// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "net/IrcClient.h"

#include <QAbstractSocket>
#include <QSslSocket>
#include <QTcpSocket>
#include <QUrl>

IrcClient::IrcClient(QObject *parent)
    : QObject(parent)
{
}

IrcClient::~IrcClient()
{
    disconnectFromServer();
}

bool IrcClient::isConnected() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

void IrcClient::connectToServer(const QString &host, quint16 port, const QString &nick,
                                const QString &channel, bool useTls)
{
    disconnectFromServer();

    m_host = host.trimmed();
    m_port = port;
    m_nick = nick.trimmed().isEmpty() ? QStringLiteral("ComicQt") : nick.trimmed();
    m_channel = channel.trimmed();
    if (!m_channel.isEmpty() && !m_channel.startsWith(QLatin1Char('#')) &&
        !m_channel.startsWith(QLatin1Char('&'))) {
        m_channel.prepend(QLatin1Char('#'));
    }
    m_useTls = useTls;
    m_registered = false;
    m_buffer.clear();

    if (m_useTls) {
        auto *ssl = new QSslSocket(this);
        // Many public IRC certs are imperfect; allow connect for archival chat.
        ssl->setPeerVerifyMode(QSslSocket::QueryPeer);
        m_socket = ssl;
        connect(ssl, &QSslSocket::encrypted, this, &IrcClient::onConnected);
        connect(ssl, &QAbstractSocket::errorOccurred, this, &IrcClient::onSocketError);
        connect(ssl, &QIODevice::readyRead, this, &IrcClient::onReadyRead);
        connect(ssl, &QAbstractSocket::disconnected, this, &IrcClient::onDisconnected);
        emit statusMessage(QStringLiteral("Connecting (TLS) to %1:%2…").arg(m_host).arg(m_port));
        ssl->connectToHostEncrypted(m_host, m_port);
    } else {
        auto *tcp = new QTcpSocket(this);
        m_socket = tcp;
        connect(tcp, &QAbstractSocket::connected, this, &IrcClient::onConnected);
        connect(tcp, &QAbstractSocket::errorOccurred, this, &IrcClient::onSocketError);
        connect(tcp, &QIODevice::readyRead, this, &IrcClient::onReadyRead);
        connect(tcp, &QAbstractSocket::disconnected, this, &IrcClient::onDisconnected);
        emit statusMessage(QStringLiteral("Connecting to %1:%2…").arg(m_host).arg(m_port));
        tcp->connectToHost(m_host, m_port);
    }
}

void IrcClient::disconnectFromServer()
{
    if (!m_socket) {
        return;
    }
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        writeLine(QStringLiteral("QUIT :Comic Chat Qt"));
        m_socket->flush();
    }
    m_socket->disconnect(this);
    m_socket->deleteLater();
    m_socket = nullptr;
    m_registered = false;
    m_buffer.clear();
}

void IrcClient::sendRaw(const QString &line)
{
    writeLine(line);
}

void IrcClient::sendPrivmsg(const QString &target, const QString &text)
{
    if (target.isEmpty() || text.isEmpty()) {
        return;
    }
    writeLine(QStringLiteral("PRIVMSG %1 :%2").arg(target, text));
}

void IrcClient::sendChannelMessage(const QString &text)
{
    if (m_channel.isEmpty()) {
        emit errorOccurred(QStringLiteral("No channel joined"));
        return;
    }
    sendPrivmsg(m_channel, text);
}

void IrcClient::writeLine(const QString &line)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }
    QByteArray data = line.toUtf8();
    data.append("\r\n");
    m_socket->write(data);
}

void IrcClient::onConnected()
{
    emit statusMessage(QStringLiteral("Connected — registering as %1").arg(m_nick));
    registerAndJoin();
    emit connected();
}

void IrcClient::registerAndJoin()
{
    writeLine(QStringLiteral("NICK %1").arg(m_nick));
    writeLine(QStringLiteral("USER %1 0 * :Comic Chat Qt").arg(m_nick));
}

void IrcClient::onDisconnected()
{
    m_registered = false;
    emit statusMessage(QStringLiteral("Disconnected"));
    emit disconnected();
}

void IrcClient::onSocketError()
{
    if (!m_socket) {
        return;
    }
    emit errorOccurred(m_socket->errorString());
}

