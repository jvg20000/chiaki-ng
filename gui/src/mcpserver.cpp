// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "mcpserver.h"
#include "settings.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

// ─── helpers ────────────────────────────────────────────────────────────────

QString McpServer::StateString() const
{
	switch(state)
	{
		case McpState::Idle:      return QStringLiteral("idle");
		case McpState::Connected:  return QStringLiteral("connected");
		case McpState::Streaming:  return QStringLiteral("streaming");
	}
	return QStringLiteral("unknown");
}

bool McpServer::StateAllowsControl() const
{
	return state == McpState::Connected || state == McpState::Streaming;
}

bool McpServer::StateAllowsStream() const
{
	return state == McpState::Streaming;
}

// ─── JSON helpers ───────────────────────────────────────────────────────────

void McpServer::SendError(QWebSocket *client, const QJsonValue &id,
	const QString &error, const QString &message)
{
	QJsonObject response;
	if(!id.isUndefined())
		response[QStringLiteral("id")] = id;
	response[QStringLiteral("error")] = error;
	response[QStringLiteral("message")] = message;

	QJsonDocument doc(response);
	client->sendTextMessage(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
}

void McpServer::SendSuccess(QWebSocket *client, const QJsonValue &id,
	const QJsonObject &extra)
{
	QJsonObject response;
	if(!id.isUndefined())
		response[QStringLiteral("id")] = id;
	response[QStringLiteral("ok")] = true;

	for(auto it = extra.begin(); it != extra.end(); ++it)
		response.insert(it.key(), it.value());

	if(id.isUndefined())
		return; // notification, no reply

	QJsonDocument doc(response);
	client->sendTextMessage(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
}

void McpServer::BroadcastState()
{
	QJsonObject notif;
	notif[QStringLiteral("type")] = QStringLiteral("state_change");
	notif[QStringLiteral("state")] = StateString();

	QJsonDocument doc(notif);
	QString payload = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));

	for(auto *c : authenticated)
		c->sendTextMessage(payload);
}

void McpServer::DisconnectClient(QWebSocket *client)
{
	if(!client)
		return;

	// send quit notification before disconnecting
	QJsonObject notif;
	notif[QStringLiteral("type")] = QStringLiteral("quit");
	notif[QStringLiteral("reason")] = QStringLiteral("superseded");

	QJsonDocument doc(notif);
	client->sendTextMessage(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));

	client->close();
}

bool McpServer::TryAuthenticate(QWebSocket *client, const QJsonObject &msg)
{
	// already authenticated — allow re-handshake
	if(authenticated.contains(client))
		return true;

	// expose mode: require valid token
	if(m_expose)
	{
		QString token = msg.value(QStringLiteral("token")).toString();

		if(token.isEmpty() || token != m_token)
		{
			QJsonObject err;
			err[QStringLiteral("type")] = QStringLiteral("handshake_error");
			err[QStringLiteral("error")] = QStringLiteral("auth_failed");
			err[QStringLiteral("message")] = QStringLiteral("Token inválido o no proporcionado");

			QJsonDocument d(err);
			client->sendTextMessage(QString::fromUtf8(d.toJson(QJsonDocument::Compact)));
			client->close();
			return false;
		}
	}

	// disconnect previous active client (single session enforcement)
	if(active_client && active_client != client)
		DisconnectClient(active_client);

	authenticated.insert(client);
	active_client = client;
	return true;
}

// ─── constructor / destructor ───────────────────────────────────────────────

McpServer::McpServer(Settings *settings, QObject *parent)
	: QObject(parent)
	, settings(settings)
	, ws_server(nullptr)
	, active_client(nullptr)
	, m_expose(false)
	, state(McpState::Idle)
{
	ws_server = new QWebSocketServer(
		QStringLiteral("chiaki-ng MCP"),
		QWebSocketServer::NonSecureMode,
		this);

	connect(ws_server, &QWebSocketServer::newConnection,
		this, &McpServer::OnNewConnection);
}

McpServer::~McpServer()
{
	StopServer();
}

// ─── server lifecycle ───────────────────────────────────────────────────────

bool McpServer::StartServer(quint16 port, bool expose, const QString &token)
{
	m_token = token;
	m_expose = expose;

	QHostAddress bind_addr = expose ? QHostAddress::Any : QHostAddress::LocalHost;

	if(!ws_server->listen(bind_addr, port))
	{
		emit ServerStopped();
		return false;
	}

	emit ServerStarted();
	return true;
}

void McpServer::StopServer()
{
	if(ws_server->isListening())
	{
		ws_server->close();

		// cerrar todos los clientes
		for(auto *c : clients)
			c->close();
		clients.clear();
	}

	authenticated.clear();
	active_client = nullptr;

	// resetear estado al parar
	if(state != McpState::Idle)
		SetState(McpState::Idle);

	emit ServerStopped();
}

bool McpServer::IsRunning() const
{
	return ws_server && ws_server->isListening();
}

// ─── state management ───────────────────────────────────────────────────────

