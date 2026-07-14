// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "mcpserver.h"
#include "settings.h"

#include <chiaki/session.h>
#include <chiaki/controller.h>
#include <chiaki/regist.h>
#include <chiaki/log.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QMetaObject>

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

	for(auto *c : clients)
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

// ─── button name mapping ────────────────────────────────────────────────────

uint32_t McpServer::ButtonNameToMask(const QString &name)
{
	if(name == QStringLiteral("cross"))     return CHIAKI_CONTROLLER_BUTTON_CROSS;
	if(name == QStringLiteral("circle") || name == QStringLiteral("moon"))
		return CHIAKI_CONTROLLER_BUTTON_MOON;
	if(name == QStringLiteral("square") || name == QStringLiteral("box"))
		return CHIAKI_CONTROLLER_BUTTON_BOX;
	if(name == QStringLiteral("triangle") || name == QStringLiteral("pyramid"))
		return CHIAKI_CONTROLLER_BUTTON_PYRAMID;
	if(name == QStringLiteral("dpad_left"))  return CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT;
	if(name == QStringLiteral("dpad_right")) return CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT;
	if(name == QStringLiteral("dpad_up"))    return CHIAKI_CONTROLLER_BUTTON_DPAD_UP;
	if(name == QStringLiteral("dpad_down"))  return CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN;
	if(name == QStringLiteral("l1"))         return CHIAKI_CONTROLLER_BUTTON_L1;
	if(name == QStringLiteral("r1"))         return CHIAKI_CONTROLLER_BUTTON_R1;
	if(name == QStringLiteral("l3"))         return CHIAKI_CONTROLLER_BUTTON_L3;
	if(name == QStringLiteral("r3"))         return CHIAKI_CONTROLLER_BUTTON_R3;
	if(name == QStringLiteral("options"))    return CHIAKI_CONTROLLER_BUTTON_OPTIONS;
	if(name == QStringLiteral("share"))      return CHIAKI_CONTROLLER_BUTTON_SHARE;
	if(name == QStringLiteral("touchpad"))   return CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;
	if(name == QStringLiteral("ps"))         return CHIAKI_CONTROLLER_BUTTON_PS;
	if(name == QStringLiteral("l2"))         return CHIAKI_CONTROLLER_ANALOG_BUTTON_L2;
	if(name == QStringLiteral("r2"))         return CHIAKI_CONTROLLER_ANALOG_BUTTON_R2;
	return 0;
}

// ─── controller state flush ─────────────────────────────────────────────────

void McpServer::FlushControllerState()
{
	if(!chiaki_session || !controller_state || !StateAllowsControl())
		return;

	chiaki_session_set_controller_state(chiaki_session, controller_state);
	controller_state_dirty = false;
}

// ─── constructor / destructor ───────────────────────────────────────────────

McpServer::McpServer(Settings *settings, QObject *parent)
	: QObject(parent)
	, settings(settings)
	, ws_server(nullptr)
	, active_client(nullptr)
	, m_expose(false)
	, state(McpState::Idle)
	, chiaki_session(nullptr)
	, controller_state(nullptr)
	, controller_state_dirty(false)
	, session_owned(false)
	, login_pin_pending(false)
	, pending_pin_client(nullptr)
	, pending_pair_target(CHIAKI_TARGET_PS5_1)
	, pending_regist_client(nullptr)
	, chiaki_regist_ptr(nullptr)
	, regist_active(false)
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
	CleanupSession();
	CleanupPairState();
	delete controller_state;
	controller_state = nullptr;
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

