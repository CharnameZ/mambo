// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_CHATMESSAGETABLE_H_
#define MUMBLE_SERVER_DATABASE_CHATMESSAGETABLE_H_

#include "database/Backend.h"
#include "database/Table.h"

#include "DBChatMessage.h"

namespace soci {
class session;
}

namespace mumble {
namespace server {
	namespace db {

		class ServerTable;

		/**
		 * Table storing the actual chat message payloads
		 */
		class ChatMessageTable : public ::mumble::db::Table {
		public:
			static constexpr const char *NAME = "chat_messages";

			struct column {
				column()                                 = delete;
				static constexpr const char *id          = "id";
				static constexpr const char *server_id   = "server_id";
				static constexpr const char *sender_id   = "sender_id";
				static constexpr const char *sender_hash = "sender_hash";
				static constexpr const char *sender_name = "sender_name";
				static constexpr const char *message     = "message";
				static constexpr const char *created_at  = "created_at";
			};

			ChatMessageTable(soci::session &sql, ::mumble::db::Backend backend, const ServerTable &serverTable);
			~ChatMessageTable() = default;

			/**
			 * Stores the given message and returns the ID assigned to it
			 */
			unsigned int addMessage(const DBChatMessage &message);

			DBChatMessage getMessage(unsigned int id);

			bool messageExists(unsigned int id);

			/**
			 * Removes all messages of the given server that are no longer referenced by any channel mapping.
			 */
			void removeOrphanMessages(unsigned int serverID);

			void removeMessagesForServer(unsigned int serverID);

			void migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) override;
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_CHATMESSAGETABLE_H_
