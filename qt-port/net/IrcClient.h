// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include <QObject>
#include <QString>

class QAbstractSocket;
class QTcpSocket;
class QSslSocket;

// Minimal IRC client for Comic Chat Qt port.
// Supports plain TCP and optional TLS (QSslSocket).
class IrcClient : public QObject {
    Q_OBJECT
public:
    explicit IrcClient(QObject *parent = nullptr);
    ~IrcClient() override;

    bool isConnected() const;
    QString nick() const { return m_nick; }
    QString channel() const { return m_channel; }

public slots:
    void connectToServer(const QString &host, quint16 port, const QString &nick,
                         const QString &channel, bool useTls = false);
    void disconnectFromServer();
    void sendRaw(const QString &line);
    void sendPrivmsg(const QString &target, const QString &text);
    void sendChannelMessage(const QString &text);

signals:
    void connected();
    void disconnected();
    void statusMessage(const QString &msg);
    void channelMessage(const QString &nick, const QString &text);
    void serverNotice(const QString &text);
    void errorOccurred(const QString &msg);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError();

private:
    void writeLine(const QString &line);
    void processLine(const QString &line);
    void registerAndJoin();

    QAbstractSocket *m_socket = nullptr;
    QByteArray m_buffer;
    QString m_nick;
    QString m_channel;
    QString m_host;
    quint16 m_port = 6667;
    bool m_useTls = false;
    bool m_registered = false;
};
