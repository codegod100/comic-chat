// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>

class QAbstractSocket;
class QTcpSocket;
class QSslSocket;

// Minimal IRC client for Comic Chat Qt port.
// Supports plain TCP, TLS, freeq SASL web-token, and IRCv3 message-tags (media).
class IrcClient : public QObject {
    Q_OBJECT
public:
    explicit IrcClient(QObject *parent = nullptr);
    ~IrcClient() override;

    bool isConnected() const;
    QString nick() const { return m_nick; }
    QString channel() const { return m_channel; }
    QString authenticatedDid() const { return m_authenticatedDid; }

    // Single-use freeq web-token for SASL method "web-token". Clear after use.
    void setWebToken(const QString &token);
    void setAuthenticatedDidHint(const QString &did);

public slots:
    void connectToServer(const QString &host, quint16 port, const QString &nick,
                         const QString &channel, bool useTls = false);
    void disconnectFromServer();
    void sendRaw(const QString &line);
    void sendPrivmsg(const QString &target, const QString &text);
    void sendChannelMessage(const QString &text);
    // freeq / IRCv3 draft/chathistory — fill msgid cache for +reply parents.
    void requestHistoryLatest(int count = 80);

signals:
    void connected();
    void disconnected();
    void statusMessage(const QString &msg);
    // tags may include freeq media-url, content-type, media-alt, msgid, +reply, …
    // history=true for join-replay / CHATHISTORY (cache parents, don't comic-panel).
    void channelMessage(const QString &nick, const QString &text,
                        const QHash<QString, QString> &tags, bool history);
    void serverNotice(const QString &text);
    void errorOccurred(const QString &msg);
    void saslSucceeded(const QString &did);
    void saslFailed(const QString &reason);
    void channelJoined(const QString &channel);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError();
    void onKeepAliveTick();

private:
    void writeLine(const QString &line);
    void processLine(const QString &line);
    void registerAndJoin();
    void replyPong(const QString &token);
    void noteServerActivity();
    void startKeepAlive();
    void stopKeepAlive();
    void enableSocketKeepAlive(QAbstractSocket *sock);
    void handleCap(const QString &prefix, const QStringList &params);
    void handleAuthenticate(const QStringList &params);
    void sendWebTokenSaslResponse();
    void finishCap();
    void maybeRequestCaps();
    // IRCv3: @key=value;key2=value2  → map (unescaped values)
    static QHash<QString, QString> parseTags(const QString &tagStr);
    static QString unescapeTagValue(const QString &v);

    QAbstractSocket *m_socket = nullptr;
    QByteArray m_buffer;
    QString m_nick;
    QString m_channel;
    QString m_host;
    quint16 m_port = 6667;
    bool m_useTls = false;
    bool m_registered = false;

    // Capability / SASL (freeq ATPROTO-CHALLENGE + web-token)
    QString m_webToken;
    QString m_authenticatedDid;
    bool m_capNegotiating = false;
    bool m_saslInProgress = false;
    bool m_wantSasl = false;
    // CAP LS 302 multi-line accumulation
    QString m_capLsAccum;
    bool m_capLsComplete = false;
    bool m_capRequested = false;
    QStringList m_ackedCaps;
    // Join-history / CHATHISTORY batch tracking
    QString m_historyBatchId;
    bool m_inHistoryBatch = false;

    QTimer m_keepAliveTimer;
    qint64 m_lastServerActivityMs = 0;
    qint64 m_lastClientPingMs = 0;
    bool m_awaitingPong = false;
    int m_missedPongs = 0;
};