void McpServer::SetState(McpState new_state)
{
	if(state == new_state)
		return;

	McpState old = state;
	state = new_state;
	emit StateChanged(state);
	BroadcastState();
}

// ─── WebSocket slots ────────────────────────────────────────────────────────

void McpServer::OnNewConnection()
{
	QWebSocket *client = ws_server->nextPendingConnection();
	if(!client)
		return;

	clients.append(client);

	// localhost: auto-authenticate, limit to one active client
	if(!m_expose)
	{
		// disconnect previous active client if any
		if(active_client && active_client != client)
			DisconnectClient(active_client);

		authenticated.insert(client);
		active_client = client;
	}

	connect(client, &QWebSocket::textMessageReceived,
		this, &McpServer::OnTextMessage);
	connect(client, &QWebSocket::disconnected,
		this, &McpServer::OnDisconnected);
}

void McpServer::OnDisconnected()
{
	QWebSocket *client = qobject_cast<QWebSocket *>(sender());
	if(client)
	{
		clients.removeAll(client);
		authenticated.remove(client);
		if(active_client == client)
			active_client = nullptr;
		client->deleteLater();
	}
}

void McpServer::OnTextMessage(const QString &message)
{
	QWebSocket *client = qobject_cast<QWebSocket *>(sender());
	if(!client)
		return;

	QJsonParseError err;
	QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &err);
	if(err.error != QJsonParseError::NoError)
	{
		SendError(client, QJsonValue::Undefined,
			QStringLiteral("parse_error"),
			QStringLiteral("Invalid JSON: ") + err.errorString());
		return;
	}

	QJsonObject msg = doc.object();
	QJsonValue id = msg.value(QStringLiteral("id"));
	QString type = msg.value(QStringLiteral("type")).toString();

	// handshake — allowed for unauthenticated clients
	if(type == QStringLiteral("handshake"))
	{
		if(TryAuthenticate(client, msg))
		{
			QJsonObject reply;
			reply[QStringLiteral("type")] = QStringLiteral("handshake_ok");
			reply[QStringLiteral("version")] = 1;
			reply[QStringLiteral("state")] = StateString();
			if(!id.isUndefined())
				reply[QStringLiteral("id")] = id;
			QJsonDocument r(reply);
			client->sendTextMessage(QString::fromUtf8(r.toJson(QJsonDocument::Compact)));
		}
		// TryAuthenticate sends its own error and disconnects on failure
		return;
	}

	// all other messages require authentication
	if(!authenticated.contains(client))
	{
		SendError(client, id,
			QStringLiteral("not_authenticated"),
			QStringLiteral("Debes autenticarte primero con {\"type\":\"handshake\", \"token\":\"...\"}"));
		return;
	}

	// command dispatch
	HandleCommand(client, msg);
}

// ─── command dispatcher ─────────────────────────────────────────────────────

void McpServer::HandleCommand(QWebSocket *client, const QJsonObject &msg)
{
	QJsonValue id = msg.value(QStringLiteral("id"));
	QString cmd = msg.value(QStringLiteral("cmd")).toString();
	QJsonObject params = msg.value(QStringLiteral("params")).toObject();

	if(cmd.isEmpty())
	{
		SendError(client, id,
			QStringLiteral("unknown_command"),
			QStringLiteral("Missing 'cmd' field"));
		return;
	}

	QJsonObject result;

	// dispatch según comando
	if(cmd == QStringLiteral("list"))
		result = CmdList();
	else if(cmd == QStringLiteral("pair"))
		result = CmdPair(params);
	else if(cmd == QStringLiteral("pair_confirm"))
		result = CmdPairConfirm(params);
	else if(cmd == QStringLiteral("connect"))
		result = CmdConnect(params);
	else if(cmd == QStringLiteral("disconnect"))
		result = CmdDisconnect();
	else if(cmd == QStringLiteral("status"))
		result = CmdStatus();
	else if(cmd == QStringLiteral("press"))
		result = CmdPress(params);
	else if(cmd == QStringLiteral("stick"))
		result = CmdStick(params);
	else if(cmd == QStringLiteral("trigger"))
		result = CmdTrigger(params);
	else if(cmd == QStringLiteral("touchpad"))
		result = CmdTouchpad(params);
	else if(cmd == QStringLiteral("screenshot"))
		result = CmdScreenshot();
	else if(cmd == QStringLiteral("home"))
		result = CmdHome();
	else if(cmd == QStringLiteral("sleep"))
		result = CmdSleep();
	else if(cmd == QStringLiteral("keyboard"))
		result = CmdKeyboard(params);
	else if(cmd == QStringLiteral("events"))
		result = CmdEvents();
	else
	{
		SendError(client, id,
			QStringLiteral("unknown_command"),
			QStringLiteral("Unknown command: ") + cmd);
		return;
	}

	// si result tiene "error", devolver error; si no, success
	if(result.contains(QStringLiteral("error")))
	{
		SendError(client, id,
			result[QStringLiteral("error")].toString(),
			result[QStringLiteral("message")].toString());
	}
	else
	{
		// quitar campos internos
		QJsonObject extra;
		for(auto it = result.begin(); it != result.end(); ++it)
			extra.insert(it.key(), it.value());
		SendSuccess(client, id, extra);
	}
}

