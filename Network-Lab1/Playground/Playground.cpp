#include <iostream>
#include <cstdlib>
#include <string>
#include <boost/asio.hpp>
#include <streambuf>
using namespace std;
typedef boost::asio::ip::tcp tcp;
typedef boost::asio::ip::udp udp;

struct ttt {
	enum PacketFlag :uint32_t {
		UDT_DATA,
		UDT_SYN,
		UDT_FIN,
		UDT_ACK,
		UDT_SYNACK
	};

	uint32_t length=12, flag=UDT_SYNACK, id=0;
};

int main() {
	try {
		boost::asio::io_service io;
		udp::socket udpSock(io, udp::endpoint(boost::asio::ip::address::from_string("172.20.0.1"), 8000));
		udp::endpoint targetEndpoint(boost::asio::ip::address::from_string("172.20.0.2"), 7000);
		cout<<"Press to start..."<<endl;
		cin.get();
		ttt t;
		size_t len=udpSock.send_to(boost::asio::buffer((char*)&t, 12), targetEndpoint);
		cout<<len<<endl;
	}catch (std::exception e){
		cerr<<e.what()<<endl;
	}
	cin.get();
}