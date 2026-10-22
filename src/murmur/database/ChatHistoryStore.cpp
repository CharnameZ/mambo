// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ChatHistoryStore.h"
#include "ArchivedChannelTable.h"
#include "ChannelTable.h"
#include "ChatMessageChannelTable.h"
#include "ChatMessageTable.h"
#include "ServerDatabase.h"

#include "database/TransactionHolder.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <optional>
#include <unordered_set>

namespace mdb = ::mumble::db;

namespace mumble {
namespace server {
	namespace db {

		ChatHistoryStore::ChatHistoryStore(ServerDatabase &database) : m_db(database) {}

		unsigned int ChatHistoryStore::storeMessage(const DBChatMessage &message,
													const std::vector< unsigned int > &channelIDs) {
			assert(!channelIDs.empty());

			::mdb::TransactionHolder transaction = m_db.ensureTransaction();

			unsigned int messageID = m_db.getChatMessageTable().addMessage(message);

			std::unordered_set< unsigned int > seenChannels;
			for (unsigned int channelID : channelIDs) {
				if (!seenChannels.insert(channelID).second) {
					continue;
				}

				m_db.getChatMessageChannelTable().addMessageToChannel(messageID, message.serverID, channelID,
																	  std::nullopt);
			}

			transaction.commit();

			return messageID;
		}

		ChatHistoryPage ChatHistoryStore::getHistory(unsigned int serverID, unsigned int channelID,
													 unsigned int beforeID, unsigned int limit) {
			assert(limit > 0);

			std::vector< DBChatMessage > messages = m_db.getChatMessageChannelTable().getMessagesForChannel(
				serverID, channelID, std::nullopt, beforeID, limit + 1);

			ChatHistoryPage page;
			page.hasMore = messages.size() > limit;

			if (page.hasMore) {
				messages.resize(limit);
			}

			std::reverse(messages.begin(), messages.end());
			page.messages = std::move(messages);

			return page;
		}

		ChatHistoryPage ChatHistoryStore::getArchivedHistory(unsigned int archiveID, unsigned int beforeID,
															 unsigned int limit) {
			assert(limit > 0);

			std::vector< DBChatMessage > messages =
				m_db.getChatMessageChannelTable().getMessagesForArchive(archiveID, beforeID, limit + 1);

			ChatHistoryPage page;
			page.hasMore = messages.size() > limit;

			if (page.hasMore) {
				messages.resize(limit);
			}

			std::reverse(messages.begin(), messages.end());
			page.messages = std::move(messages);

			return page;
		}

		std::vector< unsigned int >
			ChatHistoryStore::archiveAndDeleteChannelTree(unsigned int serverID,
														  const std::vector< DBArchivedChannel > &channels) {
			std::vector< unsigned int > archiveIDs;

			if (channels.empty()) {
				return archiveIDs;
			}

			::mdb::TransactionHolder transaction = m_db.ensureTransaction();
			archiveIDs.reserve(channels.size());

			for (const DBArchivedChannel &channel : channels) {
				DBArchivedChannel archive = channel;
				archive.archiveID.reset();
				archive.serverID = serverID;

				const unsigned int archiveID = m_db.getArchivedChannelTable().addArchive(archive);

				m_db.getChatMessageChannelTable().archiveChannel(serverID, channel.channelID, archiveID);

				// Delete the channel row so that the archive and the channel removal are atomic
				m_db.getChannelTable().removeChannel(serverID, channel.channelID);

				archiveIDs.push_back(archiveID);
			}

			transaction.commit();

			return archiveIDs;
		}

		unsigned int ChatHistoryStore::archiveAndDeleteChannel(unsigned int serverID, unsigned int channelID,
															   const std::string &channelName,
															   const std::string &archivedBy) {
			DBArchivedChannel channel;
			channel.serverID    = serverID;
			channel.channelID   = channelID;
			channel.channelName = channelName;
			channel.archivedAt  = DBArchivedChannel::timestamp_type(std::chrono::system_clock::now());
			channel.archivedBy  = archivedBy;

			return archiveAndDeleteChannelTree(serverID, { channel }).front();
		}

		void ChatHistoryStore::clearChannelHistory(unsigned int serverID, unsigned int channelID) {
			::mdb::TransactionHolder transaction = m_db.ensureTransaction();

			m_db.getChatMessageChannelTable().removeActiveMappingsForChannel(serverID, channelID);
			m_db.getChatMessageTable().removeOrphanMessages(serverID);

			transaction.commit();
		}

		void ChatHistoryStore::clearServerHistory(unsigned int serverID) {
			::mdb::TransactionHolder transaction = m_db.ensureTransaction();

			m_db.getChatMessageChannelTable().removeMappingsForServer(serverID);
			m_db.getArchivedChannelTable().removeArchivesForServer(serverID);
			m_db.getChatMessageTable().removeMessagesForServer(serverID);

			transaction.commit();
		}

		void ChatHistoryStore::deleteArchive(unsigned int archiveID) {
			::mdb::TransactionHolder transaction = m_db.ensureTransaction();

			const DBArchivedChannel archive = m_db.getArchivedChannelTable().getArchive(archiveID);

			m_db.getChatMessageChannelTable().removeMappingsForArchive(archiveID);
			m_db.getArchivedChannelTable().removeArchive(archiveID);
			m_db.getChatMessageTable().removeOrphanMessages(archive.serverID);

			transaction.commit();
		}

		std::vector< DBArchivedChannel > ChatHistoryStore::getArchivedChannels(unsigned int serverID) {
			return m_db.getArchivedChannelTable().getArchivesForServer(serverID);
		}

	} // namespace db
} // namespace server
} // namespace mumble
