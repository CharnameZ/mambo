// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ChatHistoryController.h"

#include "Channel.h"
#include "ClientUser.h"
#include "Database.h"
#include "MainWindow.h"
#include "MumbleProtocol.h"
#include "QtUtils.h"
#include "ServerHandler.h"
#include "Global.h"

#include <QtCore/QEvent>
#include <QtCore/QReadLocker>
#include <QtCore/QTimer>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QToolButton>

namespace {
constexpr std::uint64_t chatHistoryFeature = static_cast< std::uint64_t >(Mumble::Protocol::Feature::ChatHistory);

constexpr int policyTimeoutMs  = 5000;
constexpr int requestTimeoutMs = 15000;
constexpr int maxRetries       = 3;
} // namespace

ChatHistoryController::ChatHistoryController(MainWindow *parent) : QObject(parent), m_mw(parent) {
	m_policyTimer = new QTimer(this);
	m_policyTimer->setSingleShot(true);
	connect(m_policyTimer, &QTimer::timeout, this, [this]() {
		if (m_policyReceived) {
			return;
		}

		updateLoadEarlierButton();
		Global::get().l->log(Log::Warning,
							 tr("The server announced chat history support but did not send a chat history policy."));
	});

	m_requestTimer = new QTimer(this);
	m_requestTimer->setSingleShot(true);
	connect(m_requestTimer, &QTimer::timeout, this, [this]() {
		if (!m_loading) {
			return;
		}

		// The server may respond with a plain PermissionDenied instead of a ChatHistoryResponse
		m_loading = false;
		updateLoadEarlierButton();
		Global::get().l->log(Log::Warning, tr("Failed to load chat history: the server did not respond."));
	});

	if (m_mw->m_qbLoadEarlier && m_mw->m_qbLoadEarlier->parentWidget()) {
		m_mw->m_qbLoadEarlier->parentWidget()->installEventFilter(this);
	}
}

bool ChatHistoryController::eventFilter(QObject *watched, QEvent *event) {
	if (event->type() == QEvent::Resize && m_mw->m_qbLoadEarlier && watched == m_mw->m_qbLoadEarlier->parentWidget()) {
		repositionLoadEarlierButton();
	}

	return QObject::eventFilter(watched, event);
}

void ChatHistoryController::handleServerFeatures(const MumbleProto::ServerFeatures &msg) {
	m_serverFeatures = msg.enabled_features();
}

void ChatHistoryController::onServerSynchronized() {
	// ServerFeatures is sent before authentication, so keep the value learned during this connection
	const std::uint64_t features = m_serverFeatures;

	resetSession();

	m_serverFeatures = features;

	if (m_serverFeatures & chatHistoryFeature) {
		m_policyTimer->start(policyTimeoutMs);
	}
}

void ChatHistoryController::resetSession() {
	m_policyTimer->stop();
	m_requestTimer->stop();

	m_serverFeatures = 0;
	m_policyReceived = false;
	m_historyEnabled = false;
	m_consentPending = false;
	m_policy.Clear();

	resetChannel();
	Global::get().l->clearChatHistory();
	updateLoadEarlierButton();
}

void ChatHistoryController::resetChannel() {
	++m_generation;

	m_requestTimer->stop();

	m_channel           = nullptr;
	m_channelID         = 0;
	m_requestGeneration = 0;
	m_oldestID          = 0;
	m_hasMore           = false;
	m_loading           = false;
	m_requestOlder      = false;
	m_retries           = 0;

	m_knownIDs.clear();
	m_displayEntries.clear();
}

void ChatHistoryController::handlePolicy(const MumbleProto::ChatHistoryPolicy &policy) {
	m_policyTimer->stop();

	if (m_consentPending) {
		// A dialog is already open; don't stack another one
		return;
	}

	m_policyReceived = true;
	m_policy         = policy;

	resetChannel();
	Global::get().l->clearChatHistory();
	m_historyEnabled = false;
	updateLoadEarlierButton();

	if (!policy.enabled()) {
		return;
	}

	if (!policy.consent_required()) {
		m_historyEnabled = true;
		startForCurrentChannel();
		return;
	}

	const QByteArray digest = serverDigest();
	const bool cached = !digest.isEmpty() && Global::get().db->hasChatHistoryConsent(digest, policy.policy_version());

	if (cached) {
		sendConsent(true);
		m_historyEnabled = true;
		startForCurrentChannel();
		return;
	}

	m_consentPending = true;
	askForConsent();
}

