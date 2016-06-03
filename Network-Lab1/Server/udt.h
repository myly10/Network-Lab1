#pragma once
#include <cstdlib>
#include <iostream>
#include <memory>
#include <boost/asio.hpp>
#include "ColoredConsole.h"
#include <deque>
#include <string>
#include <cstdint>

using std::cout;
using std::cerr;
using std::endl;
using std::cin;
using std::string;

typedef boost::asio::ip::tcp tcp;
typedef boost::asio::ip::udp udp;

struct UDT_Packet {
	enum PacketFlag :uint32_t {
		UDT_DATA,
		UDT_SYN,
		UDT_FIN,
		UDT_ACK,
		UDT_SYNACK
	};

	struct Header {
		uint32_t length;
		uint32_t flag;
		uint32_t id;

		Header(const Header &h) :
			length(h.length),
			flag(h.flag),
			id(h.id)
		{}

		Header(const uint32_t id, const uint32_t _flag, const string &_data) :
			length(sizeof(uint32_t)*3+_data.size()),
			flag(_flag),
			id(id)
		{}

		Header(const uint32_t id, const uint32_t _flag) :
			length(sizeof(uint32_t)*3),
			flag(_flag),
			id(id)
		{}
	} header;
	std::shared_ptr<char> packet_;

	UDT_Packet(const UDT_Packet &pkt) :
		packet_(pkt.packet_),
		header(pkt.header)
	{}

	UDT_Packet(const uint32_t id, const uint32_t _flag, const string &_data) :
		packet_(new char[sizeof(header)+_data.size()]),
		header(id, _flag, _data)
	{
		auto pPacket=packet_.get();
		memcpy(pPacket, &header, sizeof(header));
		memcpy(pPacket+sizeof(header), _data.data(), _data.size());
	}

	UDT_Packet(const uint32_t id, const uint32_t _flag) :
		packet_(new char[sizeof(header)]),
		header(id, _flag)
	{
		auto pPacket=packet_.get();
		memcpy(pPacket, &header, sizeof(header));
	}

	auto data() {
		return packet_.get();
	}
};

class UDT_TimedPacket {
	UDT_Packet packet_;
public:
	enum {SENT, RESENT, NOT_SENT};
	int status=NOT_SENT;
	boost::asio::deadline_timer timer;

	UDT_TimedPacket(UDT_TimedPacket &pkt) :
		packet_(pkt.packet_),
		timer(pkt.timer.get_io_service())
	{}

	UDT_TimedPacket(boost::asio::io_service &io, uint32_t id, string &data, uint32_t _flag=UDT_Packet::UDT_DATA) :
		packet_(id, _flag, data),
		timer(io)
	{}

	UDT_TimedPacket(boost::asio::io_service &io, uint32_t id, uint32_t _flag=UDT_Packet::UDT_DATA) :
		timer(io),
		packet_(id, _flag)
	{}

	~UDT_TimedPacket() noexcept {
		timer.cancel();
	}

	auto data() {
		return packet_.data();
	}

	auto length() {
		return packet_.header.length;
	}

	auto &header() {
		return packet_.header;
	}

	void resetTimer(const int millisec) {
		timer.expires_from_now(boost::posix_time::millisec(millisec));
	}


};

class UDT_Session : public std::enable_shared_from_this<UDT_Session> {
	udp::socket udpSocket_;
	boost::asio::io_service &io_;
	std::deque<UDT_TimedPacket> sendQueue;
	uint32_t lastId, firstId;
	bool connectionEstablished=false;
	int resendWait=1200;
	size_t resentCount=0, sentCount=0;

	void UdpConnectHandler(const boost::system::error_code &ec) {
		if (!ec) {
			UdtRequestConnection();
		}
		else {
			throw std::exception((string("failed to set up UDP data channel, ec=")+ec.message()).c_str());
		}
	}

	void UdtRequestConnection() {
		sendQueue.emplace_front(UDT_TimedPacket(io_, lastId++, UDT_Packet::UDT_SYN));
		sendPacket(sendQueue.front());
	}

	void sendPacket(UDT_TimedPacket &pkt) {
		if (pkt.header().flag==UDT_Packet::UDT_DATA && !connectionEstablished) return;
		auto self(shared_from_this());
		udpSocket_.async_send(boost::asio::buffer(pkt.data(), pkt.header().length), [this, self, &pkt](boost::system::error_code ec, size_t length) {
			if (!ec) {
				if (pkt.status==pkt.NOT_SENT) {
					++sentCount;
					pkt.status=pkt.SENT;
				}
				else if (pkt.status==pkt.SENT) {
					++resentCount;
					pkt.status=pkt.RESENT;
				}
				pkt.resetTimer(resendWait);
				pkt.timer.async_wait(bind(&UDT_Session::resendPacket, this, std::placeholders::_1, &pkt));
			}
			else {
				throw std::exception((string("failed to send udt packet, ec=")+ec.message()).c_str());
			}
		});
	}

	void ackPacket(UDT_Packet &pkt) {
		firstId=std::max(firstId, pkt.header.id);
		if (!sendQueue.empty()) {
			if (sendQueue.front().header().id==pkt.header.id && sendQueue.front().header().flag==UDT_Packet::UDT_SYN && pkt.header.flag==UDT_Packet::UDT_SYNACK) { //udt connection established
				sendQueue.pop_front();
				sentCount=resentCount=0;
				connectionEstablished=true;
				sendAll();
				cerr<<yellow
					<<"UDT connection established"
					<<endl<<white;
				return;
			}
			for (auto &pkti:sendQueue) {
				if (pkti.header().id>firstId) break;
				if (pkti.status==pkti.RESENT) --resentCount;
				--sentCount;
				sendQueue.pop_front();
			}
		}
	}

	void resendPacket(const boost::system::error_code &ec, UDT_TimedPacket *pkt) {
		if (ec!=boost::asio::error::operation_aborted) {
			sendPacket(*pkt);
		}
	}

	void sendAll() {
		if (!sendQueue.empty()) for (auto &pkt:sendQueue) {
			if (pkt.status==pkt.NOT_SENT)
				sendPacket(pkt);
		}
		cout<<"Qsize="<<sendQueue.size()<<endl;
	}

public:
	enum UDT_SessionMode { UDT_MODE_SENDER, UDT_MODE_RECEIVER };

	UDT_Session(UDT_SessionMode mode, boost::asio::io_service &io, udp::socket udpSocket, udp::endpoint &endpoint, uint32_t startId=0) :
		io_(io),
		firstId(startId), //the first sent packet id in queue
		lastId(startId), //the last sent packet id in queue
		udpSocket_(std::move(udpSocket))
	{
		udpSocket_.async_connect(endpoint, std::bind(&UDT_Session::UdpConnectHandler, this, std::placeholders::_1));
	}

	void read() {
		//TODO
	}
	//TODO receiving part

	void write(string &&buf) {
		sendQueue.emplace_back(io_, lastId++, buf);
		sendPacket(sendQueue.back());
	}
};