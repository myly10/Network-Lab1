#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include "ColoredConsole.h"
#include <deque>
#include <string>
#include <cstdint>
#include <chrono>
using std::cout;
using std::cerr;
using std::endl;
using std::cin;
using std::string;

typedef boost::asio::ip::tcp tcp;
typedef boost::asio::ip::udp udp;

class UDT_Packet {
	std::shared_ptr<char> packet_;

public:
	boost::asio::deadline_timer timer;
	int timeoutLength=1200; //ms
	bool acked=false;
	uint32_t length;
	uint32_t id;

	UDT_Packet(UDT_Packet &pkt) :
		packet_(pkt.packet_),
		timeoutLength(pkt.timeoutLength),
		timer(pkt.timer.get_io_service(), boost::posix_time::millisec(timeoutLength)),
		length(pkt.length),
		id(pkt.id)
	{}

	UDT_Packet(boost::asio::io_service &io, uint32_t id, string &data, uint32_t flag) :
		timer(io, boost::posix_time::millisec(timeoutLength)),
		length(sizeof(uint32_t)*3+data.size()),
		packet_(new char[sizeof(uint32_t)*3+data.size()]),
		id(id)
	{
		auto pPacket=packet_.get();
		*(uint32_t*)pPacket=id;
		*(uint32_t*)(pPacket+sizeof(uint32_t))=flag;
		*(uint32_t*)(pPacket+sizeof(uint32_t)*2)=data.size();
		memcpy(pPacket+sizeof(uint32_t)*3, data.data(), data.size());
	}

	~UDT_Packet() {
		timer.cancel();
	}

	auto data() {
		return packet_.get();
	}
};

class UDT_Session : public std::enable_shared_from_this<UDT_Session> {
	udp::socket udpSocket_;
	boost::asio::io_service &io_;
	std::deque<UDT_Packet> sendQueue;
	uint32_t lastId, firstId;

	void UdpConnectHandler(const boost::system::error_code &ec) {
		if (!ec) {
			UdtEstablishConnection(); //TODO
		}
		else {
			throw std::exception((string("failed to set up UDP data channel, ec=")+ec.message()).c_str());
		}
	}

	void UdtEstablishConnection()
	{
		throw std::exception("The method or operation is not implemented.");
	}


	void sendPacket(UDT_Packet &pkt) {
		auto self(shared_from_this());
		udpSocket_.async_send(boost::asio::buffer(pkt.data(), pkt.length), [this, self, &pkt](boost::system::error_code ec, size_t length) {
			if (!ec) {
				pkt.timer.expires_from_now(boost::posix_time::millisec(pkt.timeoutLength));
				pkt.timer.async_wait(bind(&UDT_Session::resendPacket, this, std::placeholders::_1, &pkt));
			}
			else {
				throw std::exception((string("failed to send udt packet, ec=")+ec.message()).c_str());
			}
		});
	}

	void ackPacket(UDT_Packet &pkt) {
		pkt.acked=true;
		firstId=std::max(firstId, pkt.id);
		for (auto &pkti:sendQueue) {
			if (pkti.id>pkt.id) break;
			//TODO
		}
	}
	
	void resendPacket(const boost::system::error_code &ec, UDT_Packet *pkt) {
		if (ec!=boost::asio::error::operation_aborted) {
			sendPacket(*pkt);
			//TODO
		}
	}

	void send() {
		for (UDT_Packet &pkt:sendQueue) {
			if (pkt.acked) continue;
			sendPacket(pkt);
		}
	}

public:
	UDT_Session(boost::asio::io_service &io, udp::endpoint &remoteEndpoint, uint32_t startId=0) :
		io_(io),
		firstId(startId),
		lastId(startId),
		udpSocket_(io, udp::v4())
	{
		udpSocket_.async_connect(remoteEndpoint, std::bind(&UDT_Session::UdpConnectHandler, this, std::placeholders::_1));
	}

	void read() {

	}

	void write(string buf) {
		sendQueue.emplace_back(io_, lastId++, std::move(buf), (uint32_t)'A');
		send();
	}
};

