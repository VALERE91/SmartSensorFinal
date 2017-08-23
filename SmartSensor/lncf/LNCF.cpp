#include <cstring>

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include "CryptoEngine.h"

#include "LNCF.h"

namespace lncf {
	LNCF::LNCF(boost::asio::io_service* service)
	{
		_service = service;
	}

	LNCF::~LNCF()
	{
		if (_sender_endpoint != nullptr) {
			delete _sender_endpoint;
		}
		
		if (_listener_endpoint != nullptr) {
			delete _listener_endpoint;
		}
		
		if (_socket != nullptr) {
			delete _socket;
		}
	}

	void LNCF::Init(boost::asio::ip::address listen_address, boost::asio::ip::address group_address, int lncf_port /*= 6666*/)
	{
		//Addresses and endpoints
		_send_addr = group_address;
		_listen_addr = listen_address;
		_sender_endpoint = new boost::asio::ip::udp::endpoint(_send_addr, lncf_port);
		_listener_endpoint = new boost::asio::ip::udp::endpoint(_listen_addr, lncf_port);

		//Socket
		_socket = new boost::asio::ip::udp::socket(*_service);
	}

	void LNCF::ListenAndServe()
	{
		//Configure ASIO socket in order to listen and serve
		_socket->open(_listener_endpoint->protocol());
		_socket->set_option(boost::asio::ip::udp::socket::reuse_address(true));
		_socket->bind(*_listener_endpoint);
		// Join the multi cast group.
		_socket->set_option(boost::asio::ip::multicast::join_group(_send_addr));
		//Start the async receive
		_socket->async_receive_from(boost::asio::buffer(_data, MAX_LENGTH), *_sender_endpoint, boost::bind(&LNCF::handle_receive_from, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));

	}

	void LNCF::Stop()
	{
		_socket->shutdown(boost::asio::ip::udp::socket::shutdown_both);
		_socket->close();
	}

	void LNCF::SendClearMessage(std::string& topic, std::string& message)
	{
		if (topic.length() > 255) {
			throw std::exception("Topic is too long");
		}
		//9 bytes is the minimum size of a packet without data
		if (message.length() > MAX_LENGTH - 9) {
			throw std::exception("Data is too long");
		}

		size_t packet_length = topic.length() + message.length() + 8;
		unsigned char* packet = new unsigned char[packet_length];
		packet[0] = 0;
		packet[1] = (topic.length() & 0x000000FF);

		memcpy_s((void*)(packet + 2), packet_length, topic.c_str(), topic.length());

		packet[2 + topic.length()] = (message.length() & 0x0000FF00) >> 8;
		packet[2 + topic.length() + 1] = message.length() & 0x000000FF;

		memcpy_s((void*)(packet + 2 + topic.length() + 2), packet_length, message.c_str(), message.length());

		int32_t crc32 = CryptoEngine::CRC32((byte*)packet, 2 + topic.length() + 2 + message.length());
		packet[2 + topic.length() + 1 + message.length() + 1] = (crc32 & 0xFF000000) >> 24;
		packet[2 + topic.length() + 1 + message.length() + 2] = (crc32 & 0x00FF0000) >> 16;
		packet[2 + topic.length() + 1 + message.length() + 3] = (crc32 & 0x0000FF00) >> 8;
		packet[2 + topic.length() + 1 + message.length() + 4] = (crc32 & 0x000000FF);

		size_t bytes_send = _socket->send_to(boost::asio::buffer(packet, topic.length() + message.length() + 8), *_sender_endpoint);

		delete[] packet;
	}

	void LNCF::SendEncryptedMessage(std::string& topic, std::string& message, std::string& key_fingerprint)
	{
		/*byte* packet;
		_socket->async_send_to(asio::buffer(packet), *_sender_endpoint, boost::bind(handle_send_to, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, topic, message, packet));
		*/
	}

	void LNCF::SendDiscoveryRequest(std::string& request)
	{

	}

	void LNCF::RegisterService()
	{

	}

	void LNCF::Handle(std::string topic, LNCFHandler* handler)
	{
		if (_handlers.find(topic) != _handlers.end()) {
			throw std::exception("Topic already handle");
		}

		_handlers[topic] = handler;
	}

	std::string LNCF::RegisterEncryptionKey(unsigned char* key)
	{
		return CryptoEngine::RegisterKey(key, KEY_LENGTH);
	}

	void LNCF::handle_receive_from(boost::system::error_code error, size_t bytes_recvd)
	{
		//Minimum packet size is 9 bytes (1 for options + 1 for topic length + 1 for minimum topic + 2 for data length + 4 for CRC)
		if (bytes_recvd < 9) {
			return;
		}

		//Check CRC32
		int32_t crc = CryptoEngine::CRC32((byte*)_data, bytes_recvd - 4);
		int32_t receivedCRC = (((int)_data[bytes_recvd - 4]) & 0x000000FF) << 24 | 
							  (((int)_data[bytes_recvd - 3]) & 0x000000FF) << 16 | 
							  (((int)_data[bytes_recvd - 2]) & 0x000000FF) << 8 |
							  (((int)_data[bytes_recvd - 1]) & 0x000000FF);

		if (crc != receivedCRC) {
			return;
		}

		//Parse packet
		switch ((_data[0] >> 5))
		{
		case 0:
			parse_lncf_v1(bytes_recvd);
		default:
			break;
		}
		_socket->async_receive_from(boost::asio::buffer(_data, MAX_LENGTH), *_sender_endpoint, boost::bind(&LNCF::handle_receive_from, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
	}

	void LNCF::parse_lncf_v1(size_t packet_size)
	{
		if ((_data[0] & 0b00000010) == 2) {
			//Encrypted message
			int16_t data_length = (((int16_t)_data[1]) & 0x00FF) << 8 | (((int16_t)_data[2]) & 0x00FF);

		}
		else if ((_data[0] & 0b00000001) == 1) {
			//Discovery message
		}
		else if ((_data[0] & 0b00011111) == 0) {
			//Data message
			int16_t topic_length = _data[1];
			std::string topic(_data + 2, (size_t)topic_length);
			int16_t data_length = (((int16_t)_data[2 + topic_length]) & 0x00FF) << 8 | (((int16_t)_data[2 + topic_length + 1]) & 0x00FF);
			std::string data(_data + 2 + topic_length + 2, data_length);
			std::unordered_map<std::string, LNCFHandler*>::const_iterator h = _handlers.find(topic);
			if (h != _handlers.end()) {
				h->second->Handle(topic, data);
			}

		}
	}
}

