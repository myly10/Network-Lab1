#pragma once
#include <cstdlib>
#include <iostream>
#include <memory>
#include <deque>
#include <string>
#include <cstdint>
#include <boost/asio.hpp>
#include "ColoredConsole.h"

using std::cout;
using std::cerr;
using std::endl;
using std::cin;
using std::string;

typedef boost::asio::ip::tcp tcp;
typedef boost::asio::ip::udp udp;

#include "ServerConfig.def"

struct UDT_Packet {
	struct Header {
		uint32_t length;
		uint32_t flag;
		uint64_t id;

		Header(const Header &h) :
			length(h.length),
			flag(h.flag),
			id(h.id)
		{}

		Header(const uint64_t id, const uint32_t _flag, const string &_data) :
			length(_data.size()),
			flag(_flag),
			id(id)
		{}

		Header(const uint64_t id, const uint32_t _flag) :
			length(0),
			flag(_flag),
			id(id)
		{}
	} header;

	std::shared_ptr<char> pRawPacket;
	static const int max_length=maxPayloadLength+sizeof(Header);
	enum PacketFlag :uint32_t {
		UDT_DATA,
		UDT_SYN,
		UDT_FIN,
		UDT_ACK,
		UDT_SYNACK,
		UDT_INVALID
	};

	UDT_Packet() :
		header(0, UDT_INVALID)
	{}

	UDT_Packet(const UDT_Packet &pkt) :
		pRawPacket(pkt.pRawPacket),
		header(pkt.header)
	{}

	UDT_Packet(const uint64_t id, const uint32_t _flag, const string &_data) :
		pRawPacket(new char[sizeof(header)+_data.size()], std::default_delete<char[]>()),
		header(id, _flag, _data)
	{
		memcpy(rawPacket(), &header, sizeof(header));
		memcpy(payload(), _data.data(), _data.size());
	}

	UDT_Packet(char *buf, size_t length):
		header(*(Header*)buf),
		pRawPacket(new char[length], std::default_delete<char[]>())
	{
		if (length!=rawLength()) {
			header.flag=UDT_INVALID;
			return;
		}
		memcpy(rawPacket(), &header, sizeof(header));
		memcpy(payload(), buf+sizeof(header), header.length);
	}

	UDT_Packet(const uint64_t id, const uint32_t _flag) :
		pRawPacket(new char[sizeof(header)], std::default_delete<char[]>()),
		header(id, _flag)
	{
		memcpy(rawPacket(), &header, sizeof(header));
	}

	UDT_Packet &operator=(const UDT_Packet &pkt) {
		pRawPacket=pkt.pRawPacket;
		header=pkt.header;
		return *this;
	}

	char *rawPacket() {
		return pRawPacket.get();
	}

	uint32_t rawLength() {
		return header.length+sizeof(header);
	}

	char *payload() {
		return rawPacket()+sizeof(header);
	}
};

class UDT_TimedPacket {
	UDT_Packet packet_;
public:
	enum {SENT, RESENT, NOT_SENT, ACKED};
	int status=NOT_SENT;
	boost::asio::deadline_timer timer;

	UDT_TimedPacket(UDT_TimedPacket &pkt) :
		packet_(pkt.packet_),
		timer(pkt.timer.get_io_service())
	{}

	UDT_TimedPacket(boost::asio::io_service &io, uint64_t id, string &data, uint32_t _flag=UDT_Packet::UDT_DATA) :
		packet_(id, _flag, data),
		timer(io)
	{}

	UDT_TimedPacket(boost::asio::io_service &io, uint64_t id, uint32_t _flag=UDT_Packet::UDT_DATA) :
		timer(io),
		packet_(id, _flag)
	{}

	~UDT_TimedPacket() noexcept {
		timer.cancel();
	}

	auto data() {
		return packet_.rawPacket();
	}

	const auto length() {
		return packet_.header.length+sizeof(packet_.header);
	}

	auto &header() {
		return packet_.header;
	}

	void resetTimer(const int millisec) {
		timer.expires_from_now(boost::posix_time::millisec(millisec));
	}

	void ack() {
		timer.cancel();
		status=ACKED;
	}
};

class UDT_Session : public std::enable_shared_from_this<UDT_Session> {
public:
	//	enum UDT_SessionMode { UDT_MODE_SENDER, UDT_MODE_RECEIVER };
	static const int maxPayloadLength=1024;