class TCP_Session : public std::enable_shared_from_this<TCP_Session>{
	tcp::socket tcpSocket_;
	std::shared_ptr<UDT_Session> pUdt_;
	static const int max_length=1024;
	char data_[max_length];

	void AsyncRead(){
		auto self(shared_from_this());
		tcpSocket_.async_read_some(boost::asio::buffer(data_, max_length), [this, self](boost::system::error_code ec, size_t length){
			if (!ec){
				cout<<"Received length="<<length<<endl;
				pUdt_->write(string(data_, length));
				AsyncRead();
			}
			else if (ec==boost::asio::error::eof) {
				cerr<<yellow
					<<"Connection from "<<tcpSocket_.remote_endpoint().address()<<":"<<tcpSocket_.remote_endpoint().port()
					<<" to "<<tcpSocket_.local_endpoint().address()<<":"<<tcpSocket_.local_endpoint().port()<<" closed"
					<<endl<<white;
			}
			else {
				cerr<<red
					<<"Error occurred on connection from "<<tcpSocket_.remote_endpoint().address()<<":"<<tcpSocket_.remote_endpoint().port()
					<<" to "<<tcpSocket_.local_endpoint().address()<<":"<<tcpSocket_.local_endpoint().port()
					<<endl<<white;
			}
		});
	}

	void AsyncWrite(){
		auto self(shared_from_this());
		tcpSocket_.async_write_some(boost::asio::buffer(data_, max_length), [this, self](boost::system::error_code ec, size_t){
			if (!ec) {
				AsyncWrite();
			}
		});
	}


public:
	TCP_Session(boost::asio::io_service &io, tcp::socket tcp_socket, udp::endpoint udpRemoteEndpoint) :
		tcpSocket_(std::move(tcp_socket)),
		pUdt_(std::make_shared<UDT_Session>(io, udpRemoteEndpoint))
	{}

	void start(){
		cerr<<green
			<<"New connection from "<<tcpSocket_.remote_endpoint().address()<<":"<<tcpSocket_.remote_endpoint().port()
			<<" to "<<tcpSocket_.local_endpoint().address()<<":"<<tcpSocket_.local_endpoint().port()
			<<endl<<white;
		AsyncRead();
		pUdt_->read();
	}
};

class UDT {
public:
	UDT(boost::asio::io_service &io, const boost::asio::ip::address &listenAddr, const int listenPort, const boost::asio::ip::address &remoteAddr, const int remotePort) :
		io_(io),
		listenSocket_(io),
		listenAcceptor_(io, tcp::endpoint(listenAddr, listenPort)),
		remoteEndpoint_(remoteAddr, remotePort)
	{
		doAcceptNewConnection();
	}

private:
	boost::asio::io_service &io_;
	tcp::socket listenSocket_;
	tcp::acceptor listenAcceptor_;
	udp::endpoint remoteEndpoint_;

	void doAcceptNewConnection(){
		listenAcceptor_.async_accept(listenSocket_, [this](boost::system::error_code ec) {
			if (!ec) {
				std::make_shared<TCP_Session>(io_, std::move(listenSocket_), remoteEndpoint_)->start(); //TODO bug is here
			}
			else std::cerr<<red<< "Acceptor: " << ec.message()<<"\n"<<white;
			doAcceptNewConnection();
		});
	}
};

int main(int argc, char* argv[]) {
	if (argc < 5) {
		cerr <<red<< "Usage: "<<argv[0]<<" localAddr localPort remoteAddr remotePort\n"<<white;
		return 1;
	}
	try {
		boost::asio::io_service io;
		UDT s(io, boost::asio::ip::address::from_string(argv[1]), atoi(argv[2]), boost::asio::ip::address::from_string(argv[3]), atoi(argv[4]));
		io.run();
	}
	catch (std::exception& e) {
		cerr <<red<< "Exception: " << e.what()<<"\n"<<white;
		cin.get();
	}
	return 0;
}