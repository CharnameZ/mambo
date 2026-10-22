// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_ARCHIVEDCHANNELTABLE_H_
#define MUMBLE_SERVER_DATABASE_ARCHIVEDCHANNELTABLE_H_

#include "database/Backend.h"
#include "database/Table.h"

#include "DBArchivedChannel.h"

#include <vector>

namespace soci {
class session;
}

namespace mumble {
namespace server {
	namespace db {

		class ServerTable;

		/**
		 * Table storing information about channels whose history has been archived
		 */
		class ArchivedChannelTable : public ::mumble::db::Table {
		public:
			static constexpr const char *NAME = "archived_channels";

			struct column {
				column()                                  = delete;
				static constexpr const char *archive_id   = "archive_id";
				static constexpr const char *server_id    = "server_id";
				static constexpr const char *channel_id   = "channel_id";
				static constexpr const char *channel_name = "channel_name";
				static constexpr const char *archived_at  = "archived_at";
				static constexpr const char *archived_by  = "archived_by";
			};

			ArchivedChannelTable(soci::session &sql, ::mumble::db::Backend backend, const ServerTable &serverTable);
			~ArchivedChannelTable() = default;

			/**
			 * Stores the given archive entry and returns the ID assigned to it
			 */
			unsigned int addArchive(const DBArchivedChannel &archive);

			DBArchivedChannel getArchive(unsigned int archiveID);

			bool archiveExists(unsigned int archiveID);

			std::vector< DBArchivedChannel > getArchivesForServer(unsigned int serverID);

			void removeArchive(unsigned int archiveID);

			void removeArchivesForServer(unsigned int serverID);

			void migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) override;
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_ARCHIVEDCHANNELTABLE_H_
