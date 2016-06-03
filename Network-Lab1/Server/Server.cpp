#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <string>
#include "ColoredConsole.h"
#include "udt.h"

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
			else {
				cerr<<red
					<<"Error occurred on connection from "<<tcpSocket_.remote_endpoint().address()<<":"<<tcpSocket_.remote_endpoint().port()
					<<" to "<<tcpSocket_.local_endpoint().address()<<":"<<tcpSocket_.local_endpoint().port()<<" error="<<ec.message()
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
		pUdt_(std::make_shared<UDT_Session>(UDT_Session::UDT_MODE_SENDER, io, udp::socket(io, udp::v4()), udpRemoteEndpoint))
	{}

	~TCP_Session() noexcept{
		cerr<<yellow
			<<"Connection from "<<tcpSocket_.remote_endpoint().address()<<":"<<tcpSocket_.remote_endpoint().port()
			<<" to "<<tcpSocket_.local_endpoint().address()<<":"<<tcpSocket_.local_endpoint().port()<<" closed"
			<<endl<<white;
	}

	void start(){
		cerr<<green
			<<"New connection from "<<tcpSocket_.remote_endpoint().address()<<":"<<tcpSocket_.remote_endpoint().port()
			<<" to "<<tcpSocket_.local_endpoint().address()<<":"<<tcpSocket_.local_endpoint().port()
			<<endl<<white;
		AsyncRead();
	}
};

class UDT_Proxy {
public:
	UDT_Proxy(boost::asio::io_service &io, const boost::asio::ip::address &listenAddr, const int listenPort, const boost::asio::ip::address &remoteAddr, const int remotePort) :
		io_(io),
		tcpListenSocket_(io),
		tcpListenAcceptor_(io, tcp::endpoint(listenAddr, listenPort)),
		remoteEndpoint_(remoteAddr, remotePort)
	{
		doAcceptNewConnection();
	}

private:
	boost::asio::io_service &io_;
	tcp::socket tcpListenSocket_;
	tcp::acceptor tcpListenAcceptor_;
	udp::endpoint remoteEndpoint_;
	//TODO add UDT listen socket

	void doAcceptNewConnection(){ //TODO add UDP receiver here
		tcpListenAcceptor_.async_accept(tcpListenSocket_, [this](boost::system::error_code ec) {
			if (!ec) {
				std::make_shared<TCP_Session>(io_, std::move(tcpListenSocket_), remoteEndpoint_)->start();
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
		UDT_Proxy s(io, boost::asio::ip::address::from_string(argv[1]), std::atoi(argv[2]), boost::asio::ip::address::from_string(argv[3]), std::atoi(argv[4]));
		io.run();
	}
	catch (std::exception& e) {
		cerr<<red<<"Exception: "<<e.what()<<"\n"<<white;
		cin.get();
	}
	return 0;
}