void ChatHistoryController::askForConsent() {
	QMessageBox box(m_mw);
	box.setWindowTitle(tr("Chat History"));
	box.setIcon(QMessageBox::Information);
	box.setTextFormat(Qt::PlainText);

	QString text = tr("This server stores text messages sent to channels so that they can be shown again later. "
					  "Private messages are not stored. Only users with the \"Read text history\" permission can "
					  "view the stored messages.");

	if (m_policy.retention_days() > 0) {
		text += QLatin1String("\n\n")
				+ tr("Messages are retained for %n day(s).", "", static_cast< int >(m_policy.retention_days()));
	} else {
		text += QLatin1String("\n\n") + tr("Messages are retained indefinitely.");
	}

	const QString notice = u8(m_policy.notice());
	if (!notice.isEmpty()) {
		text += QLatin1String("\n\n") + notice;
	}

	if (m_policy.required_for_connection()) {
		text +=
			QLatin1String("\n\n") + tr("This server requires you to accept this policy in order to stay connected.");
	}

	box.setText(text);

	QPushButton *acceptButton = box.addButton(tr("Accept and continue"), QMessageBox::AcceptRole);
	QPushButton *declineButton =
		box.addButton(m_policy.required_for_connection() ? tr("Disconnect") : tr("Continue without history"),
					  QMessageBox::RejectRole);
	box.setDefaultButton(declineButton);

	box.exec();

	const bool accepted = box.clickedButton() == acceptButton;
	m_consentPending    = false;
	finishConsent(accepted);
}

void ChatHistoryController::finishConsent(bool accepted) {
	sendConsent(accepted);

	if (!accepted) {
		m_historyEnabled = false;
		updateLoadEarlierButton();
		Global::get().l->log(Log::Information, tr("Chat history is disabled for this session."));
		return;
	}

	const QByteArray digest = serverDigest();
	if (!digest.isEmpty()) {
		Global::get().db->setChatHistoryConsent(digest, m_policy.policy_version());
	}

	m_historyEnabled = true;
	startForCurrentChannel();
}

void ChatHistoryController::sendConsent(bool accepted) {
	ServerHandlerPtr sh = Global::get().sh;
	if (!sh || !sh->isConnected()) {
		return;
	}

	MumbleProto::ChatHistoryConsent msg;
	msg.set_policy_version(m_policy.policy_version());
	msg.set_accepted(accepted);
	sh->sendMessage(msg);
}

QByteArray ChatHistoryController::serverDigest() const {
	ServerHandlerPtr sh = Global::get().sh;
	return sh ? sh->qbaDigest : QByteArray();
}

void ChatHistoryController::startForCurrentChannel() {
	if (!m_historyEnabled) {
		updateLoadEarlierButton();
		return;
	}

	ClientUser *self = ClientUser::get(Global::get().uiSession);
	if (self && self->cChannel) {
		onSelfChannelChanged(self->cChannel);
	} else {
		updateLoadEarlierButton();
	}
}

void ChatHistoryController::onSelfChannelChanged(Channel *channel) {
	if (!channel || channel == m_channel) {
		return;
	}

	resetChannel();
	Global::get().l->clearChatHistory();

	m_channel   = channel;
	m_channelID = channel->iId;

	if (m_historyEnabled) {
		requestPage(false);
	}

	updateLoadEarlierButton();
}

bool ChatHistoryController::isKnownMessage(std::uint64_t messageID) const {
	return messageID != 0 && m_knownIDs.contains(messageID);
}

void ChatHistoryController::noteRealtimeMessage(std::uint64_t messageID) {
	if (messageID != 0) {
		m_knownIDs.insert(messageID);
	}
}

void ChatHistoryController::loadEarlier() {
	if (!m_historyEnabled || !m_hasMore || m_loading) {
		return;
	}

	requestPage(true);
}

void ChatHistoryController::requestPage(bool older) {
	if (!m_historyEnabled || !m_channel || m_loading) {
		return;
	}

	if (older && (!m_hasMore || m_oldestID == 0)) {
		return;
	}

	ServerHandlerPtr sh = Global::get().sh;
	if (!sh || !sh->isConnected()) {
		return;
	}

	MumbleProto::ChatHistoryRequest msg;
	msg.set_channel_id(m_channelID);
	if (older) {
		msg.set_before_id(m_oldestID);
	}
	msg.set_limit(m_policy.max_messages());

	m_loading           = true;
	m_requestOlder      = older;
	m_requestGeneration = m_generation;

	sh->sendMessage(msg);
	m_requestTimer->start(requestTimeoutMs);
	updateLoadEarlierButton();
}