	UDT_Session(boost::asio::io_service &io, udp::socket udpSocket, tcp::socket tcp_socket, udp::endpoint &endpoint, bool accepting=false, uint32_t startId=0) :
		io_(io),
		recvQueueHeadId(startId),
		lastSentId(startId),
		firstSentId(startId), // Use this id to avoid transmission of initial send id,
		udpEndpoint_(endpoint),
		udpSocket_(std::move(udpSocket)),
		tcpSocket_(std::move(tcp_socket))
	{
		if (accepting) {
			tcpSocket_.async_connect(serverEndpoint, [this](const boost::system::error_code &ec) {
				if (ec) {
					throw std::exception((string("Error: ")+ec.message()).c_str());
				}
			});
		}
		else {
			udpSocket_.async_connect(endpoint, std::bind(&UDT_Session::UdtConnectHandler, this, std::placeholders::_1, accepting));
		}
	}
	/*
	~UDT_Session() noexcept {
#ifdef _DEBUG
		cerr<<yellow
			<<"TCP Connection from "<<tcpSocket_.remote_endpoint().address()<<":"<<tcpSocket_.remote_endpoint().port()
			<<" to "<<tcpSocket_.local_endpoint().address()<<":"<<tcpSocket_.local_endpoint().port()<<" closed"
			<<endl<<white;
#endif
	}*/

	std::string read() {
		if (receiveQueue.empty() || receiveQueue.front().header.flag==UDT_Packet::UDT_INVALID || receiveQueue.front().header.id!=recvQueueHeadId)
			return string();
		string ret(receiveQueue.front().payload(), receiveQueue.front().header.length);
		receiveQueue.pop_front();
		recvQueueHeadId++;
		return ret;
	}

	void write(string &buf) {
		sendQueue.emplace_back(io_, lastSentId++, buf);
		sendPacket(sendQueue.back()); //TODO send logic need to change for flow control
	}

	void close() {
		//TODO close udt connection //really needed?
	}

	void start() {
		TcpAsyncRead();
		UdtAsyncRead();
	}

private:
	udp::socket udpSocket_;
	tcp::socket tcpSocket_;
	udp::endpoint udpEndpoint_;
	boost::asio::io_service &io_;
	std::deque<UDT_TimedPacket> sendQueue;
	std::deque<UDT_Packet> receiveQueue;
	uint64_t lastSentId=0, firstSentId=0, recvQueueHeadId;
	bool connectionEstablished=false;
	int resendWait=1200;
	size_t resentCount=0, sentCount=0;

	//TCP part
	char data_[UDT_Packet::max_length];

	void TcpAsyncRead() {
		auto self(shared_from_this());
		tcpSocket_.async_read_some(boost::asio::buffer(data_, maxPayloadLength), [this, self](boost::system::error_code ec, size_t length) {
			if (!ec) {
				cout<<"TCP Received length="<<length<<endl;
				write(string(data_, length));
				TcpAsyncRead();
			}
#ifdef _DEBUG
			else {
				cerr<<red
					<<"Error occurred on TCP connection from "<<tcpSocket_.remote_endpoint().address()<<":"<<tcpSocket_.remote_endpoint().port()
					<<" to "<<tcpSocket_.local_endpoint().address()<<":"<<tcpSocket_.local_endpoint().port()<<" error="<<ec.message()
					<<endl<<white;
			}
#endif // DEBUG
		});
	}

	void TcpAsyncWrite() {
		auto self(shared_from_this());
		string sbuf(read());
		if (!sbuf.size()) return;
		tcpSocket_.async_write_some(boost::asio::buffer(sbuf.data(), sbuf.size()), [this, self](boost::system::error_code ec, size_t) {
			if (!ec) {
				TcpAsyncWrite();
			}
		});
	}

	void UdtConnectHandler(const boost::system::error_code &ec, bool accepting) {
		if (!ec) {
			if (accepting) {
				UDT_Packet pkt(recvQueueHeadId++, UDT_Packet::UDT_SYNACK);
				sendAckPacket(pkt);
				connectionEstablished=true;

			}
			else {
				sendQueue.emplace_front(UDT_TimedPacket(io_, lastSentId++, UDT_Packet::UDT_SYN));
				sendPacket(sendQueue.front());
			}
		}
		else {
			throw std::exception((string("failed to set up UDP data channel, ec=")+ec.message()).c_str());
		}
	}

