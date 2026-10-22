// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ChatMessageChannelTable.h"
#include "ArchivedChannelTable.h"
#include "ChatMessageTable.h"
#include "ServerTable.h"

#include "database/AccessException.h"
#include "database/Column.h"
#include "database/Constraint.h"
#include "database/DataType.h"
#include "database/ForeignKey.h"
#include "database/Index.h"
#include "database/PrimaryKey.h"
#include "database/TransactionHolder.h"
#include "database/Utils.h"

#include <soci/soci.h>

#include <cassert>
#include <chrono>
#include <exception>
#include <limits>
#include <string>
#include <vector>

namespace mdb = ::mumble::db;

namespace mumble {
namespace server {
	namespace db {

		constexpr const char *ChatMessageChannelTable::NAME;
		constexpr const char *ChatMessageChannelTable::column::message_id;
		constexpr const char *ChatMessageChannelTable::column::server_id;
		constexpr const char *ChatMessageChannelTable::column::channel_id;
		constexpr const char *ChatMessageChannelTable::column::archive_id;


		ChatMessageChannelTable::ChatMessageChannelTable(soci::session &sql, ::mdb::Backend backend,
														 const ServerTable &serverTable,
														 const ChatMessageTable &chatMessageTable,
														 const ArchivedChannelTable &archivedChannelTable)
			: ::mdb::Table(sql, backend, NAME) {
			::mdb::Column messageCol(column::message_id, ::mdb::DataType(::mdb::DataType::Integer));
			messageCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column serverCol(column::server_id, ::mdb::DataType(::mdb::DataType::Integer));
			serverCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column channelCol(column::channel_id, ::mdb::DataType(::mdb::DataType::Integer));
			channelCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column archiveCol(column::archive_id, ::mdb::DataType(::mdb::DataType::Integer));


			setColumns({ messageCol, serverCol, channelCol, archiveCol });

			::mdb::PrimaryKey pk(std::vector< std::string >{ column::message_id, column::channel_id });
			setPrimaryKey(pk);

			::mdb::ForeignKey serverFK(serverTable, { serverCol });
			addForeignKey(serverFK);

			::mdb::ForeignKey messageFK(chatMessageTable, { messageCol });
			addForeignKey(messageFK);

			::mdb::ForeignKey archiveFK(archivedChannelTable, { archiveCol });
			addForeignKey(archiveFK);


			::mdb::Index activeIndex(std::string(NAME) + "_active_idx",
									 { column::server_id, column::channel_id, column::archive_id, column::message_id });
			addIndex(activeIndex, false);

			::mdb::Index archiveIndex(std::string(NAME) + "_archive_idx", { column::archive_id, column::message_id });
			addIndex(archiveIndex, false);
		}

		void ChatMessageChannelTable::addMessageToChannel(unsigned int messageID, unsigned int serverID,
														  unsigned int channelID,
														  std::optional< unsigned int > archiveID) {
			try {
				unsigned int archiveIDValue = archiveID.value_or(0);
				soci::indicator archiveInd  = archiveID ? soci::i_ok : soci::i_null;

				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "INSERT INTO \"" << NAME << "\" (\"" << column::message_id << "\", \"" << column::server_id
					  << "\", \"" << column::channel_id << "\", \"" << column::archive_id
					  << "\") VALUES (:messageID, :serverID, :channelID, :archiveID)",
					soci::use(messageID), soci::use(serverID), soci::use(channelID),
					soci::use(archiveIDValue, archiveInd);

				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException(
					"Failed at adding chat message with ID " + std::to_string(messageID) + " to channel with ID "
					+ std::to_string(channelID) + " on server with ID " + std::to_string(serverID)));
			}
		}