void McpServer::SetChiakiSession(ChiakiSession *session)
{
	chiaki_session = session;

	// (re)inicializar controller state si hay sesión
	if(chiaki_session)
	{
		if(!controller_state)
			controller_state = new ChiakiControllerState();
		chiaki_controller_state_set_idle(controller_state);
		controller_state_dirty = false;
	}
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
		if(pending_regist_client == client)
			pending_regist_client = nullptr;
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
	{
		result = CmdPairConfirm(params);
		// pair_confirm es async: si devuelve marcador "async",
		// lanzamos el registro en background y salimos sin responder
		if(result.contains(QStringLiteral("async")))
		{
			// Extraer pin ya validado desde params
			QString pin_str = params.value(QStringLiteral("pin")).toString();
			uint32_t pin = pin_str.toUInt();
			uint32_t console_pin = 0;
			if(params.contains(QStringLiteral("console_pin")))
				console_pin = params.value(QStringLiteral("console_pin")).toString().toUInt();

			StartRegist(client, id, pending_pair_host,
				pending_pair_target, pin, console_pin);
			return;
		}
	}
	else if(cmd == QStringLiteral("connect"))
		result = CmdConnect(params);
	else if(cmd == QStringLiteral("disconnect"))
		result = CmdDisconnect();
	else if(cmd == QStringLiteral("login_pin"))
		result = CmdLoginPin(params);
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

	QJsonArray hosts;

	// Leer hosts registrados y manuales
	QList<RegisteredHost> registered = settings->GetRegisteredHosts();
	QList<ManualHost> manuals = settings->GetManualHosts();

	for(const auto &rh : registered)
	{
		QString host_addr;

		// Buscar dirección IP en manual_hosts por MAC
		for(const auto &mh : manuals)
		{
			if(mh.GetRegistered() && mh.GetMAC() == rh.GetServerMAC())
			{
				host_addr = mh.GetHost();
				break;
			}
		}

		QJsonObject h;
		h[QStringLiteral("name")] = rh.GetServerNickname();
		h[QStringLiteral("console")] = chiaki_target_is_ps5(rh.GetTarget())
			? QStringLiteral("PS5") : QStringLiteral("PS4");
		h[QStringLiteral("host")] = host_addr;
		h[QStringLiteral("registered")] = true;
		hosts.append(h);
	}

	QJsonObject r;
	r[QStringLiteral("hosts")] = hosts;
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

	QString host = params.value(QStringLiteral("host")).toString();
	if(host.isEmpty())
		return {{QStringLiteral("error"), QStringLiteral("invalid_params")},
			{QStringLiteral("message"), QStringLiteral("Missing 'host' parameter")}};

	// Guardar para ps_pair_confirm
	pending_pair_host = host;
	pending_pair_target = CHIAKI_TARGET_PS5_1; // default PS5

	// Intentar determinar target mediante discovery
	// (best-effort: corre en background, no bloquea)
	// TODO: integrar discovery async para detectar PS4/PS5

	return {{QStringLiteral("ok"), true},
		{QStringLiteral("message"), QStringLiteral("introduce PIN 8 dígitos")}};
}

QJsonObject McpServer::CmdPairConfirm(const QJsonObject &params)
{
	if(state != McpState::Idle)
		return {{QStringLiteral("error"), QStringLiteral("not_idle")},
			{QStringLiteral("message"), QStringLiteral("Can only pair-confirm in Idle state")}};

	QString pin_str = params.value(QStringLiteral("pin")).toString();
	if(pin_str.isEmpty())
		return {{QStringLiteral("error"), QStringLiteral("invalid_params")},
			{QStringLiteral("message"), QStringLiteral("Missing 'pin' parameter")}};

	if(pending_pair_host.isEmpty())
		return {{QStringLiteral("error"), QStringLiteral("no_pending_pair")},
			{QStringLiteral("message"), QStringLiteral("No hay pairing pendiente. Usa ps_pair primero")}};

	bool ok;
	uint32_t pin = pin_str.toUInt(&ok);
	if(!ok || pin_str.length() != 8)
		return {{QStringLiteral("error"), QStringLiteral("invalid_pin")},
			{QStringLiteral("message"), QStringLiteral("PIN debe ser exactamente 8 dígitos numéricos")}};

	uint32_t console_pin = 0;
	if(params.contains(QStringLiteral("console_pin")))
	{
		QString cp = params.value(QStringLiteral("console_pin")).toString();
		console_pin = cp.toUInt(&ok);
		if(!ok)
			return {{QStringLiteral("error"), QStringLiteral("invalid_params")},
				{QStringLiteral("message"), QStringLiteral("console_pin inválido")}};
	}

	// El registro es async — la respuesta se envía desde el callback
	// Devolvemos marcador "async" para que HandleCommand no responda inmediatamente
	return {{QStringLiteral("async"), true}};
}

