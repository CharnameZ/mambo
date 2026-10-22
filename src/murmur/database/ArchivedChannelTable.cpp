// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ArchivedChannelTable.h"
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

		constexpr const char *ArchivedChannelTable::NAME;
		constexpr const char *ArchivedChannelTable::column::archive_id;
		constexpr const char *ArchivedChannelTable::column::server_id;
		constexpr const char *ArchivedChannelTable::column::channel_id;
		constexpr const char *ArchivedChannelTable::column::channel_name;
		constexpr const char *ArchivedChannelTable::column::archived_at;
		constexpr const char *ArchivedChannelTable::column::archived_by;


		ArchivedChannelTable::ArchivedChannelTable(soci::session &sql, ::mdb::Backend backend,
												   const ServerTable &serverTable)
			: ::mdb::Table(sql, backend, NAME) {
			::mdb::Column archiveIDCol(column::archive_id, ::mdb::DataType(::mdb::DataType::Integer),
									   { ::mdb::Constraint(::mdb::Constraint::NotNull) },
									   ::mdb::Column::Flag::AUTOINCREMENT);

			::mdb::Column serverCol(column::server_id, ::mdb::DataType(::mdb::DataType::Integer));
			serverCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column channelCol(column::channel_id, ::mdb::DataType(::mdb::DataType::Integer));
			channelCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column channelNameCol(column::channel_name, ::mdb::DataType(::mdb::DataType::VarChar, 255));
			channelNameCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column archivedAtCol(column::archived_at, ::mdb::DataType(::mdb::DataType::EpochTime));
			archivedAtCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column archivedByCol(column::archived_by, ::mdb::DataType(::mdb::DataType::VarChar, 255));


			setColumns({ archiveIDCol, serverCol, channelCol, channelNameCol, archivedAtCol, archivedByCol });

			::mdb::PrimaryKey pk(archiveIDCol);
			setPrimaryKey(pk);

			::mdb::ForeignKey serverFK(serverTable, { serverCol });
			addForeignKey(serverFK);

			::mdb::Index archivedAtServerIndex(std::string(NAME) + "_server_id_archived_at_idx",
											   { column::server_id, column::archived_at });
			addIndex(archivedAtServerIndex, false);
		}

		unsigned int ArchivedChannelTable::addArchive(const DBArchivedChannel &archive) {
			try {
				std::string archivedBy;
				soci::indicator archivedByInd = soci::i_null;
				if (archive.archivedBy) {
					archivedBy    = archive.archivedBy.value();
					archivedByInd = soci::i_ok;
				}

				std::size_t archivedAt = toEpochSeconds(archive.archivedAt);

				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "INSERT INTO \"" << NAME << "\" (\"" << column::server_id << "\", \"" << column::channel_id
					  << "\", \"" << column::channel_name << "\", \"" << column::archived_at << "\", \""
					  << column::archived_by
					  << "\") VALUES (:serverID, :channelID, :channelName, :archivedAt, :archivedBy)",
					soci::use(archive.serverID), soci::use(archive.channelID), soci::use(archive.channelName),
					soci::use(archivedAt), soci::use(archivedBy, archivedByInd);

				long long lastID = 0;
				m_sql << "SELECT MAX(\"" << column::archive_id << "\") FROM \"" << NAME << "\"", soci::into(lastID);
				::mdb::utils::verifyQueryResultedInData(m_sql);

				transaction.commit();

				assert(lastID > 0);
				return static_cast< unsigned int >(lastID);
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException(
					"Failed at adding archive for channel with ID " + std::to_string(archive.channelID)
					+ " on server with ID " + std::to_string(archive.serverID)));
			}
		}

		DBArchivedChannel ArchivedChannelTable::getArchive(unsigned int archiveID) {
			try {
				DBArchivedChannel archive;

				unsigned int fetchedArchiveID = 0;
				std::string archivedBy;
				soci::indicator archivedByInd;
				long long archivedAt = 0;

				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "SELECT \"" << column::archive_id << "\", \"" << column::server_id << "\", \""
					  << column::channel_id << "\", \"" << column::channel_name << "\", \"" << column::archived_at
					  << "\", \"" << column::archived_by << "\" FROM \"" << NAME << "\" WHERE \"" << column::archive_id
					  << "\" = :archiveID",
					soci::into(fetchedArchiveID), soci::into(archive.serverID), soci::into(archive.channelID),
					soci::into(archive.channelName), soci::into(archivedAt), soci::into(archivedBy, archivedByInd),
					soci::use(archiveID);

				::mdb::utils::verifyQueryResultedInData(m_sql);

				archive.archiveID = fetchedArchiveID;

				if (archivedByInd == soci::i_ok) {
					archive.archivedBy = std::move(archivedBy);
				}

				archive.archivedAt = DBArchivedChannel::timestamp_type(std::chrono::seconds(archivedAt));

				transaction.commit();

				return archive;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(
					::mdb::AccessException("Failed at fetching archive with ID " + std::to_string(archiveID)));
			}
		}

		bool ArchivedChannelTable::archiveExists(unsigned int archiveID) {
			try {
				long long count = 0;

				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "SELECT COUNT(*) FROM \"" << NAME << "\" WHERE \"" << column::archive_id << "\" = :archiveID",
					soci::into(count), soci::use(archiveID);
				::mdb::utils::verifyQueryResultedInData(m_sql);

				transaction.commit();

				return count > 0;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at checking whether archive with ID "
															  + std::to_string(archiveID) + " exists"));
			}
		}

		std::vector< DBArchivedChannel > ArchivedChannelTable::getArchivesForServer(unsigned int serverID) {
			try {
				std::vector< DBArchivedChannel > archives;

				::mdb::TransactionHolder transaction = ensureTransaction();

				std::string query = "SELECT \"" + std::string(column::archive_id) + "\", \"" + column::server_id
									+ "\", \"" + column::channel_id + "\", \"" + column::channel_name + "\", \""
									+ column::archived_at + "\", \"" + column::archived_by + "\" FROM \"" + NAME
									+ "\" WHERE \"" + column::server_id + "\" = :serverID ORDER BY \""
									+ column::archive_id + "\" ASC";

				DBArchivedChannel archive;
				unsigned int archiveID = 0;
				std::string archivedBy;
				soci::indicator archivedByInd = soci::i_null;
				long long archivedAt          = 0;

				soci::statement stmt =
					(m_sql.prepare << query, soci::use(serverID), soci::into(archiveID), soci::into(archive.serverID),
					 soci::into(archive.channelID), soci::into(archive.channelName), soci::into(archivedAt),
					 soci::into(archivedBy, archivedByInd));

				stmt.execute(false);
				while (stmt.fetch()) {
					archive.archiveID = archiveID;
					archive.archivedBy.reset();

					if (archivedByInd == soci::i_ok) {
						archive.archivedBy = archivedBy;
					}

					archive.archivedAt = DBArchivedChannel::timestamp_type(std::chrono::seconds(archivedAt));
					archives.push_back(archive);
				}

				transaction.commit();

				return archives;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at fetching archives of server with ID "
															  + std::to_string(serverID)));
			}
		}

		void ArchivedChannelTable::removeArchive(unsigned int archiveID) {
			try {
				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "DELETE FROM \"" << NAME << "\" WHERE \"" << column::archive_id << "\" = :archiveID",
					soci::use(archiveID);

				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(
					::mdb::AccessException("Failed at removing archive with ID " + std::to_string(archiveID)));
			}
		}

		void ArchivedChannelTable::removeArchivesForServer(unsigned int serverID) {
			try {
				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "DELETE FROM \"" << NAME << "\" WHERE \"" << column::server_id << "\" = :serverID",
					soci::use(serverID);

				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at removing archives of server with ID "
															  + std::to_string(serverID)));
			}
		}

		void ArchivedChannelTable::migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) {
			// This table was introduced in v12. There is no old table to import data from.
			(void) fromSchemaVersion;
			(void) toSchemaVersion;
		}

	} // namespace db
} // namespace server
} // namespace mumble
