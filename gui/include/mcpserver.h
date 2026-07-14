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

class Settings;

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

		// Command handlers — cada uno verifica precondiciones de estado
		QJsonObject CmdList();
		QJsonObject CmdPair(const QJsonObject &params);
		QJsonObject CmdPairConfirm(const QJsonObject &params);
		QJsonObject CmdConnect(const QJsonObject &params);
		QJsonObject CmdDisconnect();
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

	signals:
		void StateChanged(McpState state);
		void ServerStarted();
		void ServerStopped();
};

#endif // CHIAKI_MCPSERVER_H