QJsonObject McpServer::CmdConnect(const QJsonObject &params)
{
	if(state != McpState::Idle)
		return {{QStringLiteral("error"), QStringLiteral("already_connected")},
			{QStringLiteral("message"), QStringLiteral("Ya hay una sesión activa. Usa ps_disconnect() primero")}};

	QString name = params.value(QStringLiteral("name")).toString();
	if(name.isEmpty())
		return {{QStringLiteral("error"), QStringLiteral("invalid_params")},
			{QStringLiteral("message"), QStringLiteral("Missing 'name' parameter")}};

	// ── Buscar host por nombre ───────────────────────────────────────────
	RegisteredHost rh;
	bool found = false;

	if(settings->GetNicknameRegisteredHostRegistered(name))
	{
		rh = settings->GetNicknameRegisteredHost(name);
		found = true;
	}
	else
	{
		QList<RegisteredHost> hosts = settings->GetRegisteredHosts();
		for(const auto &h : hosts)
		{
			if(h.GetServerNickname() == name)
			{
				rh = h;
				found = true;
				break;
			}
		}
	}

	if(!found)
		return {{QStringLiteral("error"), QStringLiteral("host_not_found")},
			{QStringLiteral("message"), QStringLiteral("No se encontró el host '") + name + QStringLiteral("'. Usa ps_list() para ver los hosts registrados")}};

	// ── Obtener dirección IP del manual_host ─────────────────────────────
	QString host_addr;
	QList<ManualHost> manuals = settings->GetManualHosts();
	for(const auto &mh : manuals)
	{
		if(mh.GetRegistered() && mh.GetMAC() == rh.GetServerMAC())
		{
			host_addr = mh.GetHost();
			break;
		}
	}

	if(host_addr.isEmpty())
		return {{QStringLiteral("error"), QStringLiteral("no_host_address")},
			{QStringLiteral("message"), QStringLiteral("No se encontró dirección IP para '") + name + QStringLiteral("'. Añade el host manualmente en la GUI")}};

	// ── Limpiar sesión anterior si existe ────────────────────────────────
	CleanupSession();

	// ── Construir ChiakiConnectInfo ──────────────────────────────────────
	ChiakiConnectInfo info = {};
	info.ps5 = chiaki_target_is_ps5(rh.GetTarget());
	connect_host_buf = host_addr.toUtf8();
	info.host = connect_host_buf.constData();
	info.video_profile_auto_downgrade = true;
	info.enable_keyboard = false;
	info.enable_dualsense = false;
	info.auto_regist = false;
	info.packet_loss_max = 0.02;
	info.enable_idr_on_fec_failure = false;

	// Headless mode: disable audio + video for MCP control sessions
	info.audio_video_disabled = CHIAKI_AUDIO_VIDEO_DISABLED;

	chiaki_connect_video_profile_preset(&info.video_profile,
		CHIAKI_VIDEO_RESOLUTION_PRESET_720p, CHIAKI_VIDEO_FPS_PRESET_30);

	QByteArray rk = rh.GetRPRegistKey();
	if(rk.size() != CHIAKI_SESSION_AUTH_SIZE)
		return {{QStringLiteral("error"), QStringLiteral("invalid_regist_key")},
			{QStringLiteral("message"), QStringLiteral("Regist key size mismatch")}};
	memcpy(info.regist_key, rk.constData(), CHIAKI_SESSION_AUTH_SIZE);

	QByteArray morning = rh.GetRPKey();
	if(morning.size() != sizeof(info.morning))
		return {{QStringLiteral("error"), QStringLiteral("invalid_morning")},
			{QStringLiteral("message"), QStringLiteral("Morning key size mismatch")}};
	memcpy(info.morning, morning.constData(), sizeof(info.morning));

	QByteArray psn_account_b64 = settings->GetPsnAccountId().toUtf8();
	QByteArray account_id = QByteArray::fromBase64(psn_account_b64);
	if(account_id.size() == CHIAKI_PSN_ACCOUNT_ID_SIZE)
		memcpy(info.psn_account_id, account_id.constData(), CHIAKI_PSN_ACCOUNT_ID_SIZE);
	else
		memset(info.psn_account_id, 0, sizeof(info.psn_account_id));

	info.holepunch_session = NULL;
	info.rudp_sock = NULL;

	// ── Crear sesión propia ──────────────────────────────────────────────
	chiaki_session = new ChiakiSession();
	chiaki_log_init(&chiaki_log, settings->GetLogLevelMask(), nullptr, nullptr);

	ChiakiErrorCode err = chiaki_session_init(chiaki_session, &info, &chiaki_log);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		delete chiaki_session;
		chiaki_session = nullptr;
		return {{QStringLiteral("error"), QStringLiteral("init_failed")},
			{QStringLiteral("message"), QStringLiteral("Session init failed: ") +
				QString::fromLocal8Bit(chiaki_error_string(err))}};
	}

	session_owned = true;

	// ── Registrar callback de eventos ────────────────────────────────────
	chiaki_session_set_event_cb(chiaki_session, &McpServer::SessionEventCb, this);

	// ── Arrancar sesión ──────────────────────────────────────────────────
	err = chiaki_session_start(chiaki_session);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CleanupSession();
		return {{QStringLiteral("error"), QStringLiteral("start_failed")},
			{QStringLiteral("message"), QStringLiteral("Session start failed: ") +
				QString::fromLocal8Bit(chiaki_error_string(err))}};
	}

	if(!controller_state)
	{
		controller_state = new ChiakiControllerState();
		chiaki_controller_state_set_idle(controller_state);
	}

	SetState(McpState::Connected);

	return {{QStringLiteral("state"), StateString()},
		{QStringLiteral("host"), name},
		{QStringLiteral("console"), chiaki_target_is_ps5(rh.GetTarget())
			? QStringLiteral("PS5") : QStringLiteral("PS4")}};
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

	// Si la sesión es nuestra (MCP-initiated), limpiarla completamente
	if(session_owned)
	{
		chiaki_session_stop(chiaki_session);
		CleanupSession();
	}
	else
	{
		// Sesión externa (StreamSession): delegar el stop
		chiaki_session_stop(chiaki_session);
	}

	SetState(McpState::Idle);

	return {{QStringLiteral("state"), StateString()}};
}

