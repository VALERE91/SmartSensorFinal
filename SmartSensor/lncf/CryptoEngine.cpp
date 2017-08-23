#include <cryptopp/crc.h>
#include <cryptopp/hmac.h>

#include "CryptoEngine.h"

namespace lncf {

	std::unordered_map<std::string, CryptoPP::SecByteBlock> CryptoEngine::_keys;

	std::string CryptoEngine::GenerateKeyFingerprint(CryptoPP::SecByteBlock key)
	{
		//Generate key fingerprint
		//SHA256
		byte digest[CryptoPP::SHA256::DIGESTSIZE];
		CryptoPP::SHA256().CalculateDigest(digest, key, key.size());

		//Base64 encoding
		std::string encoded;
		CryptoPP::Base64Encoder encoder;
		encoder.Put(digest, CryptoPP::SHA256::DIGESTSIZE);
		encoder.MessageEnd();
		CryptoPP::word64 size = encoder.MaxRetrievable();
		//Base64 conversion
		if (size)
		{
			encoded.resize((unsigned int)size);
			encoder.Get((byte*)encoded.data(), encoded.size());
		}

		return encoded;
	}

	std::string CryptoEngine::RegisterKey(unsigned char* key, std::size_t keyLength)
	{
		CryptoPP::SecByteBlock secKey(key, keyLength);

		std::string encoded = GenerateKeyFingerprint(secKey);

		//Storage
		_keys[encoded] = secKey;

		return encoded;
	}

	std::string CryptoEngine::GenerateKey()
	{
		//Generate key
		CryptoPP::AutoSeededRandomPool rnd;
		CryptoPP::SecByteBlock key(0x00, AES::DEFAULT_KEYLENGTH);
		rnd.GenerateBlock(key, key.size());

		std::string encoded = GenerateKeyFingerprint(key);

		//Storage
		_keys[encoded] = key;

		return encoded;
	}

	void CryptoEngine::UnregisterKey(std::string& keyFingerprint)
	{
		if (_keys.find(keyFingerprint) != _keys.end()) {
			_keys.erase(keyFingerprint);
		}
	}

	void CryptoEngine::ListKeys()
	{
		std::cout << "List of all registered keys" << std::endl;
		for (auto it = _keys.begin(); it != _keys.end(); ++it)
		{
			std::cout << "\t" << it->first << std::endl;
		}
	}

	std::tuple<byte*, size_t> CryptoEngine::AESEncrypt(byte* message, size_t message_size, std::string& keyFingerprint, unsigned char* iv)
	{
		if (_keys.find(keyFingerprint) == _keys.end()) {
			throw std::exception("Key unknown");
		}

		if (message_size % AES::BLOCKSIZE != 0) {
			throw std::exception("Invalid message length");
		}

		byte* output = new byte[message_size];
		CryptoPP::CBC_Mode<AES>::Encryption cbcEncryption(_keys[keyFingerprint], _keys[keyFingerprint].size(), iv);
		cbcEncryption.ProcessData(output, message, message_size);

		return std::make_tuple(output, message_size);
	}

	std::tuple<std::string, std::string> CryptoEngine::LNCFEncrypt(byte* packet, size_t packet_size)
	{
			
	}

	std::tuple<byte*, size_t> CryptoEngine::AESDecrypt(byte* message, size_t message_size, std::string& keyFingerprint, byte* iv)
	{
		if (_keys.find(keyFingerprint) == _keys.end()) {
			throw std::exception("Key unknown");
		}

		byte* output = new byte[message_size];
		CryptoPP::CBC_Mode<AES>::Decryption cbcDecryption(_keys[keyFingerprint], _keys[keyFingerprint].size(), iv);
		cbcDecryption.ProcessData(output, message, message_size);

		return std::make_tuple(output,message_size);
	}

