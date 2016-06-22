#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <cstdint>
#include <utility>
#include <deque>
#include <boost/asio.hpp>
#include "ColoredConsole.h"

#ifdef _DEBUG
const bool __debug=true;
#else
const bool __debug=false;
#endif

typedef boost::asio::ip::tcp tcp;
//typedef boost::asio::ip::udp udp;
using std::cout;
using std::cerr;
using std::endl;
using std::cin;
using std::string;

class Totp_Session : public std::enable_shared_from_this<Totp_Session> {
	static const int max_length=1024;
	boost::asio::io_service &io;
	tcp::socket tcpTerminalSocket, totpSocket;
	tcp::endpoint connectTo;
	bool isAlice=false;
	bool writingTotp=false, writingTerminal=false, disconnecting=false;
	size_t sum=0;
	std::deque<string> ToTotpWindow, ToTerminalWindow;

	void ReadFromTerminal() {
		auto self(shared_from_this());
		char *buf=new char[max_length];
		tcpTerminalSocket.async_read_some(boost::asio::buffer(buf, max_length), [this, self, buf](boost::system::error_code ec, size_t length) {
			if (!ec) {
				ToTotpWindow.emplace_back(buf, length);
				delete[] buf;
	//			cerr<<blue<<"Starting totpSocket write"<<endl<<white;
				//			cerr<<blue<<"Recurring RedirectToTotp();"<<endl<<white;
				if (!writingTotp) {
					writingTotp=true;
					WriteToTotp();
				}
				ReadFromTerminal();
			}
			else {
				if (__debug) cerr<<red<<"tcpTerminalSocket.async_read_some --> "<<ec.message()<<" length="<<length<<endl<<white;
				disconnecting=true;
				if (!writingTotp) totpSocket.close();
				tcpTerminalSocket.close();
			}
		});
	}

	void WriteToTotp() {
		if (ToTotpWindow.empty()) {
			if (disconnecting) totpSocket.close();
			writingTotp=false;
			return;
		}
		string s(ToTotpWindow.front());
		ToTotpWindow.pop_front();
		auto self(shared_from_this());
		totpSocket.async_write_some(boost::asio::buffer(s.data(), s.size()), [this, self](boost::system::error_code ec, size_t length) {
			if (!ec) {
				WriteToTotp();
			}
		});
	}

	void ReadFromTotp() {
		auto self(shared_from_this());
		char *buf=new char[max_length];
		totpSocket.async_read_some(boost::asio::buffer(buf, max_length), [this, self, buf](boost::system::error_code ec, size_t length) {
			if (!ec) {
				//			cerr<<blue<<"Starting tcpTerminalSocket write"<<endl<<white;
				ToTerminalWindow.emplace_back(buf, length);
				delete[] buf;
				//			cerr<<blue<<"Recurring RedirectToTerminal();"<<endl<<white;
				if (!writingTerminal) {
					writingTerminal=true;
					WriteToTerminal();
				}
				ReadFromTotp();
			}
			else {
				if (__debug) cerr<<red<<"totpSocket.async_read_some --> "<<ec.message()<<" length="<<length<<endl<<white;
				disconnecting=true;
				if (!writingTerminal) tcpTerminalSocket.close();
				totpSocket.close();
			}
		});
	}

	void WriteToTerminal() {
		if (ToTerminalWindow.empty()) {
			if (disconnecting) tcpTerminalSocket.close();
			writingTerminal=false;
			return;
		}
		string s(ToTerminalWindow.front());
		ToTerminalWindow.pop_front();
		auto self(shared_from_this());
		tcpTerminalSocket.async_write_some(boost::asio::buffer(s.data(), s.size()), [this, self](boost::system::error_code ec, size_t length) {
			if (!ec) {
				WriteToTerminal();
			}
		});
	}