void ChatHistoryController::handleResponse(const MumbleProto::ChatHistoryResponse &response) {
	if (response.channel_id() != m_channelID || m_requestGeneration != m_generation) {
		// Response for a channel we are no longer displaying
		return;
	}

	m_requestTimer->stop();
	m_loading = false;

	switch (response.result()) {
		case MumbleProto::CHAT_HISTORY_OK:
			applyPage(response, m_requestOlder);
			break;
		case MumbleProto::CHAT_HISTORY_DISABLED:
		case MumbleProto::CHAT_HISTORY_CONSENT_REQUIRED:
			disableHistory();
			break;
		case MumbleProto::CHAT_HISTORY_INVALID_CHANNEL:
			m_hasMore = false;
			break;
		case MumbleProto::CHAT_HISTORY_RATE_LIMITED:
			if (m_retries < maxRetries) {
				++m_retries;

				const std::uint64_t generation = m_generation;
				const bool older               = m_requestOlder;
				QTimer::singleShot(3000 * m_retries, this, [this, generation, older]() {
					if (generation == m_generation) {
						requestPage(older);
					}
				});
			} else {
				Global::get().l->log(Log::Warning, tr("Failed to load chat history: request rate limited."));
			}
			break;
		case MumbleProto::CHAT_HISTORY_INTERNAL_ERROR:
			Global::get().l->log(Log::Warning, tr("Failed to load chat history: internal server error."));
			break;
		default:
			break;
	}

	updateLoadEarlierButton();
}

void ChatHistoryController::applyPage(const MumbleProto::ChatHistoryResponse &response, bool older) {
	QVector< DisplayEntry > newEntries;
	newEntries.reserve(response.messages_size());

	for (int i = 0; i < response.messages_size(); ++i) {
		const MumbleProto::ChatHistoryEntry &entry = response.messages(i);
		const std::uint64_t id                     = entry.id();

		if (id == 0) {
			continue;
		}

		if (m_oldestID == 0 || id < m_oldestID) {
			m_oldestID = id;
		}

		if (m_knownIDs.contains(id)) {
			continue;
		}
		m_knownIDs.insert(id);

		if (isIgnored(entry)) {
			continue;
		}

		QString senderName = u8(entry.sender_name()).toHtmlEscaped();
		if (senderName.isEmpty()) {
			senderName = tr("Unknown").toHtmlEscaped();
		}

		DisplayEntry display;
		display.id        = id;
		display.timestamp = QDateTime::fromSecsSinceEpoch(static_cast< qint64 >(entry.timestamp()));
		display.console =
			QString::fromLatin1("<span class='log-user log-source'>%1</span>: %2").arg(senderName, u8(entry.message()));
		newEntries.append(display);
	}

	if (older) {
		newEntries += m_displayEntries;
		m_displayEntries = newEntries;
	} else {
		m_displayEntries = newEntries;
	}

	m_hasMore = response.has_more();

	if (m_displayEntries.isEmpty()) {
		Global::get().l->clearChatHistory();
		return;
	}

	QVector< ChatHistoryLogEntry > logEntries;
	logEntries.reserve(m_displayEntries.size());
	for (const DisplayEntry &entry : m_displayEntries) {
		logEntries.append({ entry.timestamp, entry.console });
	}

	const QString title = m_channel ? tr("Chat history of %1").arg(Log::formatChannel(m_channel)) : tr("Chat history");
	Global::get().l->setChatHistory(title, logEntries);
}

bool ChatHistoryController::isIgnored(const MumbleProto::ChatHistoryEntry &entry) const {
	const unsigned int senderID = entry.sender_id();
	const QString senderName    = u8(entry.sender_name());

	QReadLocker locker(&ClientUser::c_qrwlUsers);
	for (const ClientUser *user : ClientUser::c_qmUsers) {
		if (senderID != 0 && user->iId == static_cast< int >(senderID)) {
			return user->bLocalIgnore;
		}
		if (senderID == 0 && user->qsName == senderName) {
			return user->bLocalIgnore;
		}
	}

	return false;
}

void ChatHistoryController::disableHistory() {
	m_historyEnabled = false;
	m_consentPending = false;
	m_loading        = false;
	m_hasMore        = false;

	m_requestTimer->stop();
	Global::get().l->clearChatHistory();
	updateLoadEarlierButton();
}

void ChatHistoryController::updateLoadEarlierButton() {
	if (!m_mw->m_qbLoadEarlier) {
		return;
	}

	const bool visible = m_historyEnabled && m_hasMore;
	m_mw->m_qbLoadEarlier->setVisible(visible);
	m_mw->m_qbLoadEarlier->setEnabled(visible && !m_loading);

	if (visible) {
		repositionLoadEarlierButton();
	}
}

void ChatHistoryController::repositionLoadEarlierButton() {
	QToolButton *button = m_mw->m_qbLoadEarlier;
	if (!button) {
		return;
	}

	QWidget *viewport = button->parentWidget();
	if (!viewport) {
		return;
	}

	const int margin = 8;
	const QSize hint = button->sizeHint();
	button->setGeometry(viewport->width() - hint.width() - margin, margin, hint.width(), hint.height());
	button->raise();
}