QJsonObject McpServer::CmdLoginPin(const QJsonObject &params)
{
	// Allow in any state where we have a session
	if(!chiaki_session)
		return {{QStringLiteral("error"), QStringLiteral("no_session")},
			{QStringLiteral("message"), QStringLiteral("No hay sesión activa")}};

	if(!login_pin_pending)
		return {{QStringLiteral("error"), QStringLiteral("no_pin_pending")},
			{QStringLiteral("message"), QStringLiteral("No hay PIN pendiente")}};

	QString pin = params.value(QStringLiteral("pin")).toString();
	if(pin.isEmpty())
		return {{QStringLiteral("error"), QStringLiteral("invalid_params")},
			{QStringLiteral("message"), QStringLiteral("Missing 'pin' parameter")}};

	QByteArray pin_data = pin.toUtf8();
	chiaki_session_set_login_pin(chiaki_session,
		(const uint8_t *)pin_data.constData(), pin_data.size());

	login_pin_pending = false;

	return {{QStringLiteral("ok"), true}};
}

QJsonObject McpServer::CmdPress(const QJsonObject &params)
{
	CHECK_CONTROL_STATE;

	if(!controller_state)
		return {{QStringLiteral("error"), QStringLiteral("internal_error")},
			{QStringLiteral("message"), QStringLiteral("Controller state not initialized")}};

	QJsonArray buttons = params.value(QStringLiteral("buttons")).toArray();
	int duration_ms = params.value(QStringLiteral("duration_ms")).toInt(0);

	if(buttons.isEmpty())
		return {{QStringLiteral("error"), QStringLiteral("invalid_params")},
			{QStringLiteral("message"), QStringLiteral("Missing 'buttons' array")}};

	// acumular máscara de botones
	uint32_t mask = 0;
	for(const QJsonValue &v : buttons)
	{
		QString name = v.toString().toLower();
		uint32_t btn = ButtonNameToMask(name);
		if(btn == 0)
		{
			return {{QStringLiteral("error"), QStringLiteral("unknown_button")},
				{QStringLiteral("message"), QStringLiteral("Unknown button: ") + name}};
		}
		mask |= btn;
	}

	// aplicar botones (OR sobre estado actual)
	controller_state->buttons |= mask;

	// manejar L2/R2 como analógicos si están en la máscara
	if(mask & CHIAKI_CONTROLLER_ANALOG_BUTTON_L2)
		controller_state->l2_state = 255;
	if(mask & CHIAKI_CONTROLLER_ANALOG_BUTTON_R2)
		controller_state->r2_state = 255;

	FlushControllerState();

	// si hay duration, programar release después de ese tiempo
	if(duration_ms > 0)
	{
		QTimer::singleShot(duration_ms, this, [this, mask]()
		{
			if(!controller_state || !StateAllowsControl())
				return;

			// soltar los botones que presionamos
			controller_state->buttons &= ~mask;

			// soltar triggers si estaban en la máscara
			if(mask & CHIAKI_CONTROLLER_ANALOG_BUTTON_L2)
				controller_state->l2_state = 0;
			if(mask & CHIAKI_CONTROLLER_ANALOG_BUTTON_R2)
				controller_state->r2_state = 0;

			FlushControllerState();
		});
	}

	QJsonObject r;
	r[QStringLiteral("pressed")] = buttons;
	if(duration_ms > 0)
		r[QStringLiteral("duration_ms")] = duration_ms;
	return r;
}

