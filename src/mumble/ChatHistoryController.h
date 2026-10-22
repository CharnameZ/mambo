// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_CHATHISTORYCONTROLLER_H_
#define MUMBLE_MUMBLE_CHATHISTORYCONTROLLER_H_

#include "Log.h"
#include "Mumble.pb.h"

#include <QtCore/QByteArray>
#include <QtCore/QDateTime>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QSet>
#include <QtCore/QVector>

#include <cstdint>

class Channel;
class MainWindow;
class QTimer;

/// Handles the client side of the chat history feature: consent, pagination and display.
class ChatHistoryController : public QObject {
	Q_OBJECT
	Q_DISABLE_COPY(ChatHistoryController)

public:
	explicit ChatHistoryController(MainWindow *parent);

	void handleServerFeatures(const MumbleProto::ServerFeatures &msg);
	void handlePolicy(const MumbleProto::ChatHistoryPolicy &policy);
	void handleResponse(const MumbleProto::ChatHistoryResponse &response);
	void onServerSynchronized();
	void onSelfChannelChanged(Channel *channel);

	/// @returns Whether a message with the given ID has already been added to the log
	bool isKnownMessage(std::uint64_t messageID) const;
	/// Records the ID of a live message so that it isn't displayed a second time when it shows up in history
	void noteRealtimeMessage(std::uint64_t messageID);

public slots:
	void loadEarlier();

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	struct DisplayEntry {
		std::uint64_t id;
		QDateTime timestamp;
		QString console;
	};

	void resetSession();
	void resetChannel();
	void startForCurrentChannel();
	void requestPage(bool older);
	void applyPage(const MumbleProto::ChatHistoryResponse &response, bool older);
	void askForConsent();
	void finishConsent(bool accepted);
	void sendConsent(bool accepted);
	void disableHistory();
	void updateLoadEarlierButton();
	void repositionLoadEarlierButton();
	QByteArray serverDigest() const;
	bool isIgnored(const MumbleProto::ChatHistoryEntry &entry) const;

	MainWindow *m_mw;
	QTimer *m_policyTimer;
	QTimer *m_requestTimer;

	std::uint64_t m_serverFeatures = 0;
	MumbleProto::ChatHistoryPolicy m_policy;
	bool m_policyReceived = false;
	bool m_historyEnabled = false;
	bool m_consentPending = false;

	QPointer< Channel > m_channel;
	unsigned int m_channelID          = 0;
	std::uint64_t m_generation        = 0;
	std::uint64_t m_requestGeneration = 0;
	std::uint64_t m_oldestID          = 0;
	bool m_hasMore                    = false;
	bool m_loading                    = false;
	bool m_requestOlder               = false;
	int m_retries                     = 0;

	QSet< std::uint64_t > m_knownIDs;
	QVector< DisplayEntry > m_displayEntries;
};

#endif
