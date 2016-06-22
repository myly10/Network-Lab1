#include <iostream>
#include "totp.h"


int main(int argc, char* argv[]) {
	if (argc < 5) {
		cerr <<red<< "Usage: "<<argv[0]<<" localAddr localPort remoteAddr remotePort\n"<<white;
		return 1;
	}
	try {
		if (argc==5)
			TCP_Proxy s(boost::asio::ip::address::from_string(argv[1]), std::atoi(argv[2]), boost::asio::ip::address::from_string(argv[3]), std::atoi(argv[4]));
		else if (argc==6)
			TCP_Proxy s(boost::asio::ip::address::from_string(argv[1]), std::atoi(argv[2]), boost::asio::ip::address::from_string(argv[3]), std::atoi(argv[4]), true);
	}
	catch (std::exception& e) {
		cerr<<red<<"Exception: "<<e.what()<<"\n"<<white;
		cin.get();
	}
	return 0;
}