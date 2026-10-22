// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "Mumble.pb.h"
#include "MumbleProtocol.h"
#include "MumbleUDP.pb.h"
#include "Version.h"

#include <QObject>
#include <QtTest>

#include <cstdint>
#include <cstring>
#include <span>
#include <sstream>
#include <string>

namespace Mumble {
namespace Protocol {
	// Add toString functions for use by QCOMPARE

	char *toString(const Mumble::Protocol::AudioData &data) {
		std::stringstream stream;
		stream << "{ payload: {" << static_cast< const void * >(data.payload.data()) << ", " << data.payload.size()
			   << "}, frameNumber: " << data.frameNumber << ", isLastFrame: " << data.isLastFrame
			   << ", senderSession: " << data.senderSession << ", targetOrContext: " << data.targetOrContext
			   << ", usedCodec: " << static_cast< int >(data.usedCodec)
			   << ", containsPositionalData: " << data.containsPositionalData;
		if (data.containsPositionalData) {
			stream << ", position: {";

			for (unsigned int i = 0; i < data.position.size(); ++i) {
				stream << data.position[i];

				if (i + 1 < data.position.size()) {
					stream << ", ";
				}
			}

			stream << "}";
		}
		stream << ", volumeAdjustment: " << data.volumeAdjustment.factor << " }";

		std::string str = stream.str();

		char *charArray = new char[str.size() + 1];

		std::strcpy(charArray, str.c_str());

		return charArray;
	}

	char *toString(const Mumble::Protocol::PingData &data) {
		std::stringstream stream;

		stream << "{ timestamp: " << data.timestamp
			   << ", requestAdditionalInformation: " << data.requestAdditionalInformation
			   << ", containsAdditionalInformation: " << data.containsAdditionalInformation
			   << ", userCount: " << data.userCount << ", maxUserCount: " << data.maxUserCount
			   << ", maxBandwidthPerUser: " << data.maxBandwidthPerUser << " }";

		std::string str = stream.str();

		char *charArray = new char[str.size() + 1];

		std::strcpy(charArray, str.c_str());

		return charArray;
	}


	template< Role role > class TestAudioEncoder : public UDPAudioEncoder< role > {
	public:
		using UDPAudioEncoder< role >::UDPAudioEncoder;

		// Expose these functions publicly for testing-purposes
		using UDPAudioEncoder< role >::getPreEncodedContext;
		using UDPAudioEncoder< role >::getPreEncodedVolumeAdjustment;
	};

} // namespace Protocol
} // namespace Mumble

template< Mumble::Protocol::Role encoderRole, Mumble::Protocol::Role decoderRole > void do_test_ping() {
	Mumble::Protocol::UDPPingEncoder< encoderRole > encoder;
	Mumble::Protocol::UDPDecoder< decoderRole > decoder;

	for (Version::full_t version :
		 { Version::fromComponents(1, 3, 0), Mumble::Protocol::PROTOBUF_INTRODUCTION_VERSION }) {
		qWarning("Using protocol version %s", Version::toString(Version::get()).toStdString().c_str());

		// Note: When the decoder is set to a version < PROTOBUF_INTRODUCTION_VERSION, it can decode pings in
		// either format
		encoder.setProtocolVersion(version);

		Mumble::Protocol::PingData data;
		data.timestamp = 42;

		// Regular connectivity ping
		auto encodedData = encoder.encodePingPacket(data);
		QVERIFY(decoder.decode(encodedData));

		QCOMPARE(decoder.getMessageType(), Mumble::Protocol::UDPMessageType::Ping);
		QCOMPARE(decoder.getPingData(), data);

		// Extended ping (request)
#ifdef _MSVC_LANG
#	pragma warning(push)
		// Disable warning about this if condition being constant
		// TODO: Use if constexpr as soon as we have moved to C++17 (or higher)
#	pragma warning(disable : 4127)
#endif
		if (decoderRole == Mumble::Protocol::Role::Server) {
#ifdef _MSVC_LANG
#	pragma warning(pop)
#endif
			QVERIFY(encoderRole == Mumble::Protocol::Role::Client);

			data.requestAdditionalInformation = true;

			encodedData = encoder.encodePingPacket(data);
			QVERIFY(decoder.decode(encodedData));

			QCOMPARE(decoder.getMessageType(), Mumble::Protocol::UDPMessageType::Ping);
			QCOMPARE(decoder.getPingData(), data);
		} else {
			QVERIFY(encoderRole == Mumble::Protocol::Role::Server);

			data.containsAdditionalInformation = true;
			data.userCount                     = 12;
			data.maxUserCount                  = 42;
			data.maxBandwidthPerUser           = 512;

			encodedData = encoder.encodePingPacket(data);
			QVERIFY(decoder.decode(encodedData));

			QCOMPARE(decoder.getMessageType(), Mumble::Protocol::UDPMessageType::Ping);
		}
	}
}