	std::tuple<bool, byte*, size_t> CryptoEngine::LNCFDecrypt(byte* packet, size_t packet_size)
	{
		std::string keyFingerprint((char*)packet, KEY_FINGERPRINT_SIZE);
		try
		{
			if (VerifyHMAC256(keyFingerprint, packet, packet_size)) {
				byte* msgKey = new byte[MSG_KEY_SIZE];
				memcpy_s(msgKey, MSG_KEY_SIZE, packet + KEY_FINGERPRINT_SIZE, MSG_KEY_SIZE);
	
				byte* aesKey = new byte[KEY_LENGTH];
				byte* aesIV = new byte[CryptoPP::AES::BLOCKSIZE];
				std::tie(aesKey,aesIV) = LNCF_KDF(msgKey, MSG_KEY_SIZE, keyFingerprint);
	
				byte* clear;
				size_t msgSize;
				std::tie(clear,msgSize) = AESDecrypt(packet + KEY_FINGERPRINT_SIZE + MSG_KEY_SIZE, 
													packet_size - KEY_FINGERPRINT_SIZE - MSG_KEY_SIZE - CryptoPP::SHA256::DIGESTSIZE,
													keyFingerprint,
													aesIV);
	
				delete[] msgKey;
				delete[]aesKey;
				delete[]aesIV;

				if (msgSize > 3) {
					int16_t data_length = (((int16_t)clear[0]) & 0x00FF) << 8 | (((int16_t)clear[1]) & 0x00FF);
					return std::make_tuple(true, clear + 2 + (msgSize - data_length), data_length);
				}
			}
			else {
				return std::make_tuple(false, nullptr, 0);
			}
		}
		catch (std::exception e)
		{
			return std::make_tuple(false, nullptr, 0);
		}

		return std::make_tuple(false, nullptr, 0);
	}

	int32_t CryptoEngine::CRC32(byte* message, size_t message_size)
	{
		int32_t crc32_hash;
		CryptoPP::CRC32().CalculateDigest((byte*)&crc32_hash, message, message_size);
		return crc32_hash;
	}

	byte* CryptoEngine::SHA1(byte* message, size_t message_size)
	{
		byte* output = new byte[CryptoPP::SHA1::DIGESTSIZE];
		CryptoPP::SHA1().CalculateDigest(output, message, message_size);
		return output;
	}

	byte* CryptoEngine::SHA256(byte* message, size_t message_size)
	{
		byte* output = new byte[CryptoPP::SHA256::DIGESTSIZE];
		CryptoPP::SHA256().CalculateDigest(output, message, message_size);
		return output;
	}

	byte* CryptoEngine::HMAC256(std::string& keyFingerprint, byte* message, size_t message_size)
	{
		if (_keys.find(keyFingerprint) == _keys.end()) {
			throw std::exception("Key unknown");
		}

		byte* output = new byte[CryptoPP::SHA256::DIGESTSIZE];
		CryptoPP::HMAC<CryptoPP::SHA256> hmac(_keys[keyFingerprint], _keys[keyFingerprint].size());

		CryptoPP::ArraySource ss2(message,message_size, true,
			new CryptoPP::HashFilter(hmac,
				new CryptoPP::ArraySink(output, CryptoPP::SHA256::DIGESTSIZE)
			) // HashFilter      
		); // StringSource

		return output;
	}

	bool CryptoEngine::VerifyHMAC256(std::string& keyFingerprint, byte* message, size_t message_size)
	{
		if (_keys.find(keyFingerprint) == _keys.end()) {
			throw std::exception("Key unknown");
		}

		CryptoPP::HMAC<CryptoPP::SHA256> hmac(_keys[keyFingerprint], _keys[keyFingerprint].size());

		bool result = false;
		CryptoPP::ArraySource ss(message, message_size, true,
			new CryptoPP::HashVerificationFilter(hmac,
				new CryptoPP::ArraySink((byte*)&result, sizeof(result)),
				CryptoPP::HashVerificationFilter::PUT_RESULT | CryptoPP::HashVerificationFilter::HASH_AT_END
			) // HashVerificationFilter
		); // StringSource

		return result;
	}

	std::tuple<byte*, size_t> CryptoEngine::Base64Encrypt(byte* message, size_t message_size)
	{
		CryptoPP::Base64Encoder encoder;
		encoder.Put(message,message_size);
		encoder.MessageEnd();
		
		CryptoPP::lword size = encoder.MaxRetrievable();
		if (size)
		{
			byte* output = new byte[size];
			encoder.Get(output, size);
			return std::make_tuple(output, size);
		}
		return std::make_tuple(nullptr, -1);
	}

	std::tuple<byte*, size_t> CryptoEngine::Base64Decrypt(byte* message, size_t message_size)
	{
		CryptoPP::Base64Decoder decoder;
		decoder.Put(message, message_size);
		decoder.MessageEnd();
		
		CryptoPP::word64 size = decoder.MaxRetrievable();
		if (size)
		{
			byte* output = new byte[size];
			decoder.Get(output, size);
			return std::make_tuple(output, size);
		}
		return std::make_tuple(nullptr,-1);
	}

	std::tuple<byte*, byte*> CryptoEngine::LNCF_KDF(byte* messageKey, size_t messageKeySize, std::string& keyFingerprint)
	{

		return std::make_tuple(nullptr, nullptr);
	}
}