		bool ChatMessageChannelTable::mappingExists(unsigned int messageID, unsigned int channelID) {
			try {
				long long count = 0;

				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "SELECT COUNT(*) FROM \"" << NAME << "\" WHERE \"" << column::message_id
					  << "\" = :messageID AND \"" << column::channel_id << "\" = :channelID",
					soci::into(count), soci::use(messageID), soci::use(channelID);
				::mdb::utils::verifyQueryResultedInData(m_sql);

				transaction.commit();

				return count > 0;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException(
					"Failed at checking whether chat message with ID " + std::to_string(messageID)
					+ " is mapped to channel with ID " + std::to_string(channelID)));
			}
		}

		void ChatMessageChannelTable::archiveChannel(unsigned int serverID, unsigned int channelID,
													 unsigned int archiveID) {
			try {
				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "UPDATE \"" << NAME << "\" SET \"" << column::archive_id << "\" = :archiveID WHERE \""
					  << column::server_id << "\" = :serverID AND \"" << column::channel_id << "\" = :channelID AND \""
					  << column::archive_id << "\" IS NULL",
					soci::use(archiveID), soci::use(serverID), soci::use(channelID);

				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at archiving chat messages of channel with ID "
															  + std::to_string(channelID) + " on server with ID "
															  + std::to_string(serverID) + " as archive with ID "
															  + std::to_string(archiveID)));
			}
		}

		std::vector< unsigned int >
			ChatMessageChannelTable::getMessageIDsForChannel(unsigned int serverID, unsigned int channelID,
															 std::optional< unsigned int > archiveID,
															 unsigned int limit) {
			assert(limit <= static_cast< unsigned int >(std::numeric_limits< int >::max()));

			try {
				std::vector< unsigned int > messageIDs;

				::mdb::TransactionHolder transaction = ensureTransaction();

				unsigned int messageID = 0;
				const int queryLimit   = static_cast< int >(limit);

				if (archiveID) {
					std::string query = "SELECT \"" + std::string(column::message_id) + "\" FROM \"" + NAME
										+ "\" WHERE \"" + column::server_id + "\" = :serverID AND \""
										+ column::channel_id + "\" = :channelID AND \"" + column::archive_id
										+ "\" = :archiveID ORDER BY \"" + column::message_id + "\" DESC "
										+ ::mdb::utils::limitOffset(m_backend, ":limit");

					soci::statement stmt = (m_sql.prepare << query, soci::use(serverID), soci::use(channelID),
											soci::use(*archiveID), soci::use(queryLimit), soci::into(messageID));

					stmt.execute(false);
					while (stmt.fetch()) {
						messageIDs.push_back(messageID);
					}
				} else {
					std::string query = "SELECT \"" + std::string(column::message_id) + "\" FROM \"" + NAME
										+ "\" WHERE \"" + column::server_id + "\" = :serverID AND \""
										+ column::channel_id + "\" = :channelID AND \"" + column::archive_id
										+ "\" IS NULL ORDER BY \"" + column::message_id + "\" DESC "
										+ ::mdb::utils::limitOffset(m_backend, ":limit");

					soci::statement stmt = (m_sql.prepare << query, soci::use(serverID), soci::use(channelID),
											soci::use(queryLimit), soci::into(messageID));

					stmt.execute(false);
					while (stmt.fetch()) {
						messageIDs.push_back(messageID);
					}
				}

				transaction.commit();

				return messageIDs;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at fetching chat message IDs for channel with ID "
															  + std::to_string(channelID) + " on server with ID "
															  + std::to_string(serverID)));
			}
		}

		std::vector< DBChatMessage >
			ChatMessageChannelTable::getMessagesForChannel(unsigned int serverID, unsigned int channelID,
														   std::optional< unsigned int > archiveID,
														   unsigned int beforeID, unsigned int limit) {
			assert(limit <= static_cast< unsigned int >(std::numeric_limits< int >::max()));

			try {
				std::vector< DBChatMessage > messages;

				const int queryLimit = static_cast< int >(limit);
				const long long before =
					beforeID == 0 ? std::numeric_limits< long long >::max() : static_cast< long long >(beforeID);

				// Filter and order on the mapping's message_id so the covering index can be scanned in
				// reverse without a temporary sort; the join guarantees it equals m.id.
				std::string query =
					"SELECT m.\"" + std::string(ChatMessageTable::column::id) + "\", m.\""
					+ ChatMessageTable::column::server_id + "\", m.\"" + ChatMessageTable::column::sender_id
					+ "\", m.\"" + ChatMessageTable::column::sender_hash + "\", m.\""
					+ ChatMessageTable::column::sender_name + "\", m.\"" + ChatMessageTable::column::message
					+ "\", m.\"" + ChatMessageTable::column::created_at + "\" FROM \"" + NAME + "\" c JOIN \""
					+ ChatMessageTable::NAME + "\" m ON m.\"" + ChatMessageTable::column::id + "\" = c.\""
					+ column::message_id + "\" WHERE c.\"" + column::server_id + "\" = :serverID AND c.\""
					+ column::channel_id + "\" = :channelID AND c.\"" + column::archive_id + "\" ";
				query += archiveID ? "= :archiveID" : "IS NULL";
				query += std::string(" AND c.\"") + column::message_id + "\" < :beforeID ORDER BY c.\""
						 + column::message_id + "\" DESC " + ::mdb::utils::limitOffset(m_backend, ":limit");

				::mdb::TransactionHolder transaction = ensureTransaction();

				DBChatMessage row;
				unsigned int senderID     = 0;
				soci::indicator senderInd = soci::i_null;
				std::string senderHash;
				soci::indicator senderHashInd = soci::i_null;
				long long createdAt           = 0;

				soci::statement stmt(m_sql);
				if (archiveID) {
					stmt = (m_sql.prepare << query, soci::use(serverID), soci::use(channelID), soci::use(*archiveID),
							soci::use(before), soci::use(queryLimit), soci::into(row.id), soci::into(row.serverID),
							soci::into(senderID, senderInd), soci::into(senderHash, senderHashInd),
							soci::into(row.senderName), soci::into(row.message), soci::into(createdAt));
				} else {
					stmt = (m_sql.prepare << query, soci::use(serverID), soci::use(channelID), soci::use(before),
							soci::use(queryLimit), soci::into(row.id), soci::into(row.serverID),
							soci::into(senderID, senderInd), soci::into(senderHash, senderHashInd),
							soci::into(row.senderName), soci::into(row.message), soci::into(createdAt));
				}

				stmt.execute(false);
				while (stmt.fetch()) {
					row.senderID.reset();
					row.senderHash.reset();

					if (senderInd == soci::i_ok) {
						row.senderID = senderID;
					}
					if (senderHashInd == soci::i_ok) {
						row.senderHash = senderHash;
					}

					row.createdAt = DBChatMessage::timestamp_type(std::chrono::seconds(createdAt));
					messages.push_back(row);
				}

				transaction.commit();

				return messages;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at fetching chat messages for channel with ID "
															  + std::to_string(channelID) + " on server with ID "
															  + std::to_string(serverID)));
			}
		}

		std::vector< DBChatMessage > ChatMessageChannelTable::getMessagesForArchive(unsigned int archiveID,
																					unsigned int beforeID,
																					unsigned int limit) {
			assert(limit <= static_cast< unsigned int >(std::numeric_limits< int >::max()));

			try {
				std::vector< DBChatMessage > messages;

				const int queryLimit = static_cast< int >(limit);
				const long long before =
					beforeID == 0 ? std::numeric_limits< long long >::max() : static_cast< long long >(beforeID);

				std::string query =
					"SELECT m.\"" + std::string(ChatMessageTable::column::id) + "\", m.\""
					+ ChatMessageTable::column::server_id + "\", m.\"" + ChatMessageTable::column::sender_id
					+ "\", m.\"" + ChatMessageTable::column::sender_hash + "\", m.\""
					+ ChatMessageTable::column::sender_name + "\", m.\"" + ChatMessageTable::column::message
					+ "\", m.\"" + ChatMessageTable::column::created_at + "\" FROM \"" + NAME + "\" c JOIN \""
					+ ChatMessageTable::NAME + "\" m ON m.\"" + ChatMessageTable::column::id + "\" = c.\""
					+ column::message_id + "\" WHERE c.\"" + column::archive_id + "\" = :archiveID AND c.\""
					+ column::message_id + "\" < :beforeID ORDER BY c.\"" + column::message_id + "\" DESC "
					+ ::mdb::utils::limitOffset(m_backend, ":limit");

				::mdb::TransactionHolder transaction = ensureTransaction();

				DBChatMessage row;
				unsigned int senderID     = 0;
				soci::indicator senderInd = soci::i_null;
				std::string senderHash;
				soci::indicator senderHashInd = soci::i_null;
				long long createdAt           = 0;

				soci::statement stmt = (m_sql.prepare << query, soci::use(archiveID), soci::use(before),
										soci::use(queryLimit), soci::into(row.id), soci::into(row.serverID),
										soci::into(senderID, senderInd), soci::into(senderHash, senderHashInd),
										soci::into(row.senderName), soci::into(row.message), soci::into(createdAt));

				stmt.execute(false);
				while (stmt.fetch()) {
					row.senderID.reset();
					row.senderHash.reset();

					if (senderInd == soci::i_ok) {
						row.senderID = senderID;
					}
					if (senderHashInd == soci::i_ok) {
						row.senderHash = senderHash;
					}

					row.createdAt = DBChatMessage::timestamp_type(std::chrono::seconds(createdAt));
					messages.push_back(row);
				}

				transaction.commit();

				return messages;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at fetching chat messages of archive with ID "
															  + std::to_string(archiveID)));
			}
		}

		void ChatMessageChannelTable::removeMappingsForChannel(unsigned int serverID, unsigned int channelID) {
			try {
				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "DELETE FROM \"" << NAME << "\" WHERE \"" << column::server_id << "\" = :serverID AND \""
					  << column::channel_id << "\" = :channelID",
					soci::use(serverID), soci::use(channelID);

				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException(
					"Failed at removing chat message mappings of channel with ID " + std::to_string(channelID)
					+ " on server with ID " + std::to_string(serverID)));
			}
		}

		void ChatMessageChannelTable::removeActiveMappingsForChannel(unsigned int serverID, unsigned int channelID) {
			try {
				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "DELETE FROM \"" << NAME << "\" WHERE \"" << column::server_id << "\" = :serverID AND \""
					  << column::channel_id << "\" = :channelID AND \"" << column::archive_id << "\" IS NULL",
					soci::use(serverID), soci::use(channelID);

				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException(
					"Failed at removing active chat message mappings of channel with ID " + std::to_string(channelID)
					+ " on server with ID " + std::to_string(serverID)));
			}
		}

		void ChatMessageChannelTable::removeMappingsForArchive(unsigned int archiveID) {
			try {
				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "DELETE FROM \"" << NAME << "\" WHERE \"" << column::archive_id << "\" = :archiveID",
					soci::use(archiveID);

				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at removing chat message mappings of archive "
															  "with ID "
															  + std::to_string(archiveID)));
			}
		}

		void ChatMessageChannelTable::removeMappingsForServer(unsigned int serverID) {
			try {
				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "DELETE FROM \"" << NAME << "\" WHERE \"" << column::server_id << "\" = :serverID",
					soci::use(serverID);

				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException(
					"Failed at removing chat message mappings of server with ID " + std::to_string(serverID)));
			}
		}

		void ChatMessageChannelTable::migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) {
			// This table was introduced in v12. There is no old table to import data from.
			(void) fromSchemaVersion;
			(void) toSchemaVersion;
		}

	} // namespace db
} // namespace server
} // namespace mumble