void printData(std::span< const Mumble::Protocol::byte > data) {
	QString str = "Data is: { ";
	for (Mumble::Protocol::byte current : data) {
		str += QString::number(static_cast< int >(current)) + " ";
	}
	str += " }";

	qDebug() << str;
}

template< Mumble::Protocol::Role encoderRole, Mumble::Protocol::Role decoderRole > void do_test_audio() {
	Mumble::Protocol::UDPAudioEncoder< encoderRole > encoder;
	Mumble::Protocol::UDPDecoder< decoderRole > decoder;

	std::string payloadData = "I am the payload";

	for (Version::full_t version :
		 { Version::fromComponents(1, 3, 0), Mumble::Protocol::PROTOBUF_INTRODUCTION_VERSION }) {
		Version::component_t major, minor, patch;
		Version::getComponents(major, minor, patch, version);
		qWarning("Using protocol version %d.%d.%d", major, minor, patch);

		encoder.setProtocolVersion(version);
		decoder.setProtocolVersion(version);

		Mumble::Protocol::AudioData data;
		data.payload = { reinterpret_cast< const Mumble::Protocol::byte * >(payloadData.c_str()), payloadData.size() };

		data.frameNumber            = 12;
		data.containsPositionalData = true;
		data.position               = { 3, 2, 1 };
		data.isLastFrame            = true;
		data.usedCodec              = Mumble::Protocol::AudioCodec::Opus;
		if (version >= Mumble::Protocol::PROTOBUF_INTRODUCTION_VERSION
			&& decoderRole == Mumble::Protocol::Role::Client) {
			// Transmitting volume adjustment is only supported in the new packet format
			// and only in the server->client direction
			data.volumeAdjustment = VolumeAdjustment::fromFactor(1.4f);
		}

#ifdef _MSVC_LANG
#	pragma warning(push)
		// Disable warning about this if condition being constant
		// TODO: Use if constexpr as soon as we have moved to C++17 (or higher)
#	pragma warning(disable : 4127)
#endif
		if (decoderRole == Mumble::Protocol::Role::Client) {
#ifdef _MSVC_LANG
#	pragma warning(pop)
#endif
			QVERIFY(encoder.getRole() == Mumble::Protocol::Role::Server);

			data.targetOrContext = Mumble::Protocol::AudioContext::SHOUT;
			data.senderSession   = 42;
		} else {
			QVERIFY(encoder.getRole() == Mumble::Protocol::Role::Client);

			data.targetOrContext = Mumble::Protocol::ReservedTargetIDs::SERVER_LOOPBACK;
		}

		auto encodedData = encoder.encodeAudioPacket(data);
		QVERIFY(!encodedData.empty());

		QVERIFY(decoder.decode(encodedData));

		QCOMPARE(decoder.getMessageType(), Mumble::Protocol::UDPMessageType::Audio);
		QCOMPARE(decoder.getAudioData(), data);

		qWarning() << "Partial re-encoding";

		// Re-encode fields from the "variable" part
		data.targetOrContext = Mumble::Protocol::AudioContext::LISTEN;
		if (version >= Mumble::Protocol::PROTOBUF_INTRODUCTION_VERSION
			&& decoderRole == Mumble::Protocol::Role::Client) {
			data.volumeAdjustment = VolumeAdjustment::fromFactor(0.9f);
		}

		encodedData = encoder.updateAudioPacket(data);
		QVERIFY(!encodedData.empty());

		QVERIFY(decoder.decode(encodedData));

		QCOMPARE(decoder.getMessageType(), Mumble::Protocol::UDPMessageType::Audio);
		QCOMPARE(decoder.getAudioData(), data);

		qWarning() << "Removing positional data";
		// Update the audio packet to no longer contain positional data.
		data.containsPositionalData = false;
		data.targetOrContext        = Mumble::Protocol::AudioContext::NORMAL;

		encoder.dropPositionalData();

		encodedData = encoder.updateAudioPacket(data);
		QVERIFY(!encodedData.empty());

		QVERIFY(decoder.decode(encodedData));

		QCOMPARE(decoder.getMessageType(), Mumble::Protocol::UDPMessageType::Audio);
		QCOMPARE(decoder.getAudioData(), data);
	}
}