void IrcClient::onReadyRead()
{
    if (!m_socket) {
        return;
    }
    m_buffer.append(m_socket->readAll());
    while (true) {
        int crlf = m_buffer.indexOf("\r\n");
        int lf = m_buffer.indexOf('\n');
        int cut = -1;
        int skip = 0;
        if (crlf >= 0 && (lf < 0 || crlf <= lf)) {
            cut = crlf;
            skip = 2;
        } else if (lf >= 0) {
            cut = lf;
            skip = 1;
        } else {
            break;
        }
        QByteArray raw = m_buffer.left(cut);
        m_buffer.remove(0, cut + skip);
        if (raw.endsWith('\r')) {
            raw.chop(1);
        }
        processLine(QString::fromUtf8(raw));
    }
}

void IrcClient::processLine(const QString &line)
{
    if (line.isEmpty()) {
        return;
    }

    // PING
    if (line.startsWith(QLatin1String("PING "), Qt::CaseInsensitive)) {
        QString token = line.mid(5);
        if (token.startsWith(QLatin1Char(':'))) {
            token = token.mid(1);
        }
        writeLine(QStringLiteral("PONG :%1").arg(token));
        return;
    }

    // :prefix COMMAND args...
    QString prefix;
    QString rest = line;
    if (rest.startsWith(QLatin1Char(':'))) {
        const int sp = rest.indexOf(QLatin1Char(' '));
        if (sp < 0) {
            return;
        }
        prefix = rest.mid(1, sp - 1);
        rest = rest.mid(sp + 1).trimmed();
    }

    QString command;
    QStringList params;
    {
        const int sp = rest.indexOf(QLatin1Char(' '));
        if (sp < 0) {
            command = rest;
        } else {
            command = rest.left(sp);
            QString args = rest.mid(sp + 1);
            while (!args.isEmpty()) {
                if (args.startsWith(QLatin1Char(':'))) {
                    params << args.mid(1);
                    break;
                }
                const int n = args.indexOf(QLatin1Char(' '));
                if (n < 0) {
                    params << args;
                    break;
                }
                params << args.left(n);
                args = args.mid(n + 1);
            }
        }
    }

    const QString cmd = command.toUpper();

    // Welcome / registered
    if (cmd == QLatin1String("001") || cmd == QLatin1String("376") ||
        cmd == QLatin1String("422")) {
        if (!m_registered) {
            m_registered = true;
            if (!m_channel.isEmpty()) {
                writeLine(QStringLiteral("JOIN %1").arg(m_channel));
                emit statusMessage(QStringLiteral("Joining %1").arg(m_channel));
            }
        }
        if (!params.isEmpty()) {
            emit serverNotice(params.last());
        }
        return;
    }

    if (cmd == QLatin1String("NOTICE") || cmd == QLatin1String("001") ||
        cmd == QLatin1String("002") || cmd == QLatin1String("003") ||
        cmd == QLatin1String("372") || cmd == QLatin1String("375") ||
        cmd == QLatin1String("376")) {
        if (!params.isEmpty()) {
            emit serverNotice(params.last());
        }
        return;
    }

    if (cmd == QLatin1String("JOIN")) {
        QString nick = prefix.section(QLatin1Char('!'), 0, 0);
        emit statusMessage(QStringLiteral("%1 joined %2").arg(nick, m_channel));
        return;
    }

    if (cmd == QLatin1String("PRIVMSG") && params.size() >= 2) {
        QString nick = prefix.section(QLatin1Char('!'), 0, 0);
        const QString target = params.at(0);
        const QString text = params.at(1);
        // Channel or query to us
        if (target.compare(m_channel, Qt::CaseInsensitive) == 0 ||
            target.compare(m_nick, Qt::CaseInsensitive) == 0) {
            // CTCP ACTION
            if (text.startsWith(QLatin1String("\x01""ACTION ")) && text.endsWith(QChar(1))) {
                QString action = text.mid(8, text.size() - 9);
                emit channelMessage(nick, QStringLiteral("* %1").arg(action));
            } else {
                emit channelMessage(nick, text);
            }
        }
        return;
    }

    // Nickname in use
    if (cmd == QLatin1String("433")) {
        m_nick += QLatin1Char('_');
        writeLine(QStringLiteral("NICK %1").arg(m_nick));
        emit statusMessage(QStringLiteral("Nick in use — trying %1").arg(m_nick));
        return;
    }
}