	void UdtAsyncRead() {
		auto self(shared_from_this());
		udpSocket_.async_receive_from(boost::asio::buffer(data_, UDT_Packet::max_length), udpEndpoint_, [this, self](boost::system::error_code ec, size_t length) {
			if (!ec) {
				if (length<sizeof(UDT_Packet::Header)) {
#ifdef _DEBUG
					cerr<<red<<"malformed udt packet, length="<<length<<endl<<white;
#endif
				}
				else {
					UDT_Packet pkt(data_, length);
					if (pkt.header.flag==UDT_Packet::UDT_SYNACK)
						udpSocket_.connect(udpEndpoint_);
					if (pkt.header.flag!=pkt.UDT_INVALID) {
						processReceivedPacket(pkt);
					}
				}
			}
		});
	}

	void sendPacket(UDT_TimedPacket &pkt) {
		if (pkt.header().flag==UDT_Packet::UDT_DATA && !connectionEstablished) return;
		auto self(shared_from_this());
		udpSocket_.async_send(boost::asio::buffer(pkt.data(), pkt.length()), [this, self, &pkt](boost::system::error_code ec, size_t length) {
			if (!ec) {
				if (pkt.status==pkt.NOT_SENT) {
					++sentCount;
					pkt.status=pkt.SENT;
				}
				else if (pkt.status==pkt.SENT) {
					++resentCount;
					pkt.status=pkt.RESENT;
				}
				pkt.resetTimer(resendWait);
				pkt.timer.async_wait(std::bind(&UDT_Session::resendPacket, this, std::placeholders::_1, &pkt));
			}
			else {
				throw std::exception((string("failed to send udt packet, ec=")+ec.message()).c_str());
			}
		});
	}

	void sendAckPacket(UDT_Packet &pkt) { //send given ACK packet
		if (pkt.header.flag!=pkt.UDT_ACK && pkt.header.flag!=pkt.UDT_SYNACK) {
#ifdef _DEBUG
			throw std::exception("Error: try to send invalid packet through sendAckPacket()");
#endif
			return;
		}
		udpSocket_.async_send(boost::asio::buffer(pkt.rawPacket(), pkt.rawLength()), [this](boost::system::error_code ec, size_t length) {
#ifdef _DEBUG
			if (ec) throw std::exception("error sending ack packet");
#endif
		});
		//TODO FIN impl
	}

	void processReceivedPacket(UDT_Packet &rpkt) {
		if (sendQueue.empty()) return;
		if (sendQueue.front().header().flag==UDT_Packet::UDT_SYN
			&& rpkt.header.flag==UDT_Packet::UDT_SYNACK
			&& sendQueue.front().header().id==rpkt.header.id) //requested udt connection has been accepted
		{
			sendQueue.pop_front();
#ifdef _DEBUG
			assert(sendQueue.size()==0);
#endif
			sentCount=resentCount=0;
			connectionEstablished=true;
			sendAll();
#ifdef _DEBUG
			cerr<<green
				<<"New UDT connection from "<<udpSocket_.remote_endpoint().address()<<":"<<udpSocket_.remote_endpoint().port()
				<<" to "<<udpSocket_.local_endpoint().address()<<":"<<udpSocket_.local_endpoint().port()
				<<endl<<white;
#endif
			return;
		}
		if (rpkt.header.flag==rpkt.UDT_SYN && rpkt.header.id<recvQueueHeadId) {
			sendAckPacket(UDT_Packet(rpkt.header.id, rpkt.UDT_SYNACK));
			return;
		}
		if (rpkt.header.flag==rpkt.UDT_DATA) {
			int recvQueueIndex=rpkt.header.id-recvQueueHeadId;
			if (recvQueueIndex>=0) {
				if (recvQueueIndex>=receiveQueue.size()) receiveQueue.resize(recvQueueIndex+1);
				if (receiveQueue[recvQueueIndex].header.id!=rpkt.header.id)
					receiveQueue[recvQueueIndex]=rpkt;
				TcpAsyncWrite();
			}
			sendAckPacket(UDT_Packet(rpkt.header.id, rpkt.UDT_ACK));
			return;
		}
		if (rpkt.header.flag==rpkt.UDT_ACK) { // sent and resent count are now for current window unacked packets only, may be changed later
			int sendQueueIndex=rpkt.header.id-firstSentId;
			assert(sendQueueIndex<sendQueue.size());
			if (sendQueueIndex>=0) {
				if (sendQueue[sendQueueIndex].status!=UDT_TimedPacket::ACKED) return;
				if (sendQueue[sendQueueIndex].status==UDT_TimedPacket::RESENT) --resentCount;
				--sentCount;
				sendQueue[sendQueueIndex].ack();
				for (auto &pkti:sendQueue) {
					if (pkti.status!=pkti.ACKED) break;
					sendQueue.pop_front();
					++firstSentId;
				}
			}
			return;
		}
		if (rpkt.header.flag==rpkt.UDT_FIN) {
			//TODO
		}
	}

