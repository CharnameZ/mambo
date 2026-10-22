# Chat history

Mumble can persist channel text messages so that clients can display messages
that were sent before they joined. The feature is optional and off by default.
It is configured per virtual server and only involves messages that clients
explicitly opt in to, so old clients and clients that decline keep working as
before.

The storage layout is documented in [`ServerDatabase.md`](../ServerDatabase.md);
the administrative interface (archival, clearing, paging) is part of the Ice
interface in [`MumbleServer.ice`](../../../src/murmur/MumbleServer.ice).

## Capability negotiation

Supporting clients send `ClientFeatures` right after the TLS connection has
been established and before `Version`/`Authenticate`. The message announces the
feature protocol version and a bitmask of supported features:

| Bit | Name                   | Meaning                                                    |
| --- | ---------------------- | ---------------------------------------------------------- |
| 0   | `FEATURE_CHAT_HISTORY` | The client understands the chat history messages and UI.   |

The server answers with `ServerFeatures`, containing the feature protocol
version it speaks and the features that are enabled for this connection. The
current feature protocol version is `1`; a client that sends another version
gets `enabled_features = 0` and a `reason`, but is not disconnected.

Negotiation is not a handshake: there is no timeout and no message is rejected
while it is outstanding. Clients must not wait for `ServerFeatures` before
sending `Version`/`Authenticate`. Servers that do not know the new message
types ignore them, and clients that do not know them ignore the server's
answers, which is what makes the feature backward compatible in both
directions.

The server only sends `ChatHistoryPolicy` (and only stores messages) for
sessions that sent `ClientFeatures` with `FEATURE_CHAT_HISTORY`. Old clients
therefore never receive chat history messages and their messages are never
stored. This is deliberate: the server cannot know whether an old client has
shown a privacy notice to its user.

## Messages

| Type | Message               | Direction       | Purpose                                                        |
| ---- | --------------------- | --------------- | -------------------------------------------------------------- |
| `27` | `ClientFeatures`      | client → server | Announce supported features and feature protocol version.      |
| `28` | `ServerFeatures`      | server → client | Report enabled features; sent in response to `ClientFeatures`. |
| `29` | `ChatHistoryPolicy`   | server → client | Describe the server's storage and consent policy.              |
| `30` | `ChatHistoryConsent`  | client → server | Accept or decline the current policy.                          |
| `31` | `ChatHistoryRequest`  | client → server | Request a page of a channel's history.                         |
| `32` | `ChatHistoryResponse` | server → client | Deliver a page, `has_more` and a result code.                  |

