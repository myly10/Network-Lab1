#include <iostream>
#include <cstdlib>
#include <string>
#include <boost/asio.hpp>
using namespace std;
typedef boost::asio::ip::tcp tcp;
typedef boost::asio::ip::udp udp;

int main() {
	try {
		boost::asio::io_service io;
		udp::socket udpSock(io, udp::v4());
		udp::endpoint targetEndpoint(boost::asio::ip::address::from_string("172.19.0.1"), 6000);
		string buf;
		srand(time(0));
		for (int i=0; i!=200; ++i) {
			buf+="AAAAAAAAAAAAAAAAAAAA....................AAAAAAAAAAAAAAAAAAAA....................";
			buf+=(char)rand();
		}
		cout<<"Press to start..."<<endl;
		cin.get();
		size_t len=udpSock.send_to(boost::asio::buffer(buf), targetEndpoint);
		cout<<len<<endl;
	}catch (std::exception e){
		cerr<<e.what()<<endl;
	}
	cin.get();
}