QJsonObject McpServer::CmdStick(const QJsonObject &params)
{
	CHECK_CONTROL_STATE;

	if(!controller_state)
		return {{QStringLiteral("error"), QStringLiteral("internal_error")},
			{QStringLiteral("message"), QStringLiteral("Controller state not initialized")}};

	// parsear floats -1.0 .. 1.0
	bool ok;
	double lx = params.value(QStringLiteral("lx")).toDouble(0.0);
	double ly = params.value(QStringLiteral("ly")).toDouble(0.0);
	double rx = params.value(QStringLiteral("rx")).toDouble(0.0);
	double ry = params.value(QStringLiteral("ry")).toDouble(0.0);

	// validar rangos (son opcionales, solo los presentes se actualizan)
	if(params.contains(QStringLiteral("lx")))
	{
		if(lx < -1.0 || lx > 1.0)
			return {{QStringLiteral("error"), QStringLiteral("invalid_params")},
				{QStringLiteral("message"), QStringLiteral("lx must be -1.0..1.0")}};
		controller_state->left_x = static_cast<int16_t>(lx * 32767.0);
	}
	if(params.contains(QStringLiteral("ly")))
	{
		if(ly < -1.0 || ly > 1.0)
			return {{QStringLiteral("error"), QStringLiteral("invalid_params")},
				{QStringLiteral("message"), QStringLiteral("ly must be -1.0..1.0")}};
		controller_state->left_y = static_cast<int16_t>(ly * 32767.0);
	}
	if(params.contains(QStringLiteral("rx")))
	{
		if(rx < -1.0 || rx > 1.0)
			return {{QStringLiteral("error"), QStringLiteral("invalid_params")},
				{QStringLiteral("message"), QStringLiteral("rx must be -1.0..1.0")}};
		controller_state->right_x = static_cast<int16_t>(rx * 32767.0);
	}
	if(params.contains(QStringLiteral("ry")))
	{
		if(ry < -1.0 || ry > 1.0)
			return {{QStringLiteral("error"), QStringLiteral("invalid_params")},
				{QStringLiteral("message"), QStringLiteral("ry must be -1.0..1.0")}};
		controller_state->right_y = static_cast<int16_t>(ry * 32767.0);
	}

	FlushControllerState();

	return {{QStringLiteral("left"), QJsonObject{{QStringLiteral("x"), lx}, {QStringLiteral("y"), ly}}},
		{QStringLiteral("right"), QJsonObject{{QStringLiteral("x"), rx}, {QStringLiteral("y"), ry}}}};
}

