/*#include <iostream>
#include <cstdlib>
#include <string>
#include <boost/asio.hpp>
#include <streambuf>
using namespace std;
typedef boost::asio::ip::tcp tcp;
typedef boost::asio::ip::udp udp;

int main() {
	try {
		boost::asio::io_service io;
		udp::socket udpSock(io, udp::v4());
		udp::endpoint targetEndpoint(boost::asio::ip::address::from_string("172.19.0.1"), 6000);
		streambuf buf;
		srand(time(0));
		for (int i=0; i!=200; ++i) {
			buf.sputn("AAAAAAAAAAAAAAAAAAAA....................AAAAAAAAAAAAAAAAAAAA....................");
			buf.sputc((char)rand());
		}
		cout<<"Press to start..."<<endl;
		cin.get();
		size_t len=udpSock.send_to(boost::asio::buffer(buf, 1024), targetEndpoint);
		cout<<len<<endl;
	}catch (std::exception e){
		cerr<<e.what()<<endl;
	}
	cin.get();
}*/