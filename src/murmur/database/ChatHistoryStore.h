// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_CHATHISTORYSTORE_H_
#define MUMBLE_SERVER_DATABASE_CHATHISTORYSTORE_H_

#include "DBArchivedChannel.h"
#include "DBChatMessage.h"

#include <string>
#include <vector>

namespace mumble {
namespace server {
	namespace db {

		class ServerDatabase;

		struct ChatHistoryPage {
			std::vector< DBChatMessage > messages;
			bool hasMore = false;
		};

		/**
		 * High-level access to the chat history tables. It reuses the connection owned by the given ServerDatabase
		 * and must therefore be used from the thread that owns that database.
		 */
		class ChatHistoryStore {
		public:
			explicit ChatHistoryStore(ServerDatabase &database);

			/**
			 * Stores the given message and maps it to all given channels in a single transaction.
			 *
			 * @returns The ID assigned to the message
			 */
			unsigned int storeMessage(const DBChatMessage &message, const std::vector< unsigned int > &channelIDs);

			/**
			 * Fetches the active history of the given channel, ordered by ascending message ID.
			 *
			 * @param beforeID Only messages with an ID smaller than this are considered. 0 means no restriction.
			 */
			ChatHistoryPage getHistory(unsigned int serverID, unsigned int channelID, unsigned int beforeID,
									   unsigned int limit);

			/**
			 * Fetches the history of the given archive, ordered by ascending message ID.
			 */
			ChatHistoryPage getArchivedHistory(unsigned int archiveID, unsigned int beforeID, unsigned int limit);

			/**
			 * Moves the active history of the given channels into new archives and deletes their channel rows in a
			 * single transaction. The channels must be ordered such that every channel appears after all of its
			 * children so that the self-referencing foreign key of the channel table is satisfied.
			 *
			 * @returns The IDs of the created archives in the order of the given channels
			 */
			std::vector< unsigned int > archiveAndDeleteChannelTree(unsigned int serverID,
																	const std::vector< DBArchivedChannel > &channels);

			/**
			 * Moves the active history of the given channel into a new archive and deletes the channel row in a single
			 * transaction.
			 *
			 * @returns The ID of the created archive
			 */
			unsigned int archiveAndDeleteChannel(unsigned int serverID, unsigned int channelID,
												 const std::string &channelName, const std::string &archivedBy);

			/**
			 * Removes the active history of the given channel and deletes messages that are no longer referenced.
			 */
			void clearChannelHistory(unsigned int serverID, unsigned int channelID);

			/**
			 * Removes all history and archives of the given server.
			 */
			void clearServerHistory(unsigned int serverID);

			/**
			 * Removes the given archive including all of its message mappings.
			 */
			void deleteArchive(unsigned int archiveID);

			std::vector< DBArchivedChannel > getArchivedChannels(unsigned int serverID);

		private:
			ServerDatabase &m_db;
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_CHATHISTORYSTORE_H_
