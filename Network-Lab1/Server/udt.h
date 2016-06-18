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

const tcp::endpoint serverEndpoint(boost::asio::ip::address::from_string("127.0.0.1"), 8000);

struct UDT_Packet {
	enum PacketFlag :uint32_t {
		UDT_DATA,
		UDT_SYN,
		UDT_FIN,
		UDT_ACK,
		UDT_SYNACK,
		UDT_INVALID
	};

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

	UDT_Packet(char *buf):
		header(*(Header*)buf),
		pRawPacket(new char[sizeof(header)+header.length], std::default_delete<char[]>())
	{
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
};

class UDT_Session : public std::enable_shared_from_this<UDT_Session> {
public:
	//	enum UDT_SessionMode { UDT_MODE_SENDER, UDT_MODE_RECEIVER };
	static const int max_length=1024;

	UDT_Session(boost::asio::io_service &io, udp::socket udpSocket, tcp::socket tcp_socket, udp::endpoint &endpoint, uint32_t startId=0, bool accepting=false) :
		io_(io),
		firstSentId(startId), //the first sent packet id in queue
		lastSentId(startId), //the last sent packet id in queue
		udpSocket_(std::move(udpSocket)),
		tcpSocket_(std::move(tcp_socket))
	{
		udpSocket_.async_connect(endpoint, std::bind(&UDT_Session::UdpConnectHandler, this, std::placeholders::_1, accepting));
		if (accepting) {
			tcpSocket_.async_connect(serverEndpoint, [this](const boost::system::error_code &ec) {
				if (ec) {
#ifdef _DEBUG
					throw std::exception(string("Error: ")+ec.message());
#endif // _DEBUG
				}
			});
		}
	}

	~UDT_Session() noexcept {
		close();
		cerr<<yellow
			<<"Connection from "<<tcpSocket_.remote_endpoint().address()<<":"<<tcpSocket_.remote_endpoint().port()
			<<" to "<<tcpSocket_.local_endpoint().address()<<":"<<tcpSocket_.local_endpoint().port()<<" closed"
			<<endl<<white;
	}

	std::string read() {
		if (receiveQueue.empty() || receiveQueue.front().header.flag==UDT_Packet::UDT_INVALID || receiveQueue.front().header.id!=firstRecvId)
			throw std::exception("recvPkt not yet available");
		string ret(receiveQueue.front().payload(), receiveQueue.front().header.length);
		receiveQueue.pop_front();
		return ret;
	}
	//TODO receiving part

	void write(string &buf) {
		sendQueue.emplace_back(io_, lastSentId++, buf);
		sendPacket(sendQueue.back()); //TODO send logic need to change for flow control
	}

	void close() {
		//TODO close udt connection //really needed?
	}

	void start() {
		cerr<<green
			<<"New connection from "<<tcpSocket_.remote_endpoint().address()<<":"<<tcpSocket_.remote_endpoint().port()
			<<" to "<<tcpSocket_.local_endpoint().address()<<":"<<tcpSocket_.local_endpoint().port()
			<<endl<<white;
		TcpAsyncRead();
		//TODO udp async read
	}

private:
	udp::socket udpSocket_;
	tcp::socket tcpSocket_;
	boost::asio::io_service &io_;
	std::deque<UDT_TimedPacket> sendQueue;
	std::deque<UDT_Packet> receiveQueue;
	uint64_t lastSentId, firstSentId, lastRecvId, firstRecvId;
	bool connectionEstablished=false;
	int resendWait=1200;
	size_t resentCount=0, sentCount=0;

	//TCP part
	char data_[max_length];

	void TcpAsyncRead() {
		auto self(shared_from_this());
		tcpSocket_.async_read_some(boost::asio::buffer(data_, max_length), [this, self](boost::system::error_code ec, size_t length) {
			if (!ec) {
				cout<<"Received length="<<length<<endl;
				write(string(data_, length));
				TcpAsyncRead();
			}
			else {
				cerr<<red
					<<"Error occurred on connection from "<<tcpSocket_.remote_endpoint().address()<<":"<<tcpSocket_.remote_endpoint().port()
					<<" to "<<tcpSocket_.local_endpoint().address()<<":"<<tcpSocket_.local_endpoint().port()<<" error="<<ec.message()
					<<endl<<white;
			}
		});
	}

	void TcpAsyncWrite() {
		auto self(shared_from_this());
		tcpSocket_.async_write_some(boost::asio::buffer(data_, max_length), [this, self](boost::system::error_code ec, size_t) {
			if (!ec) {
				TcpAsyncWrite();
			}
		});
	}

