// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_DBCHATMESSAGE_H_
#define MUMBLE_SERVER_DATABASE_DBCHATMESSAGE_H_

#include <chrono>
#include <optional>
#include <string>

namespace mumble {
namespace server {
	namespace db {

		/**
		 * A struct representing what the database needs to know / knows about a chat message
		 */
		struct DBChatMessage {
			using timestamp_type = std::chrono::time_point< std::chrono::system_clock >;

			unsigned int id       = 0;
			unsigned int serverID = 0;
			std::optional< unsigned int > senderID;
			std::optional< std::string > senderHash;
			std::string senderName;
			std::string message;
			timestamp_type createdAt = std::chrono::system_clock::now();
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_DBCHATMESSAGE_H_
