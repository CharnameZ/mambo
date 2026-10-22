// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ChatMessageTable.h"
#include "ChatMessageChannelTable.h"
#include "ChronoUtils.h"
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
#include <string>
#include <utility>

namespace mdb = ::mumble::db;

namespace mumble {
namespace server {
	namespace db {

		constexpr const char *ChatMessageTable::NAME;
		constexpr const char *ChatMessageTable::column::id;
		constexpr const char *ChatMessageTable::column::server_id;
		constexpr const char *ChatMessageTable::column::sender_id;
		constexpr const char *ChatMessageTable::column::sender_hash;
		constexpr const char *ChatMessageTable::column::sender_name;
		constexpr const char *ChatMessageTable::column::message;
		constexpr const char *ChatMessageTable::column::created_at;


		ChatMessageTable::ChatMessageTable(soci::session &sql, ::mdb::Backend backend, const ServerTable &serverTable)
			: ::mdb::Table(sql, backend, NAME) {
			::mdb::Column idCol(column::id, ::mdb::DataType(::mdb::DataType::Integer),
								{ ::mdb::Constraint(::mdb::Constraint::NotNull) }, ::mdb::Column::Flag::AUTOINCREMENT);

			::mdb::Column serverCol(column::server_id, ::mdb::DataType(::mdb::DataType::Integer));
			serverCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column senderIDCol(column::sender_id, ::mdb::DataType(::mdb::DataType::Integer));
			::mdb::Column senderHashCol(column::sender_hash, ::mdb::DataType(::mdb::DataType::VarChar, 40));

			::mdb::Column senderNameCol(column::sender_name, ::mdb::DataType(::mdb::DataType::VarChar, 255));
			senderNameCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column messageCol(column::message, ::mdb::DataType(::mdb::DataType::Text));
			messageCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column createdAtCol(column::created_at, ::mdb::DataType(::mdb::DataType::EpochTime));
			createdAtCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));


			setColumns({ idCol, serverCol, senderIDCol, senderHashCol, senderNameCol, messageCol, createdAtCol });

			::mdb::PrimaryKey pk(idCol);
			setPrimaryKey(pk);

			::mdb::ForeignKey serverFK(serverTable, { serverCol });
			addForeignKey(serverFK);

			::mdb::Index serverIDIndex(std::string(NAME) + "_server_id_id_idx", { column::server_id, column::id });
			addIndex(serverIDIndex, false);

			::mdb::Index createdAtIndex(std::string(NAME) + "_server_id_created_at_idx",
										{ column::server_id, column::created_at });
			addIndex(createdAtIndex, false);
		}

		unsigned int ChatMessageTable::addMessage(const DBChatMessage &message) {
			try {
				unsigned int senderID     = 0;
				soci::indicator senderInd = soci::i_null;
				if (message.senderID) {
					senderID  = message.senderID.value();
					senderInd = soci::i_ok;
				}

				std::string senderHash;
				soci::indicator senderHashInd = soci::i_null;
				if (message.senderHash) {
					senderHash    = message.senderHash.value();
					senderHashInd = soci::i_ok;
				}

				std::size_t createdAt = toEpochSeconds(message.createdAt);

				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "INSERT INTO \"" << NAME << "\" (\"" << column::server_id << "\", \"" << column::sender_id
					  << "\", \"" << column::sender_hash << "\", \"" << column::sender_name << "\", \""
					  << column::message << "\", \"" << column::created_at
					  << "\") VALUES (:serverID, :senderID, :senderHash, :senderName, :message, :createdAt)",
					soci::use(message.serverID), soci::use(senderID, senderInd), soci::use(senderHash, senderHashInd),
					soci::use(message.senderName), soci::use(message.message), soci::use(createdAt);

				long long lastID = 0;
				m_sql << "SELECT MAX(\"" << column::id << "\") FROM \"" << NAME << "\"", soci::into(lastID);
				::mdb::utils::verifyQueryResultedInData(m_sql);

				transaction.commit();

				assert(lastID > 0);
				return static_cast< unsigned int >(lastID);
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at adding chat message for server with ID "
															  + std::to_string(message.serverID)));
			}
		}

		DBChatMessage ChatMessageTable::getMessage(unsigned int id) {
			try {
				DBChatMessage message;

				unsigned int senderID = 0;
				soci::indicator senderInd;
				std::string senderHash;
				soci::indicator senderHashInd;
				long long createdAt = 0;

				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "SELECT \"" << column::id << "\", \"" << column::server_id << "\", \"" << column::sender_id
					  << "\", \"" << column::sender_hash << "\", \"" << column::sender_name << "\", \""
					  << column::message << "\", \"" << column::created_at << "\" FROM \"" << NAME << "\" WHERE \""
					  << column::id << "\" = :id",
					soci::into(message.id), soci::into(message.serverID), soci::into(senderID, senderInd),
					soci::into(senderHash, senderHashInd), soci::into(message.senderName), soci::into(message.message),
					soci::into(createdAt), soci::use(id);

				::mdb::utils::verifyQueryResultedInData(m_sql);

				if (senderInd == soci::i_ok) {
					message.senderID = senderID;
				}
				if (senderHashInd == soci::i_ok) {
					message.senderHash = std::move(senderHash);
				}

				message.createdAt = DBChatMessage::timestamp_type(std::chrono::seconds(createdAt));

				transaction.commit();

				return message;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(
					::mdb::AccessException("Failed at fetching chat message with ID " + std::to_string(id)));
			}
		}

		bool ChatMessageTable::messageExists(unsigned int id) {
			try {
				long long count = 0;

				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "SELECT COUNT(*) FROM \"" << NAME << "\" WHERE \"" << column::id << "\" = :id",
					soci::into(count), soci::use(id);
				::mdb::utils::verifyQueryResultedInData(m_sql);

				transaction.commit();

				return count > 0;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at checking whether chat message with ID "
															  + std::to_string(id) + " exists"));
			}
		}

		void ChatMessageTable::removeOrphanMessages(unsigned int serverID) {
			try {
				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "DELETE FROM \"" << NAME << "\" WHERE \"" << column::server_id
					  << "\" = :serverID AND NOT EXISTS ("
						 "SELECT 1 FROM \""
					  << ChatMessageChannelTable::NAME << "\" c WHERE c.\""
					  << ChatMessageChannelTable::column::message_id << "\" = \"" << NAME << "\".\"" << column::id
					  << "\")",
					soci::use(serverID);

				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException(
					"Failed at removing orphaned chat messages of server with ID " + std::to_string(serverID)));
			}
		}

		void ChatMessageTable::removeMessagesForServer(unsigned int serverID) {
			try {
				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "DELETE FROM \"" << NAME << "\" WHERE \"" << column::server_id << "\" = :serverID",
					soci::use(serverID);

				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at removing chat messages of server with ID "
															  + std::to_string(serverID)));
			}
		}

		void ChatMessageTable::migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) {
			// This table was introduced in v12. There is no old table to import data from.
			(void) fromSchemaVersion;
			(void) toSchemaVersion;
		}

	} // namespace db
} // namespace server
} // namespace mumble
