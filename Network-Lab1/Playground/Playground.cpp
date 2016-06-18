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
		udp::socket udpSock2(io, udp::endpoint(boost::asio::ip::address::from_string("172.20.0.1"), 8000)), udpSock(io);
		udp::endpoint targetEndpoint(boost::asio::ip::address::from_string("172.20.0.2"), 7000);
		cout<<"Press to start..."<<endl;
		cin.get();
		char s[1024];
		udp::endpoint remoteEndpoint;
		size_t len=udpSock2.receive_from(boost::asio::buffer(s, 1024), remoteEndpoint);
		cout<<"From "<<udpSock2.local_endpoint().address()<<":"<<udpSock2.local_endpoint().port()<<" to "<<remoteEndpoint.address()<<":"<<remoteEndpoint.port()<<endl;
		udpSock.connect(remoteEndpoint);
		udpSock.send(boost::asio::buffer("acked"));
#ifdef _DEBUG
		cout<<"Done."<<endl<<len<<endl<<string(s, len)<<endl;
#endif
		len=udpSock2.receive_from(boost::asio::buffer(s, 1024), remoteEndpoint);
		cout<<"From "<<udpSock2.local_endpoint().address()<<":"<<udpSock2.local_endpoint().port()<<" to "<<remoteEndpoint.address()<<":"<<remoteEndpoint.port()<<endl;
		len=udpSock.receive(boost::asio::buffer(s, 1024));
		cout<<len<<endl<<string(s, len)<<endl;
		len=udpSock.receive(boost::asio::buffer(s, 1024));
		cout<<len<<endl<<string(s, len)<<endl;
	}catch (std::exception e){
		cerr<<e.what()<<endl;
	}
	cin.get();
}