	void UdpConnectHandler(const boost::system::error_code &ec, bool accepting) {
		if (!ec) {
			if (accepting) {
				UDT_Packet pkt(lastSentId++, UDT_Packet::UDT_SYNACK);
				udpSocket_.async_send(boost::asio::buffer(pkt.rawPacket(), pkt.rawLength()), [this](boost::system::error_code ec, size_t length) {}); //TODO use sendAckPacket()
				connectionEstablished=true;
			}
			else {
				sendQueue.emplace_front(UDT_TimedPacket(io_, lastSentId++, UDT_Packet::UDT_SYN));
				sendPacket(sendQueue.front());
			}
		}
		else {
#ifdef _DEBUG
			throw std::exception((string("failed to set up UDP data channel, ec=")+ec.message()).c_str());
#endif
		}
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
#ifdef _DEBUG
				throw std::exception((string("failed to send udt packet, ec=")+ec.message()).c_str());
#endif
			}
		});
	}

	void sendAckPacket(UDT_Packet &pkt) {
		//TODO receiving ack
	}

	void ackPacketHandler(UDT_Packet &pkt) {
		if (sendQueue.empty()) return;
		if (sendQueue.front().header().id==pkt.header.id
			&& sendQueue.front().header().flag==UDT_Packet::UDT_SYN
			&& pkt.header.flag==UDT_Packet::UDT_SYNACK) //udt connection established
		{
			sendQueue.pop_front();
			sentCount=resentCount=0;
			connectionEstablished=true;
			sendAll();
#ifdef _DEBUG
			cerr<<yellow
				<<"UDT connection established"
				<<endl<<white;
#endif
			return;
		}
		for (auto &pkti:sendQueue) {
			if (pkti.header().id>firstSentId) break;
			if (pkti.status==pkti.RESENT) --resentCount;
			--sentCount;
			sendQueue.pop_front();
		}
	}

	void resendPacket(const boost::system::error_code &ec, UDT_TimedPacket *pkt) {
		if (ec!=boost::asio::error::operation_aborted) {
			sendPacket(*pkt);
		}
	}

	void sendAll() {
		if (!sendQueue.empty()) for (auto &pkt:sendQueue) {
			if (pkt.status==pkt.NOT_SENT)
				sendPacket(pkt);
		}
#ifdef _DEBUG
		cout<<"Qsize="<<sendQueue.size()<<endl;
#endif
	}
};
/*
class TCP_Session : public std::enable_shared_from_this<TCP_Session> {
	tcp::socket tcpSocket_;
	std::shared_ptr<UDT_Session> pUdtClient_;
	static const int max_length=1024;
	char data_[max_length];

	void TcpAsyncRead() {
		auto self(shared_from_this());
		tcpSocket_.async_read_some(boost::asio::buffer(data_, max_length), [this, self](boost::system::error_code ec, size_t length) {
			if (!ec) {
				cout<<"Received length="<<length<<endl;
				pUdtClient_->write(string(data_, length));
				TcpAsyncRead();
			}
			else {
				cerr<<red
					<<"Error occurred on connection from "<<tcpSocket_.remote_endpoint().address()<<":"<<tcpSocket_.remote_endpoint().port()
					<<" to "<<tcpSocket_.local_endpoint().address()<<":"<<tcpSocket_.local_endpoint().port()<<" error="<<ec.message()
					<<endl<<white;
			}
		});
	}

	void TcpAsyncWrite() {
		auto self(shared_from_this());
		tcpSocket_.async_write_some(boost::asio::buffer(data_, max_length), [this, self](boost::system::error_code ec, size_t) {
			if (!ec) {
				TcpAsyncWrite();
			}
		});
	}


public:
	TCP_Session(boost::asio::io_service &io, tcp::socket tcp_socket, udp::endpoint udpRemoteEndpoint) :
		tcpSocket_(std::move(tcp_socket))/*,
		pUdtClient_(std::make_shared<UDT_Session>(io, udp::socket(io, udp::v4()), udpRemoteEndpoint))*
	{}

	~TCP_Session() noexcept {
		cerr<<yellow
			<<"Connection from "<<tcpSocket_.remote_endpoint().address()<<":"<<tcpSocket_.remote_endpoint().port()
			<<" to "<<tcpSocket_.local_endpoint().address()<<":"<<tcpSocket_.local_endpoint().port()<<" closed"
			<<endl<<white;
	}

	void start() {
		cerr<<green
			<<"New connection from "<<tcpSocket_.remote_endpoint().address()<<":"<<tcpSocket_.remote_endpoint().port()
			<<" to "<<tcpSocket_.local_endpoint().address()<<":"<<tcpSocket_.local_endpoint().port()
			<<endl<<white;
		TcpAsyncRead();
	}
};*/

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
	//TODO add UDT listen socket

	void doAcceptNewTcpConnection() {
		tcpListenAcceptor_.async_accept(tcpListenSocket_, [this](boost::system::error_code ec) {
			if (!ec) {
				std::make_shared<UDT_Session>(io_, udp::socket(io_, udp::v4()), std::move(tcpListenSocket_), remoteEndpoint_)->start();
			}
			else {
#ifdef _DEBUG
				std::cerr<<red<< "TCP Acceptor: " << ec.message()<<"\n"<<white;
#endif
			}
			doAcceptNewTcpConnection();
		});
	}

	void doAcceptNewUdtConnection() {
		auto recv_buf=std::make_shared<char>(new char[UDT_Session::max_length], std::default_delete<int[]>());
		auto pRemoteEndpoint=std::make_shared<udp::endpoint>(new udp::endpoint());
		udpListenSocket_.async_receive_from(
			boost::asio::buffer(recv_buf.get(), UDT_Session::max_length*2),
			*pRemoteEndpoint,
			[this, recv_buf, pRemoteEndpoint](boost::system::error_code ec, size_t length) {
			if (!ec) {
				UDT_Packet pkt(recv_buf.get());
				if (pkt.header.flag==pkt.UDT_SYN) {

					std::make_shared<UDT_Session>(io_, udp::socket(io_, localEndpoint_), tcp::socket(io_, tcp::v4()), *pRemoteEndpoint)->start();

				}
			}
			else {
#ifdef _DEBUG
				std::cerr<<red<< "UDT Acceptor: " << ec.message()<<"\n"<<white;
#endif
			}
			doAcceptNewUdtConnection();
		});
	}
};