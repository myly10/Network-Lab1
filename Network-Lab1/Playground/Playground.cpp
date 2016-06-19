#include <iostream>
#include <cstdlib>
#include <string>
#include <boost/asio.hpp>
#include <streambuf>
#include <chrono>
using namespace std;
typedef boost::asio::ip::tcp tcp;
typedef boost::asio::ip::udp udp;

void handler(const boost::system::error_code &ec) {
	cout<<"handler called"<<endl;
	
}

void xxx(tcp::socket &tcpSock, int &i) {
	tcpSock.async_write_some(boost::asio::buffer("write\n"), [&i](boost::system::error_code ec, size_t) {
		cout<<"w handler called"<<endl;
		i=20;
	});
}

int main() {
	try {
		boost::asio::io_service io;
		//udp::socket udpSock2(io, udp::endpoint(boost::asio::ip::address::from_string("172.19.0.1"), 7000)), udpSock(io);
		tcp::endpoint targetEndpoint(boost::asio::ip::address::from_string("172.19.0.1"), 30001);
		cout<<"Press to start..."<<endl;
		cin.get();
		tcp::socket tcpSock(io, tcp::v4());
		int i=10;
		cout<<i<<endl;
		tcpSock.async_connect(targetEndpoint, [&tcpSock, &i](const boost::system::error_code &ec) {
			cout<<"handler called"<<endl;
			tcpSock.write_some(boost::asio::buffer("established"));
			xxx(tcpSock, i);
			cout<<"blocking..."<<endl;
			cin.get();
			cout<<i<<endl;
		});
		io.run();
	}catch (std::exception e){
		cerr<<e.what()<<endl;
	}
	cin.get();
}