The concrete field definitions live in
[`src/Mumble.proto`](https://github.com/mumble-voip/mumble/blob/master/src/Mumble.proto);
the message type numbers are registered in `src/MumbleProtocol.h`.

## Consent

After a successful authentication, when chat history is enabled for the
virtual server, the server sends `ChatHistoryPolicy` to capable sessions. The
policy carries:

- `enabled`: whether history is enabled;
- `consent_required`: whether the client must answer with
  `ChatHistoryConsent`;
- `required_for_connection`: whether the server refuses clients that do not
  support or accept history (strict mode);
- `policy_version`: bumped by the administrator when the notice changes;
- `max_messages`: maximum page size accepted for a request;
- `retention_days`: configured retention; `0` means messages are kept until an
  administrator clears them. Automatic expiry is not implemented yet, so
  non-zero values are only advertised to clients at the moment;
- `notice`: an optional administrator-provided text.

If consent is required, the session starts in `AwaitingConsent`. A
`ChatHistoryConsent` with a matching `policy_version` moves it to `Consented`
or `Declined`. A mismatching version makes the server send the current policy
again. If no answer arrives within the configured timeout, the session is
treated as having declined. Declining keeps the connection and normal chat
working; history is simply unavailable for that session. In strict mode a
declined or timing-out connection is rejected instead.

The client may cache acceptance locally so that the dialog is not shown again
for the same server certificate and policy version. Declines are not cached.

## Storing messages

Only channel text messages are stored. Private messages, server-generated
messages and messages to temporary channels are not. When a message is sent to
a channel tree, all target channels share one stored message and therefore one
ID.

If a message was stored, the server sets `TextMessage.message_id` on the
message it forwards to clients. The field is unset for messages that were
stored for some other reason (history disabled, no consent, database error) and
is also unset when the server could not write to the database; the message is
still forwarded in that case. Any `message_id` sent by a client is ignored and
cleared by the server.

`message_id` allows clients to correlate a live message with an entry in a
history response. IDs are allocated by the database, are stable for the
lifetime of the message and are not reused for the same channel.

## Requesting history

`ChatHistoryRequest` asks for one page of a channel's active history:

- `channel_id`: the channel to read; it must still exist;
- `before_id`: only return messages with a smaller ID; `0` starts at the
  newest message;
- `limit`: number of messages; `0` means the server's configured default.

The server clamps `limit` to the configured maximum (hard upper bound: 200),
queries the newest matching messages first and returns them in ascending ID
order, so clients can insert them in order above the live messages.
`has_more` indicates whether an older page exists.

A request requires both the `Enter` permission on the channel and the
`TextMessageHistory` permission. If either is missing, the server sends the
usual `PermissionDenied` message. Requests are rate limited per session and per
virtual server; exceeding the limit results in
`ChatHistoryResult::CHAT_HISTORY_RATE_LIMITED`.

`ChatHistoryResponse.result` reports the outcome:

| Result                          | Meaning                                                        |
| ------------------------------- | -------------------------------------------------------------- |
| `CHAT_HISTORY_OK`               | The page was returned (it may be empty).                       |
| `CHAT_HISTORY_DISABLED`         | History is disabled or the session is not participating.      |
| `CHAT_HISTORY_CONSENT_REQUIRED` | The session has not accepted the policy.                       |
| `CHAT_HISTORY_INVALID_CHANNEL`  | The channel does not exist (archived channels are invalid too). |
| `CHAT_HISTORY_RATE_LIMITED`     | Too many requests; retry later.                                |
| `CHAT_HISTORY_INTERNAL_ERROR`   | The database query failed; details are logged server-side.     |

## Archived history

When a channel is removed, its history is moved into an archive instead of
being deleted. Archived history has no protocol message: it can only be read
and managed through the Ice interface (`getArchivedChannels`,
`getArchivedChatHistory`, `deleteArchivedChannel`, `clearChatHistory`,
`clearAllChatHistory`). New channels that reuse an old channel ID never see the
archived messages.

The whole removed channel subtree is archived and its channel rows are deleted
in a single database transaction. If it fails, the channels are not removed.

## Configuration

Chat history settings are stored per virtual server in the database and are
not read from `mumble-server.ini`. Set them with the Ice `setConf` method (or a
management tool) and restart the virtual server for the changes to take effect.
The recognized keys and their defaults are:

| Key                         | Default    | Description                                                                 |
| --------------------------- | ---------- | --------------------------------------------------------------------------- |
| `chathistory`              | `false`    | Enable storing channel messages for this virtual server.                    |
| `chathistoryconsent`       | `required` | `required` asks clients for consent; `off` treats capable clients as opted in. |
| `chathistoryrequireconsent`| `false`    | Strict mode: reject clients that do not support or accept history.          |
| `chathistorymax`           | `50`       | Default and maximum page size; clamped to `[1, 200]`.                       |
| `chathistorymaxchannels`   | `100`      | Maximum number of target channels for one stored message; larger fan-outs are not stored. |
| `chathistorypolicyversion` | `1`        | Policy version; increment to make clients ask for consent again.            |
| `chathistorynotice`        | empty      | Custom notice shown to clients; empty uses a localized default text.        |
| `chathistoryretention`     | `0`        | Retention in days advertised to clients; automatic expiry is not implemented yet, so messages are kept until cleared. |
| `chathistoryconsenttimeout`| `300`      | Seconds to wait for consent before treating the session as declined.        |

Enabling `chathistoryrequireconsent` while `chathistoryconsent` is `off` is
treated as consent being required, and the server logs the conflict. Strict
mode only takes effect while `chathistory` is enabled.

## Compatibility

- Old client, new server: the client sends no `ClientFeatures`, receives no
  policy, is not stored and cannot query history. It continues to chat unless
  strict mode is on.
- New client, old server: `ClientFeatures` is ignored, no policy arrives and
  the history UI stays disabled. There is no extra delay.
- Clients that decline consent in non-strict mode continue with normal chat;
  history stays disabled for that session.
- Strict mode rejects clients without chat history support during
  authentication. It is opt-in per virtual server and off by default.
