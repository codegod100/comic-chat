// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "net/IrcClient.h"

#include <QAbstractSocket>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSslSocket>
#include <QTcpSocket>

#ifdef Q_OS_UNIX
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

namespace {
constexpr int kKeepAliveIntervalMs = 30 * 1000;
constexpr qint64 kIdleBeforeClientPingMs = 60 * 1000;
constexpr int kMaxMissedPongs = 2;
} // namespace

IrcClient::IrcClient(QObject *parent)
    : QObject(parent)
{
    m_keepAliveTimer.setInterval(kKeepAliveIntervalMs);
    connect(&m_keepAliveTimer, &QTimer::timeout, this, &IrcClient::onKeepAliveTick);
}

IrcClient::~IrcClient()
{
    disconnectFromServer();
}

bool IrcClient::isConnected() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

void IrcClient::setWebToken(const QString &token)
{
    m_webToken = token;
    m_wantSasl = !token.isEmpty();
}

void IrcClient::setAuthenticatedDidHint(const QString &did)
{
    m_authenticatedDid = did;
}

void IrcClient::enableSocketKeepAlive(QAbstractSocket *sock)
{
    if (!sock) {
        return;
    }
    sock->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
#ifdef Q_OS_UNIX
    const qintptr fd = sock->socketDescriptor();
    if (fd < 0) {
        return;
    }
    const int fdInt = static_cast<int>(fd);
#ifdef TCP_KEEPIDLE
    {
        const int idle = 60;
        ::setsockopt(fdInt, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    }
#endif
#ifdef TCP_KEEPINTVL
    {
        const int intvl = 15;
        ::setsockopt(fdInt, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
    }
#endif
#ifdef TCP_KEEPCNT
    {
        const int cnt = 4;
        ::setsockopt(fdInt, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
    }
#endif
#endif
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
    m_awaitingPong = false;
    m_missedPongs = 0;
    m_lastServerActivityMs = 0;
    m_lastClientPingMs = 0;
    m_capNegotiating = false;
    m_saslInProgress = false;
    // Keep m_webToken / m_wantSasl as set by caller.

    if (m_useTls) {
        auto *ssl = new QSslSocket(this);
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
    stopKeepAlive();
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
    m_awaitingPong = false;
    m_missedPongs = 0;
    m_capNegotiating = false;
    m_saslInProgress = false;
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
    m_socket->flush();
}

void IrcClient::onConnected()
{
    enableSocketKeepAlive(m_socket);
    noteServerActivity();
    startKeepAlive();
    emit statusMessage(QStringLiteral("Connected — registering as %1").arg(m_nick));
    registerAndJoin();
    emit connected();
}

void IrcClient::registerAndJoin()
{
    // freeq-sdk order: CAP LS, then NICK/USER. SASL completes before CAP END.
    m_capNegotiating = true;
    writeLine(QStringLiteral("CAP LS 302"));
    writeLine(QStringLiteral("NICK %1").arg(m_nick));
    writeLine(QStringLiteral("USER %1 0 * :Comic Chat Qt").arg(m_nick));
}

void IrcClient::finishCap()
{
    writeLine(QStringLiteral("CAP END"));
    m_capNegotiating = false;
    m_saslInProgress = false;
}

void IrcClient::onDisconnected()
{
    stopKeepAlive();
    m_registered = false;
    m_awaitingPong = false;
    m_capNegotiating = false;
    m_saslInProgress = false;
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

void IrcClient::startKeepAlive()
{
    m_keepAliveTimer.start();
}

void IrcClient::stopKeepAlive()
{
    m_keepAliveTimer.stop();
}

void IrcClient::noteServerActivity()
{
    m_lastServerActivityMs = QDateTime::currentMSecsSinceEpoch();
    m_awaitingPong = false;
    m_missedPongs = 0;
}

void IrcClient::onKeepAliveTick()
{
    if (!isConnected()) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastServerActivityMs <= 0) {
        m_lastServerActivityMs = now;
        return;
    }

    if (m_awaitingPong && (now - m_lastClientPingMs) >= kKeepAliveIntervalMs) {
        ++m_missedPongs;
        m_awaitingPong = false;
        if (m_missedPongs >= kMaxMissedPongs) {
            emit errorOccurred(QStringLiteral("IRC keepalive timeout (no PONG)"));
            if (m_socket) {
                m_socket->abort();
            }
            return;
        }
    }

    if ((now - m_lastServerActivityMs) >= kIdleBeforeClientPingMs && !m_awaitingPong) {
        writeLine(QStringLiteral("PING :comic-chat-qt"));
        m_lastClientPingMs = now;
        m_awaitingPong = true;
    }
}

void IrcClient::replyPong(const QString &token)
{
    if (token.isEmpty()) {
        writeLine(QStringLiteral("PONG"));
    } else {
        writeLine(QStringLiteral("PONG :%1").arg(token));
    }
    noteServerActivity();
}

void IrcClient::onReadyRead()
{
    if (!m_socket) {
        return;
    }
    m_buffer.append(m_socket->readAll());
    noteServerActivity();
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

void IrcClient::sendWebTokenSaslResponse()
{
    // freeq-sdk: { did: "", method: "web-token", signature: <token> }
    // Server looks up the token and ignores did.
    QJsonObject o;
    o.insert(QStringLiteral("did"),
             m_authenticatedDid.isEmpty() ? QString() : m_authenticatedDid);
    o.insert(QStringLiteral("method"), QStringLiteral("web-token"));
    o.insert(QStringLiteral("signature"), m_webToken);
    const QByteArray json = QJsonDocument(o).toJson(QJsonDocument::Compact);
    const QByteArray enc =
        json.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    writeLine(QStringLiteral("AUTHENTICATE %1").arg(QString::fromLatin1(enc)));
    // Token is single-use on the server; drop local copy after send.
    m_webToken.clear();
    m_wantSasl = false;
}

void IrcClient::handleCap(const QString & /*prefix*/, const QStringList &params)
{
    // Forms we care about:
    //   CAP * LS :sasl message-tags …
    //   CAP * LS * :sasl …          (302 multi-line; final line has no '*')
    //   CAP * ACK :sasl
    //   CAP nick ACK sasl
    if (params.isEmpty()) {
        return;
    }

    QString sub;
    int subIdx = -1;
    for (int i = 0; i < params.size(); ++i) {
        const QString p = params.at(i).toUpper();
        if (p == QLatin1String("LS") || p == QLatin1String("ACK") || p == QLatin1String("NAK") ||
            p == QLatin1String("NEW") || p == QLatin1String("DEL")) {
            sub = p;
            subIdx = i;
            break;
        }
    }
    if (subIdx < 0) {
        return;
    }

    // Caps list is everything after the subcommand (skip a lone "*" continuation marker).
    QStringList rest = params.mid(subIdx + 1);
    if (!rest.isEmpty() && rest.first() == QLatin1String("*")) {
        rest.removeFirst();
    }
    const QString caps = rest.join(QLatin1Char(' '));

    if (sub == QLatin1String("LS")) {
        QStringList want;
        // freeq media uses IRCv3 tags (media-url, content-type, …).
        if (caps.contains(QLatin1String("message-tags"), Qt::CaseInsensitive)) {
            want << QStringLiteral("message-tags");
        }
        if (m_wantSasl && !m_webToken.isEmpty() &&
            caps.contains(QLatin1String("sasl"), Qt::CaseInsensitive)) {
            want << QStringLiteral("sasl");
        }
        if (want.isEmpty()) {
            finishCap();
        } else {
            writeLine(QStringLiteral("CAP REQ :%1").arg(want.join(QLatin1Char(' '))));
            if (want.contains(QStringLiteral("sasl"))) {
                emit statusMessage(QStringLiteral("Negotiating SASL (ATProto)…"));
            }
        }
        return;
    }

    if (sub == QLatin1String("ACK")) {
        if (caps.contains(QLatin1String("sasl"), Qt::CaseInsensitive) &&
            !m_webToken.isEmpty()) {
            m_saslInProgress = true;
            writeLine(QStringLiteral("AUTHENTICATE ATPROTO-CHALLENGE"));
        } else {
            // message-tags only (or empty ack) — end CAP now.
            finishCap();
        }
        return;
    }

    if (sub == QLatin1String("NAK")) {
        emit statusMessage(QStringLiteral("Server rejected SASL — continuing as guest"));
        finishCap();
        return;
    }
}

void IrcClient::handleAuthenticate(const QStringList &params)
{
    if (params.isEmpty()) {
        return;
    }
    // Server sends challenge payload (base64) or "+"
    if (!m_webToken.isEmpty()) {
        sendWebTokenSaslResponse();
        return;
    }
    // No token — abort SASL
    writeLine(QStringLiteral("AUTHENTICATE *"));
    finishCap();
}

QString IrcClient::unescapeTagValue(const QString &v)
{
    // IRCv3 tag escapes: \s space, \: ;, \\ \, \r, \n
    QString out;
    out.reserve(v.size());
    for (int i = 0; i < v.size(); ++i) {
        if (v.at(i) == QLatin1Char('\\') && i + 1 < v.size()) {
            const QChar n = v.at(++i);
            if (n == QLatin1Char('s')) {
                out += QLatin1Char(' ');
            } else if (n == QLatin1Char(':')) {
                out += QLatin1Char(';');
            } else if (n == QLatin1Char('\\')) {
                out += QLatin1Char('\\');
            } else if (n == QLatin1Char('r')) {
                out += QLatin1Char('\r');
            } else if (n == QLatin1Char('n')) {
                out += QLatin1Char('\n');
            } else {
                out += n;
            }
        } else {
            out += v.at(i);
        }
    }
    return out;
}

QHash<QString, QString> IrcClient::parseTags(const QString &tagStr)
{
    QHash<QString, QString> tags;
    const QStringList parts = tagStr.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const int eq = part.indexOf(QLatin1Char('='));
        if (eq < 0) {
            tags.insert(part, QString());
        } else {
            tags.insert(part.left(eq), unescapeTagValue(part.mid(eq + 1)));
        }
    }
    return tags;
}

void IrcClient::processLine(const QString &line)
{
    if (line.isEmpty()) {
        return;
    }

    // IRCv3 message-tags: @key=value;key2=value2 :prefix CMD …
    QHash<QString, QString> tags;
    QString rest = line;
    if (rest.startsWith(QLatin1Char('@'))) {
        const int sp = rest.indexOf(QLatin1Char(' '));
        if (sp < 0) {
            return;
        }
        tags = parseTags(rest.mid(1, sp - 1));
        rest = rest.mid(sp + 1).trimmed();
    }

    if (rest.startsWith(QLatin1String("PING"), Qt::CaseInsensitive)) {
        QString token;
        if (rest.size() > 4 && rest.at(4).isSpace()) {
            token = rest.mid(5).trimmed();
            if (token.startsWith(QLatin1Char(':'))) {
                token = token.mid(1);
            }
        }
        replyPong(token);
        return;
    }

    QString prefix;
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

    if (cmd == QLatin1String("PING")) {
        replyPong(params.isEmpty() ? QString() : params.last());
        return;
    }
    if (cmd == QLatin1String("PONG")) {
        noteServerActivity();
        return;
    }
    if (cmd == QLatin1String("ERROR")) {
        emit errorOccurred(QStringLiteral("Server ERROR: %1")
                               .arg(params.isEmpty() ? line : params.last()));
        return;
    }

    if (cmd == QLatin1String("CAP")) {
        handleCap(prefix, params);
        return;
    }

    if (cmd == QLatin1String("AUTHENTICATE")) {
        handleAuthenticate(params);
        return;
    }

    // RPL_LOGGEDIN
    if (cmd == QLatin1String("900")) {
        if (!params.isEmpty()) {
            const QString text = params.last();
            const int as = text.lastIndexOf(QStringLiteral("as "));
            if (as >= 0) {
                const QString did = text.mid(as + 3).trimmed();
                if (did.startsWith(QLatin1String("did:"))) {
                    m_authenticatedDid = did;
                }
            }
            // Sometimes DID is its own param
            for (const QString &p : params) {
                if (p.startsWith(QLatin1String("did:"))) {
                    m_authenticatedDid = p;
                }
            }
        }
        return;
    }

    // RPL_SASLSUCCESS
    if (cmd == QLatin1String("903")) {
        m_saslInProgress = false;
        emit statusMessage(QStringLiteral("SASL OK%1")
                               .arg(m_authenticatedDid.isEmpty()
                                        ? QString()
                                        : QStringLiteral(" (%1)").arg(m_authenticatedDid)));
        emit saslSucceeded(m_authenticatedDid);
        finishCap();
        return;
    }

    // ERR_SASLFAIL / ERR_SASLABORTED
    if (cmd == QLatin1String("904") || cmd == QLatin1String("906")) {
        m_saslInProgress = false;
        const QString reason = params.isEmpty() ? QStringLiteral("SASL failed") : params.last();
        emit saslFailed(reason);
        emit statusMessage(QStringLiteral("SASL failed: %1 — continuing as guest").arg(reason));
        finishCap();
        return;
    }

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

    if (cmd == QLatin1String("NOTICE") || cmd == QLatin1String("002") ||
        cmd == QLatin1String("003") || cmd == QLatin1String("372") ||
        cmd == QLatin1String("375") || cmd == QLatin1String("376")) {
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
        if (target.compare(m_channel, Qt::CaseInsensitive) == 0 ||
            target.compare(m_nick, Qt::CaseInsensitive) == 0) {
            if (text.startsWith(QLatin1String("\x01""ACTION ")) && text.endsWith(QChar(1))) {
                QString action = text.mid(8, text.size() - 9);
                emit channelMessage(nick, QStringLiteral("* %1").arg(action), tags);
            } else {
                emit channelMessage(nick, text, tags);
            }
        }
        return;
    }

    if (cmd == QLatin1String("433")) {
        m_nick += QLatin1Char('_');
        writeLine(QStringLiteral("NICK %1").arg(m_nick));
        emit statusMessage(QStringLiteral("Nick in use — trying %1").arg(m_nick));
        return;
    }
}