class TestMumbleProtocol : public QObject {
	Q_OBJECT
private slots:
	void test_ping_client_to_server() {
		do_test_ping< Mumble::Protocol::Role::Client, Mumble::Protocol::Role::Server >();
	}

	void test_ping_server_to_client() {
		do_test_ping< Mumble::Protocol::Role::Server, Mumble::Protocol::Role::Client >();
	}

	void test_audio_client_to_server() {
		do_test_audio< Mumble::Protocol::Role::Client, Mumble::Protocol::Role::Server >();
	}

	void test_audio_server_to_client() {
		do_test_audio< Mumble::Protocol::Role::Server, Mumble::Protocol::Role::Client >();
	}

	void test_chatHistoryMessages() {
		QCOMPARE(static_cast< std::uint64_t >(Mumble::Protocol::Feature::ChatHistory), std::uint64_t{ 1 });
		QCOMPARE(Mumble::Protocol::FEATURE_PROTOCOL_VERSION, 1U);

		QCOMPARE(static_cast< Mumble::Protocol::byte >(Mumble::Protocol::TCPMessageType::ClientFeatures), 27);
		QCOMPARE(static_cast< Mumble::Protocol::byte >(Mumble::Protocol::TCPMessageType::ServerFeatures), 28);
		QCOMPARE(static_cast< Mumble::Protocol::byte >(Mumble::Protocol::TCPMessageType::ChatHistoryPolicy), 29);
		QCOMPARE(static_cast< Mumble::Protocol::byte >(Mumble::Protocol::TCPMessageType::ChatHistoryConsent), 30);
		QCOMPARE(static_cast< Mumble::Protocol::byte >(Mumble::Protocol::TCPMessageType::ChatHistoryRequest), 31);
		QCOMPARE(static_cast< Mumble::Protocol::byte >(Mumble::Protocol::TCPMessageType::ChatHistoryResponse), 32);

		MumbleProto::ClientFeatures clientFeatures;
		clientFeatures.set_protocol_version(Mumble::Protocol::FEATURE_PROTOCOL_VERSION);
		clientFeatures.set_supported_features(static_cast< std::uint64_t >(Mumble::Protocol::Feature::ChatHistory));
		clientFeatures.set_client_version(Version::get());
		clientFeatures.set_client_release("test");

		std::string serialized;
		QVERIFY(clientFeatures.SerializeToString(&serialized));

		MumbleProto::ClientFeatures parsedClientFeatures;
		QVERIFY(parsedClientFeatures.ParseFromString(serialized));
		QCOMPARE(parsedClientFeatures.protocol_version(), Mumble::Protocol::FEATURE_PROTOCOL_VERSION);
		QCOMPARE(parsedClientFeatures.supported_features(),
				 static_cast< std::uint64_t >(Mumble::Protocol::Feature::ChatHistory));
		QCOMPARE(parsedClientFeatures.client_version(), Version::get());
		QCOMPARE(parsedClientFeatures.client_release(), std::string("test"));

		// Defaults must match the values documented in the design
		MumbleProto::ChatHistoryPolicy defaultPolicy;
		QCOMPARE(defaultPolicy.enabled(), false);
		QCOMPARE(defaultPolicy.consent_required(), true);
		QCOMPARE(defaultPolicy.required_for_connection(), false);
		QCOMPARE(defaultPolicy.policy_version(), 1U);
		QCOMPARE(defaultPolicy.max_messages(), 50U);
		QCOMPARE(defaultPolicy.retention_days(), 0U);

		MumbleProto::ChatHistoryRequest defaultRequest;
		QCOMPARE(defaultRequest.channel_id(), 0U);
		QCOMPARE(defaultRequest.before_id(), std::uint64_t{ 0 });
		QCOMPARE(defaultRequest.limit(), 0U);

		MumbleProto::ChatHistoryResponse response;
		response.set_channel_id(42);
		response.set_result(MumbleProto::CHAT_HISTORY_OK);
		response.set_has_more(true);

		MumbleProto::ChatHistoryEntry *entry = response.add_messages();
		entry->set_id(1234);
		entry->set_sender_id(56);
		entry->set_sender_name("sender");
		entry->set_timestamp(1700000000);
		entry->set_message("hello");

		serialized.clear();
		QVERIFY(response.SerializeToString(&serialized));

		MumbleProto::ChatHistoryResponse parsedResponse;
		QVERIFY(parsedResponse.ParseFromString(serialized));
		QCOMPARE(parsedResponse.channel_id(), 42U);
		QCOMPARE(parsedResponse.result(), MumbleProto::CHAT_HISTORY_OK);
		QCOMPARE(parsedResponse.has_more(), true);
		QCOMPARE(parsedResponse.messages_size(), 1);
		QCOMPARE(parsedResponse.messages(0).id(), std::uint64_t{ 1234 });
		QCOMPARE(parsedResponse.messages(0).sender_id(), 56U);
		QCOMPARE(parsedResponse.messages(0).sender_name(), std::string("sender"));
		QCOMPARE(parsedResponse.messages(0).timestamp(), std::uint64_t{ 1700000000 });
		QCOMPARE(parsedResponse.messages(0).message(), std::string("hello"));

		// The real-time message carries the database ID assigned by the server
		MumbleProto::TextMessage textMessage;
		textMessage.set_message("hello");
		textMessage.set_message_id(1234);

		serialized.clear();
		QVERIFY(textMessage.SerializeToString(&serialized));

		MumbleProto::TextMessage parsedTextMessage;
		QVERIFY(parsedTextMessage.ParseFromString(serialized));
		QCOMPARE(parsedTextMessage.message_id(), std::uint64_t{ 1234 });
	}