QJsonObject McpServer::CmdTrigger(const QJsonObject &params)
{
	CHECK_CONTROL_STATE;

	if(!controller_state)
		return {{QStringLiteral("error"), QStringLiteral("internal_error")},
			{QStringLiteral("message"), QStringLiteral("Controller state not initialized")}};

	if(params.contains(QStringLiteral("l2")))
	{
		int l2 = params.value(QStringLiteral("l2")).toInt(0);
		if(l2 < 0 || l2 > 255)
			return {{QStringLiteral("error"), QStringLiteral("invalid_params")},
				{QStringLiteral("message"), QStringLiteral("l2 must be 0..255")}};
		controller_state->l2_state = static_cast<uint8_t>(l2);
	}

	if(params.contains(QStringLiteral("r2")))
	{
		int r2 = params.value(QStringLiteral("r2")).toInt(0);
		if(r2 < 0 || r2 > 255)
			return {{QStringLiteral("error"), QStringLiteral("invalid_params")},
				{QStringLiteral("message"), QStringLiteral("r2 must be 0..255")}};
		controller_state->r2_state = static_cast<uint8_t>(r2);
	}

	FlushControllerState();

	return {{QStringLiteral("l2"), controller_state->l2_state},
		{QStringLiteral("r2"), controller_state->r2_state}};
}

QJsonObject McpServer::CmdTouchpad(const QJsonObject &params)
{
	CHECK_CONTROL_STATE;

	if(!controller_state)
		return {{QStringLiteral("error"), QStringLiteral("internal_error")},
			{QStringLiteral("message"), QStringLiteral("Controller state not initialized")}};

	if(!params.contains(QStringLiteral("x")) || !params.contains(QStringLiteral("y")))
		return {{QStringLiteral("error"), QStringLiteral("invalid_params")},
			{QStringLiteral("message"), QStringLiteral("Missing x,y coordinates")}};

	int x = params.value(QStringLiteral("x")).toInt();
	int y = params.value(QStringLiteral("y")).toInt();

	if(x < 0 || x > 1920 || y < 0 || y > 1080)
		return {{QStringLiteral("error"), QStringLiteral("invalid_params")},
			{QStringLiteral("message"), QStringLiteral("x must be 0..1920, y must be 0..1080")}};

	// detener touch existente y empezar uno nuevo
	for(int i = 0; i < CHIAKI_CONTROLLER_TOUCHES_MAX; i++)
	{
		if(controller_state->touches[i].id >= 0)
			chiaki_controller_state_stop_touch(controller_state, controller_state->touches[i].id);
	}

	int8_t id = chiaki_controller_state_start_touch(controller_state,
		static_cast<uint16_t>(x), static_cast<uint16_t>(y));

	if(id < 0)
		return {{QStringLiteral("error"), QStringLiteral("touch_failed")},
			{QStringLiteral("message"), QStringLiteral("No touch slots available")}};

	FlushControllerState();

	return {{QStringLiteral("x"), x}, {QStringLiteral("y"), y}, {QStringLiteral("touch_id"), id}};
}

QJsonObject McpServer::CmdHome()
{
	CHECK_CONTROL_STATE;

	if(!chiaki_session)
		return {{QStringLiteral("error"), QStringLiteral("internal_error")},
			{QStringLiteral("message"), QStringLiteral("Session not set")}};

	chiaki_session_go_home(chiaki_session);
	return {};
}

QJsonObject McpServer::CmdSleep()
{
	CHECK_CONTROL_STATE;

	if(!chiaki_session)
		return {{QStringLiteral("error"), QStringLiteral("internal_error")},
			{QStringLiteral("message"), QStringLiteral("Session not set")}};

	chiaki_session_goto_bed(chiaki_session);
	SetState(McpState::Idle);
	return {{QStringLiteral("state"), StateString()}};
}

