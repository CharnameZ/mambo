// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_CHATMESSAGECHANNELTABLE_H_
#define MUMBLE_SERVER_DATABASE_CHATMESSAGECHANNELTABLE_H_

#include "database/Backend.h"
#include "database/Table.h"

#include "DBChatMessage.h"

#include <optional>
#include <vector>

namespace soci {
class session;
}

namespace mumble {
namespace server {
	namespace db {

		class ServerTable;
		class ChatMessageTable;
		class ArchivedChannelTable;

		/**
		 * Table mapping chat messages to the channels they were sent to
		 */
		class ChatMessageChannelTable : public ::mumble::db::Table {
		public:
			static constexpr const char *NAME = "chat_message_channels";

			struct column {
				column()                                = delete;
				static constexpr const char *message_id = "message_id";
				static constexpr const char *server_id  = "server_id";
				static constexpr const char *channel_id = "channel_id";
				static constexpr const char *archive_id = "archive_id";
			};

			ChatMessageChannelTable(soci::session &sql, ::mumble::db::Backend backend, const ServerTable &serverTable,
									const ChatMessageTable &chatMessageTable,
									const ArchivedChannelTable &archivedChannelTable);
			~ChatMessageChannelTable() = default;

			void addMessageToChannel(unsigned int messageID, unsigned int serverID, unsigned int channelID,
									 std::optional< unsigned int > archiveID = {});

			bool mappingExists(unsigned int messageID, unsigned int channelID);

			void archiveChannel(unsigned int serverID, unsigned int channelID, unsigned int archiveID);

			std::vector< unsigned int > getMessageIDsForChannel(unsigned int serverID, unsigned int channelID,
																std::optional< unsigned int > archiveID = {},
																unsigned int limit                      = 50);

			/**
			 * Fetches the messages mapped to the given channel, newest first.
			 *
			 * @param archiveID If set, only messages archived under this archive are returned. Otherwise only
			 * active messages (archive_id IS NULL) are considered.
			 * @param beforeID If non-zero, only messages with an ID smaller than this are returned.
			 */
			std::vector< DBChatMessage > getMessagesForChannel(unsigned int serverID, unsigned int channelID,
															   std::optional< unsigned int > archiveID,
															   unsigned int beforeID, unsigned int limit);

			/**
			 * Fetches the messages belonging to the given archive, newest first.
			 */
			std::vector< DBChatMessage > getMessagesForArchive(unsigned int archiveID, unsigned int beforeID,
															   unsigned int limit);

			void removeMappingsForChannel(unsigned int serverID, unsigned int channelID);
			void removeActiveMappingsForChannel(unsigned int serverID, unsigned int channelID);
			void removeMappingsForArchive(unsigned int archiveID);
			void removeMappingsForServer(unsigned int serverID);

			void migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) override;
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_CHATMESSAGECHANNELTABLE_H_
