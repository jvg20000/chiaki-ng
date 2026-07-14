// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_MCPSERVER_H
#define CHIAKI_MCPSERVER_H

#include <QObject>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QList>
#include <QSet>
#include <QHostAddress>

#include <chiaki/regist.h>
#include <chiaki/session.h>
#include <chiaki/log.h>

class Settings;
typedef struct chiaki_session_t ChiakiSession;
typedef struct chiaki_controller_state_t ChiakiControllerState;

enum class McpState
{
	Idle,
	Connected,
	Streaming
};

class McpServer : public QObject
{
	Q_OBJECT

	private:
		Settings *settings;
		QWebSocketServer *ws_server;
		QList<QWebSocket *> clients;
		QSet<QWebSocket *> authenticated;
		QWebSocket *active_client;

		QString m_token;
		bool m_expose;

		McpState state;

		ChiakiSession *chiaki_session;
		ChiakiControllerState *controller_state;
		bool controller_state_dirty;

		// Session ownership — for MCP-initiated connections (CmdConnect)
		bool session_owned;
		ChiakiLog chiaki_log;
		QByteArray connect_host_buf;  // keeps host string alive for ChiakiConnectInfo

		// Login PIN pending state
		bool login_pin_pending;
		QWebSocket *pending_pin_client;

		// Session lifecycle helpers
		void CleanupSession();
		static void SessionEventCb(ChiakiEvent *event, void *user);
		void OnSessionEvent(ChiakiEvent *event);

		bool StateAllowsControl() const;
		bool StateAllowsStream() const;
		QString StateString() const;

		void SendError(QWebSocket *client, const QJsonValue &id,
			const QString &error, const QString &message);
		void SendSuccess(QWebSocket *client, const QJsonValue &id,
			const QJsonObject &extra = QJsonObject());
		void BroadcastState();
		void DisconnectClient(QWebSocket *client);
		bool TryAuthenticate(QWebSocket *client, const QJsonObject &msg);

		void HandleCommand(QWebSocket *client, const QJsonObject &msg);

		// Helpers para mapear strings a enums
		static uint32_t ButtonNameToMask(const QString &name);
		void FlushControllerState();

		// Command handlers — cada uno verifica precondiciones de estado
		QJsonObject CmdList();
		QJsonObject CmdPair(const QJsonObject &params);
		QJsonObject CmdPairConfirm(const QJsonObject &params);
		QJsonObject CmdConnect(const QJsonObject &params);
		QJsonObject CmdDisconnect();
		QJsonObject CmdLoginPin(const QJsonObject &params);
		QJsonObject CmdStatus();
		QJsonObject CmdPress(const QJsonObject &params);
		QJsonObject CmdStick(const QJsonObject &params);
		QJsonObject CmdTrigger(const QJsonObject &params);
		QJsonObject CmdTouchpad(const QJsonObject &params);
		QJsonObject CmdScreenshot();
		QJsonObject CmdHome();
		QJsonObject CmdSleep();
		QJsonObject CmdKeyboard(const QJsonObject &params);
		QJsonObject CmdEvents();

		// Regist callback (static, invoked from chiaki thread)
		static void RegistCb(ChiakiRegistEvent *event, void *user);

		// Pairing state
		QString pending_pair_host;
		ChiakiTarget pending_pair_target;
		QWebSocket *pending_regist_client;
		QJsonValue pending_regist_id;

		// Regist lifecycle
		ChiakiLog regist_log;
		ChiakiRegist *chiaki_regist_ptr;
		bool regist_active;

		void CleanupPairState();
		void StartRegist(QWebSocket *client, const QJsonValue &id,
			const QString &host, ChiakiTarget target,
			uint32_t pin, uint32_t console_pin);
		void OnRegistSuccess(const ChiakiRegisteredHost &host);
		void OnRegistFailed();

	private slots:
		void OnNewConnection();
		void OnTextMessage(const QString &message);
		void OnDisconnected();

	public:
		explicit McpServer(Settings *settings, QObject *parent = nullptr);
		~McpServer();

		bool StartServer(quint16 port, bool expose, const QString &token);
		void StopServer();
		bool IsRunning() const;
		McpState GetState() const { return state; }

		void SetState(McpState new_state);
		void SetChiakiSession(ChiakiSession *session);

	signals:
		void StateChanged(McpState state);
		void ServerStarted();
		void ServerStopped();
};

#endif // CHIAKI_MCPSERVER_H