	void resendPacket(const boost::system::error_code &ec, UDT_TimedPacket *pkt) {
		if (ec!=boost::asio::error::operation_aborted) {
			sendPacket(*pkt);
		}
	}

	void sendAll() {
		//TODO impl a better sender, with flow control
		if (!sendQueue.empty()) {
			for (auto &pkt:sendQueue) {
				if (pkt.status==pkt.NOT_SENT)
					sendPacket(pkt);
			}
		}
#ifdef _DEBUG
		cout<<"Qsize="<<sendQueue.size()<<endl;
#endif
	}
};

class UDT_Proxy {
public:
	UDT_Proxy(const boost::asio::ip::address &listenAddr, const int listenPort, const boost::asio::ip::address &remoteAddr, const int remotePort) :
		io_(),
		tcpListenSocket_(io_),
		tcpListenAcceptor_(io_, tcp::endpoint(listenAddr, listenPort)),
		remoteEndpoint_(remoteAddr, remotePort),
		localEndpoint_(listenAddr, listenPort),
		udpListenSocket_(io_, localEndpoint_)
	{
		doAcceptNewTcpConnection();
		doAcceptNewUdtConnection();
		io_.run();
	}

private:
	boost::asio::io_service io_;
	tcp::socket tcpListenSocket_;
	tcp::acceptor tcpListenAcceptor_;
	udp::endpoint remoteEndpoint_, localEndpoint_;
	udp::socket udpListenSocket_;

	void doAcceptNewTcpConnection() {
		tcpListenAcceptor_.async_accept(tcpListenSocket_, [this](boost::system::error_code ec) {
			if (!ec) {
				std::make_shared<UDT_Session>(io_, udp::socket(io_, udp::v4()), std::move(tcpListenSocket_), remoteEndpoint_)->start();
			}
#ifdef _DEBUG
			else {
				std::cerr<<red<< "TCP Acceptor: " << ec.message()<<"\n"<<white;
			}
#endif
			doAcceptNewTcpConnection();
		});
	}

	void doAcceptNewUdtConnection() {
		std::shared_ptr<char> recv_buf(new char[UDT_Packet::max_length], std::default_delete<char[]>());
		std::shared_ptr<udp::endpoint> pRemoteEndpoint(new udp::endpoint());
		udpListenSocket_.async_receive_from(boost::asio::buffer(recv_buf.get(), UDT_Packet::max_length), *pRemoteEndpoint, [this, recv_buf, pRemoteEndpoint](boost::system::error_code ec, size_t length) {
			if (!ec) {
				if (length<sizeof(UDT_Packet::Header)) {
#ifdef _DEBUG
					cerr<<red<<"malformed udt packet, length="<<length<<endl<<white;
#endif
				}
				else {
					UDT_Packet pkt(recv_buf.get(), length);
					if (pkt.header.flag==pkt.UDT_SYN) {
						std::make_shared<UDT_Session>(io_, udp::socket(io_, udp::v4()), tcp::socket(io_, tcp::v4()), *pRemoteEndpoint, true, pkt.header.id)->start();
					}
				}
			}
#ifdef _DEBUG
			else {
				std::cerr<<red<< "UDT Acceptor: " << ec.message()<<"\n"<<white;
			}
#endif
			doAcceptNewUdtConnection();
		});
	}
};