	void Connect(const bool isAlice){
		if (isAlice) {
			auto self(shared_from_this());
			totpSocket.async_connect(connectTo, [this, self](const boost::system::error_code &ec) {
				if (!ec) {
					if (__debug) cerr<<green<<"TOTP connected"<<endl<<white;
					totpSocket.set_option(tcp::no_delay(true));
					tcpTerminalSocket.set_option(tcp::no_delay(true));
					ReadFromTerminal();
					ReadFromTotp();
				}
				else {
					if (__debug) cerr<<red<<"TOTP connect error: "<<ec.message()<<endl<<white;
				}
			});
		}
		else {
			auto self(shared_from_this());
			tcpTerminalSocket.async_connect(connectTo, [this, self](const boost::system::error_code &ec) {
				if (!ec) {
					if (__debug) cerr<<green<<"terminal connected"<<endl<<white;
					totpSocket.set_option(tcp::no_delay(true));
					tcpTerminalSocket.set_option(tcp::no_delay(true));
					ReadFromTerminal();
					ReadFromTotp();
				}
				else {
					if (__debug) cerr<<red<<"terminal connect error: "<<ec.message()<<endl<<white;
				}
			});
		}
	}

public:
	Totp_Session(boost::asio::io_service &io_s, tcp::socket totp_socket, tcp::endpoint connect_to):
		io(io_s),
		tcpTerminalSocket(io, tcp::v4()),
		totpSocket(std::move(totp_socket)),
		connectTo(connect_to)
	{}
	Totp_Session(boost::asio::io_service &io_s, tcp::socket terminal_socket, tcp::endpoint connect_to, bool isAlice_) :
		io(io_s),
		tcpTerminalSocket(std::move(terminal_socket)),
		totpSocket(io, tcp::v4()),
		connectTo(connect_to),
		isAlice(true)
	{}
	~Totp_Session() noexcept {
		if (__debug) {
			cerr<<yellow<<"Totp session closed"<<endl<<white;
		}
	}

	void start() {
		Connect(isAlice);
	}
};

class TCP_Proxy {
	boost::asio::io_service io;
	tcp::endpoint remoteEndpoint, localEndpoint;
	tcp::socket terminalSocket, totpSocket;
	tcp::acceptor terminalListenAcceptor, totpListenAcceptor;

	void doAcceptNewTerminalConnection() {
		terminalListenAcceptor.async_accept(terminalSocket, [this](boost::system::error_code ec) {
			if (!ec) {
				if (__debug) cerr<<green<<"terminal connection accepted"<<endl<<white;
				std::make_shared<Totp_Session>(io, std::move(terminalSocket), remoteEndpoint, true)->start();
			}
#ifdef _DEBUG
			else {
				std::cerr<<red<< "Terminal Acceptor: " << ec.message()<<"\n"<<white;
			}
#endif
			doAcceptNewTerminalConnection();
		});
	}

	void doAcceptNewTotpConnection() {
		totpListenAcceptor.async_accept(totpSocket, [this](boost::system::error_code ec) {
			if (!ec) {
				if (__debug) cerr<<green<<"TOTP connection accepted"<<endl<<white;
				std::make_shared<Totp_Session>(io, std::move(totpSocket), remoteEndpoint)->start();
			}
#ifdef _DEBUG
			else {
				std::cerr<<red<< "Totp Acceptor: " << ec.message()<<"\n"<<white;
			}
#endif
			doAcceptNewTotpConnection();
		});
	}

public:
	TCP_Proxy(const boost::asio::ip::address &listenAddr, const int listenPort, const boost::asio::ip::address &remoteAddr, const int remotePort, bool isAlice) :
		io(),
		terminalSocket(io),
		totpSocket(io),
		remoteEndpoint(remoteAddr, remotePort),
		localEndpoint(listenAddr, listenPort),
		terminalListenAcceptor(io, localEndpoint),
		totpListenAcceptor(io)
	{
		doAcceptNewTerminalConnection();
		io.run();
	}

	TCP_Proxy(const boost::asio::ip::address &listenAddr, const int listenPort, const boost::asio::ip::address &remoteAddr, const int remotePort) :
		io(),
		terminalSocket(io),
		totpSocket(io),
		remoteEndpoint(boost::asio::ip::address::from_string("127.0.0.1"), 8000),
		localEndpoint(listenAddr, listenPort),
		terminalListenAcceptor(io),
		totpListenAcceptor(io, localEndpoint)
	{
		doAcceptNewTotpConnection();
		io.run();
	}
};