// ═════════════════════════════════════════════════════════════════════════════
// Command handlers — cada uno verifica precondiciones de estado
// ═════════════════════════════════════════════════════════════════════════════

// ── always allowed ──────────────────────────────────────────────────────────

QJsonObject McpServer::CmdList()
{
	Q_UNUSED(state); // list siempre funciona

	// stub — leería de Settings::GetRegisteredHosts()
	QJsonObject r;
	r[QStringLiteral("hosts")] = QJsonArray();
	return r;
}

QJsonObject McpServer::CmdStatus()
{
	QJsonObject r;
	r[QStringLiteral("state")] = StateString();
	return r;
}

// ── IDLE only ───────────────────────────────────────────────────────────────

QJsonObject McpServer::CmdPair(const QJsonObject &params)
{
	if(state != McpState::Idle)
		return {{QStringLiteral("error"), QStringLiteral("not_idle")},
			{QStringLiteral("message"), QStringLiteral("Can only pair in Idle state")}};

	Q_UNUSED(params);
	// stub — llamaría a chiaki_discovery_* + chiaki_regist_start()
	return {{QStringLiteral("message"), QStringLiteral("introduce PIN 8 dígitos")}};
}

QJsonObject McpServer::CmdPairConfirm(const QJsonObject &params)
{
	if(state != McpState::Idle)
		return {{QStringLiteral("error"), QStringLiteral("not_idle")},
			{QStringLiteral("message"), QStringLiteral("Can only pair-confirm in Idle state")}};

	Q_UNUSED(params);
	// stub — llamaría a chiaki_regist_finish()
	return {};
}

QJsonObject McpServer::CmdConnect(const QJsonObject &params)
{
	if(state != McpState::Idle)
		return {{QStringLiteral("error"), QStringLiteral("already_connected")},
			{QStringLiteral("message"), QStringLiteral("Ya hay una sesión activa. Usa ps_disconnect() primero")}};

	Q_UNUSED(params);
	// stub — llamaría a chiaki_session_init() + chiaki_session_start()
	SetState(McpState::Connected);
	return {{QStringLiteral("state"), StateString()}};
}

// ── CONNECTED+ (control) ────────────────────────────────────────────────────

#define CHECK_CONTROL_STATE \
	if(!StateAllowsControl()) { \
		return {{QStringLiteral("error"), QStringLiteral("no_session")}, \
			{QStringLiteral("message"), QStringLiteral("No hay sesión activa. Usa ps_connect(<nombre>) primero")}}; \
	}

QJsonObject McpServer::CmdDisconnect()
{
	// disconnect también funciona en Connected (no solo Streaming)
	if(state == McpState::Idle)
		return {{QStringLiteral("error"), QStringLiteral("no_session")},
			{QStringLiteral("message"), QStringLiteral("No hay sesión activa")}};

	// stub — llamaría a chiaki_session_stop()
	SetState(McpState::Idle);
	return {{QStringLiteral("state"), StateString()}};
}

QJsonObject McpServer::CmdPress(const QJsonObject &params)
{
	CHECK_CONTROL_STATE;
	Q_UNUSED(params);
	// stub — llamaría a chiaki_session_set_controller_state()
	return {};
}

QJsonObject McpServer::CmdStick(const QJsonObject &params)
{
	CHECK_CONTROL_STATE;
	Q_UNUSED(params);
	return {};
}

QJsonObject McpServer::CmdTrigger(const QJsonObject &params)
{
	CHECK_CONTROL_STATE;
	Q_UNUSED(params);
	return {};
}

QJsonObject McpServer::CmdTouchpad(const QJsonObject &params)
{
	CHECK_CONTROL_STATE;
	Q_UNUSED(params);
	return {};
}

QJsonObject McpServer::CmdHome()
{
	CHECK_CONTROL_STATE;
	// stub — llamaría a chiaki_session_go_home()
	return {};
}

QJsonObject McpServer::CmdSleep()
{
	CHECK_CONTROL_STATE;
	// stub — llamaría a chiaki_session_goto_bed()
	SetState(McpState::Idle);
	return {{QStringLiteral("state"), StateString()}};
}

QJsonObject McpServer::CmdKeyboard(const QJsonObject &params)
{
	CHECK_CONTROL_STATE;
	Q_UNUSED(params);
	// stub — llamaría a chiaki_session_keyboard_set_text()
	return {};
}

QJsonObject McpServer::CmdEvents()
{
	CHECK_CONTROL_STATE;
	// stub — leería cola de eventos
	return {};
}

// ── STREAMING only ──────────────────────────────────────────────────────────

QJsonObject McpServer::CmdScreenshot()
{
	if(!StateAllowsStream())
		return {{QStringLiteral("error"), QStringLiteral("no_stream")},
			{QStringLiteral("message"), QStringLiteral("El stream de vídeo no está activo aún")}};

	// stub — capturaría frame del decoder FFMPEG
	return {};
}