	void test_preEncode_audio_context() {
		Mumble::Protocol::TestAudioEncoder< Mumble::Protocol::Role::Server > encoder;

		MumbleUDP::Audio msg;
		std::vector< Mumble::Protocol::byte > buffer;

		for (Mumble::Protocol::audio_context_t currentContext = Mumble::Protocol::AudioContext::BEGIN;
			 currentContext < Mumble::Protocol::AudioContext::END; currentContext++) {
			std::span< const Mumble::Protocol::byte > snippet = encoder.getPreEncodedContext(currentContext);

			QVERIFY2(!snippet.empty(), "Unable to find pre-encoded snippet for audio context");

			msg.set_context(currentContext);

#if GOOGLE_PROTOBUF_VERSION >= 3002000
			// ByteSizeLong() was introduced in Protobuf v3.2 as a replacement for ByteSize()
			buffer.resize(msg.ByteSizeLong());
#else
			buffer.resize(msg.ByteSize());
#endif
			const auto end = msg.SerializeWithCachedSizesToArray(buffer.data());
			QCOMPARE(end, buffer.data() + buffer.size());

			QCOMPARE(snippet.size(), buffer.size());
			QVERIFY2(std::equal(snippet.begin(), snippet.end(), buffer.begin()), "Pre-encoded snippet is incorrect");
		}

		// Ensure that an unknown context yields an empty span
		QVERIFY(encoder.getPreEncodedContext(Mumble::Protocol::AudioContext::END).empty());
	}

	void test_preEncode_volume_adjustments() {
		Mumble::Protocol::TestAudioEncoder< Mumble::Protocol::Role::Server > encoder;

		MumbleUDP::Audio msg;

		constexpr int MIN = -60;
		constexpr int MAX = 30;

		for (int currentAdjustment = MIN; currentAdjustment <= MAX; ++currentAdjustment) {
			msg.Clear();

			std::span< const Mumble::Protocol::byte > snippet =
				encoder.getPreEncodedVolumeAdjustment(VolumeAdjustment::fromDBAdjustment(currentAdjustment));

			QVERIFY2(!snippet.empty(), "Unable to find pre-encoded snippet for volume adjustment");

			QVERIFY(msg.ParseFromArray(snippet.data(), static_cast< int >(snippet.size())));

			// This will perform a fuzzy-compare
			QCOMPARE(msg.volume_adjustment(), std::pow(2.0f, static_cast< float >(currentAdjustment) / 6.0f));
		}

		// Ensure that an unknown/unexpected volume adjustment yields an empty span
		QVERIFY(encoder.getPreEncodedVolumeAdjustment(VolumeAdjustment::fromDBAdjustment(MIN - 1)).empty());
		// We only expect pre-encoded values for integer dB adjustments
		QVERIFY(encoder.getPreEncodedVolumeAdjustment(VolumeAdjustment(std::pow(2.0f, (MAX + 0.5f) / 6.0f))).empty());
	}
};

QTEST_MAIN(TestMumbleProtocol)
#include "TestMumbleProtocol.moc"
