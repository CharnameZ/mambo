// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_DBARCHIVEDCHANNEL_H_
#define MUMBLE_SERVER_DATABASE_DBARCHIVEDCHANNEL_H_

#include <chrono>
#include <optional>
#include <string>

namespace mumble {
namespace server {
	namespace db {

		/**
		 * A struct representing what the database needs to know / knows about an archived channel
		 */
		struct DBArchivedChannel {
			using timestamp_type = std::chrono::time_point< std::chrono::system_clock >;

			std::optional< unsigned int > archiveID;
			unsigned int serverID  = 0;
			unsigned int channelID = 0;
			std::string channelName;
			timestamp_type archivedAt = std::chrono::system_clock::now();
			std::optional< std::string > archivedBy;
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_DBARCHIVEDCHANNEL_H_
