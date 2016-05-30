#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include "ColoredConsole.h"
using namespace std;

typedef boost::asio::ip::tcp tcp;
typedef boost::asio::ip::udp udp;

class TCP_Session : public enable_shared_from_this<TCP_Session>{
public:
	TCP_Session(tcp::socket tcp_socket/*, udp::socket udp_socket*/) : tcpSocket_(move(tcp_socket)/*, udpSocket_(move(udp_socket))*/){}

	void start(){
		cerr<<green<<"New connection from "
			<<tcpSocket_.remote_endpoint().address()<<":"<<tcpSocket_.remote_endpoint().port()<<" to "
			<<tcpSocket_.local_endpoint().address()<<":"<<tcpSocket_.local_endpoint().port()
			<<endl<<white;
		TcpRead();
		UdpRead();
	}

private:
	void TcpRead(){
		auto self(shared_from_this());
		tcpSocket_.async_read_some(boost::asio::buffer(data_, max_length), [this, self](boost::system::error_code ec, size_t length){
			if (!ec){
				cout<<"Received length="<<length<<endl;
				TcpRead();
			}
			else if (ec==boost::asio::error::eof) {
				cerr<<red
					<<"Connection from "<<tcpSocket_.remote_endpoint().address()<<":"<<tcpSocket_.remote_endpoint().port()<<" to "
					<<tcpSocket_.local_endpoint().address()<<":"<<tcpSocket_.local_endpoint().port()
					<<" closed"<<endl<<white;
			}
			UdpWrite();
		});
	}

	void TcpWrite(){
		auto self(shared_from_this());
		boost::asio::async_write(tcpSocket_, boost::asio::buffer(data_, max_length), [this, self](boost::system::error_code ec, size_t){
			if (!ec){
			}
			TcpWrite();
		});
	}

	void UdpRead() {

	}

	void UdpWrite() {

	}

	tcp::socket tcpSocket_;
	udp::socket udpSocket_;
	static const int max_length=1500;
	char data_[max_length];
};

class UDT : enable_shared_from_this<UDT> {
public:
	UDT(boost::asio::io_service &io, const boost::asio::ip::address listenAddr, const int listenPort, const boost::asio::ip::address remoteAddr, const int remotePort)
		:udpSocket_(io, udp::v4()),
		listenSocket_(io),
		listenAcceptor_(io, tcp::endpoint((listenAddr), listenPort))
	{
		doAcceptNewConnection();
	}

private:
	tcp::socket listenSocket_;
	udp::socket udpSocket_;
	tcp::acceptor listenAcceptor_;

	void doAcceptNewConnection(){
		listenAcceptor_.async_accept(listenSocket_, [this](boost::system::error_code ec) {
			if (!ec) make_shared<TCP_Session>(move(listenSocket_))->start();
			else cerr<<red<<"Error: "<<ec.message()<<endl<<white;
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
	catch (exception& e) {
		cerr <<red<< "Exception: \"" << e.what()<<"\"\n"<<white;
	}
//	cin.get();
	return 0;
}