QJsonObject McpServer::CmdKeyboard(const QJsonObject &params)
{
	CHECK_CONTROL_STATE;

	if(!chiaki_session)
		return {{QStringLiteral("error"), QStringLiteral("internal_error")},
			{QStringLiteral("message"), QStringLiteral("Session not set")}};

	QString text = params.value(QStringLiteral("text")).toString();
	if(text.isEmpty())
		return {{QStringLiteral("error"), QStringLiteral("invalid_params")},
			{QStringLiteral("message"), QStringLiteral("Missing 'text' parameter")}};

	QByteArray text_utf8 = text.toUtf8();
	chiaki_session_keyboard_set_text(chiaki_session, text_utf8.constData());

	return {{QStringLiteral("text"), text}};
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

// ═════════════════════════════════════════════════════════════════════════════
// Session lifecycle — MCP-initiated connections
// ═════════════════════════════════════════════════════════════════════════════

void McpServer::CleanupSession()
{
	if(!session_owned || !chiaki_session)
		return;

	chiaki_session_fini(chiaki_session);
	delete chiaki_session;
	chiaki_session = nullptr;
	session_owned = false;
	login_pin_pending = false;
	pending_pin_client = nullptr;
}

void McpServer::SessionEventCb(ChiakiEvent *event, void *user)
{
	auto *self = static_cast<McpServer *>(user);
	QMetaObject::invokeMethod(self, [self, e = *event]() mutable {
		self->OnSessionEvent(&e);
	}, Qt::QueuedConnection);
}

void McpServer::OnSessionEvent(ChiakiEvent *event)
{
	switch(event->type)
	{
		case CHIAKI_EVENT_LOGIN_PIN_REQUEST:
		{
			login_pin_pending = true;
			pending_pin_client = active_client;

			QJsonObject notif;
			notif[QStringLiteral("type")] = QStringLiteral("pin_request");
			notif[QStringLiteral("message")] = event->login_pin_request.pin_incorrect
				? QStringLiteral("PIN incorrecto. Intenta de nuevo")
				: QStringLiteral("PS5/PS4 solicita PIN de acceso");

			QJsonDocument doc(notif);
			QString payload = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));

			for(auto *c : clients)
				c->sendTextMessage(payload);
			break;
		}
		case CHIAKI_EVENT_QUIT:
		{
			CleanupSession();

			QJsonObject notif;
			notif[QStringLiteral("type")] = QStringLiteral("quit");
			notif[QStringLiteral("reason")] = QString::fromUtf8(
				chiaki_quit_reason_string(event->quit.reason));

			QJsonDocument doc(notif);
			QString payload = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));

			for(auto *c : clients)
				c->sendTextMessage(payload);

			SetState(McpState::Idle);
			break;
		}
		default:
			break;
	}
}

// ═════════════════════════════════════════════════════════════════════════════
// Pairing / Registration — async via chiaki_regist
// ═════════════════════════════════════════════════════════════════════════════

void McpServer::CleanupPairState()
{
	if(regist_active)
	{
		chiaki_regist_stop(chiaki_regist_ptr);
		chiaki_regist_fini(chiaki_regist_ptr);
		delete chiaki_regist_ptr;
		chiaki_regist_ptr = nullptr;
		regist_active = false;
	}

	pending_pair_host.clear();
	pending_regist_client = nullptr;
	pending_regist_id = QJsonValue::Undefined;
}

void McpServer::StartRegist(QWebSocket *client, const QJsonValue &id,
	const QString &host, ChiakiTarget target,
	uint32_t pin, uint32_t console_pin)
{
	// Limpiar registro anterior si existe
	if(regist_active)
	{
		chiaki_regist_stop(chiaki_regist_ptr);
		chiaki_regist_fini(chiaki_regist_ptr);
		delete chiaki_regist_ptr;
		chiaki_regist_ptr = nullptr;
		regist_active = false;
	}

	// Guardar request pendiente para responder en el callback
	pending_regist_client = client;
	pending_regist_id = id;

	// Configurar ChiakiRegistInfo
	QByteArray hostb = host.toUtf8();
	ChiakiRegistInfo info = {};
	info.target = target;
	info.host = hostb.constData();
	info.broadcast = false;
	info.pin = pin;
	info.console_pin = console_pin;
	info.holepunch_info = nullptr;
	info.rudp = nullptr;

	// Obtener PSN account ID de settings si está disponible
	QByteArray psn_account_b64 = settings->GetPsnAccountId().toUtf8();
	QByteArray account_id = QByteArray::fromBase64(psn_account_b64);
	if(account_id.size() == CHIAKI_PSN_ACCOUNT_ID_SIZE)
	{
		info.psn_online_id = nullptr;
		memcpy(info.psn_account_id, account_id.constData(), CHIAKI_PSN_ACCOUNT_ID_SIZE);
	}
	else
	{
		info.psn_online_id = nullptr;
		memset(info.psn_account_id, 0, sizeof(info.psn_account_id));
	}

	// Inicializar log y regist
	chiaki_regist_ptr = new ChiakiRegist();
	chiaki_log_init(&regist_log, settings->GetLogLevelMask(), nullptr, nullptr);

	ChiakiErrorCode err = chiaki_regist_start(chiaki_regist_ptr,
		&regist_log, &info, &McpServer::RegistCb, this);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		SendError(client, id,
			QStringLiteral("regist_error"),
			QStringLiteral("Failed to start registration: ") + QString::number(err));
		delete chiaki_regist_ptr;
		chiaki_regist_ptr = nullptr;
		pending_regist_client = nullptr;
		return;
	}

	regist_active = true;
}

void McpServer::RegistCb(ChiakiRegistEvent *event, void *user)
{
	auto *self = static_cast<McpServer *>(user);

	switch(event->type)
	{
		case CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS:
		{
			// Copiar host antes de marshal (el puntero vive en el stack del thread)
			ChiakiRegisteredHost host_copy = *event->registered_host;
			QMetaObject::invokeMethod(self, [self, host_copy]() {
				self->OnRegistSuccess(host_copy);
			}, Qt::QueuedConnection);
			break;
		}
		case CHIAKI_REGIST_EVENT_TYPE_FINISHED_FAILED:
		{
			QMetaObject::invokeMethod(self, [self]() {
				self->OnRegistFailed();
			}, Qt::QueuedConnection);
			break;
		}
		default:
			break;
	}
}

void McpServer::OnRegistSuccess(const ChiakiRegisteredHost &host)
{
	if(!pending_regist_client)
		return;

	// Crear RegisteredHost y guardar en settings
	RegisteredHost rh(host);
	settings->AddRegisteredHost(rh);

	// Si hay un manual_host con esta MAC, marcarlo como registrado
	QList<ManualHost> manuals = settings->GetManualHosts();
	for(auto &mh : manuals)
	{
		if(!mh.GetRegistered() && mh.GetHost() == pending_pair_host)
		{
			mh.Register(rh);
			settings->SetManualHost(mh);
			break;
		}
	}

	// Responder al cliente
	QJsonObject extra;
	extra[QStringLiteral("ok")] = true;
	extra[QStringLiteral("name")] = rh.GetServerNickname();
	extra[QStringLiteral("console")] = chiaki_target_is_ps5(rh.GetTarget())
		? QStringLiteral("PS5") : QStringLiteral("PS4");

	SendSuccess(pending_regist_client, pending_regist_id, extra);

	// Limpiar estado
	chiaki_regist_fini(chiaki_regist_ptr);
	delete chiaki_regist_ptr;
	chiaki_regist_ptr = nullptr;
	regist_active = false;
	pending_pair_host.clear();
	pending_regist_client = nullptr;
	pending_regist_id = QJsonValue::Undefined;
}

void McpServer::OnRegistFailed()
{
	if(!pending_regist_client)
		return;

	SendError(pending_regist_client, pending_regist_id,
		QStringLiteral("regist_failed"),
		QStringLiteral("Registration failed. Verifica que el PIN sea correcto y la consola esté accesible."));

	// Limpiar estado
	chiaki_regist_fini(chiaki_regist_ptr);
	delete chiaki_regist_ptr;
	chiaki_regist_ptr = nullptr;
	regist_active = false;
	pending_pair_host.clear();
	pending_regist_client = nullptr;
	pending_regist_id = QJsonValue::Undefined;
}
