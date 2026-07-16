// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "net/FreeqAuth.h"

#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

namespace {
constexpr char kSettingsGroup[] = "freeqAuth";
// HTML that turns the broker's #oauth= fragment into a query the TCP server can read.
// (Browsers never send fragments to the server.)
const char kFragmentBridgeHtml[] = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>Comic Chat login</title>
<style>body{font-family:system-ui,sans-serif;max-width:28rem;margin:3rem auto;padding:0 1rem;color:#222}
h1{font-size:1.2rem} .muted{color:#666;font-size:.9rem}</style>
</head><body>
<h1>Finishing login…</h1>
<p class="muted">Returning to Comic Chat. You can close this tab when done.</p>
<script>
(function () {
  var h = location.hash || "";
  if (h.charAt(0) === "#") h = h.slice(1);
  if (h.indexOf("oauth=") === 0) {
    location.replace("/complete?" + h);
    return;
  }
  document.body.innerHTML = "<h1>Login incomplete</h1><p class='muted'>No OAuth payload in the URL. Close this tab and try again from Comic Chat.</p>";
})();
</script>
</body></html>
)HTML";
} // namespace

FreeqAuth::FreeqAuth(QObject *parent)
    : QObject(parent)
{
    loadCachedSession();
}

FreeqAuth::~FreeqAuth()
{
    stopListener();
}

void FreeqAuth::setBrokerOrigin(const QString &origin)
{
    QString o = origin.trimmed();
    while (o.endsWith(QLatin1Char('/'))) {
        o.chop(1);
    }
    if (!o.isEmpty()) {
        m_brokerOrigin = o;
    }
}

QString FreeqAuth::percentEncode(const QString &s)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(s));
}

void FreeqAuth::loadCachedSession()
{
    QSettings s;
    s.beginGroup(QString::fromUtf8(kSettingsGroup));
    m_session.brokerToken = s.value(QStringLiteral("brokerToken")).toString();
    m_session.handle = s.value(QStringLiteral("handle")).toString();
    m_session.did = s.value(QStringLiteral("did")).toString();
    m_session.nick = s.value(QStringLiteral("nick")).toString();
    m_session.pdsUrl = s.value(QStringLiteral("pdsUrl")).toString();
    m_session.webToken.clear(); // always refresh before IRC
    s.endGroup();
}

void FreeqAuth::persistSession() const
{
    QSettings s;
    s.beginGroup(QString::fromUtf8(kSettingsGroup));
    s.setValue(QStringLiteral("brokerToken"), m_session.brokerToken);
    s.setValue(QStringLiteral("handle"), m_session.handle);
    s.setValue(QStringLiteral("did"), m_session.did);
    s.setValue(QStringLiteral("nick"), m_session.nick);
    s.setValue(QStringLiteral("pdsUrl"), m_session.pdsUrl);
    s.endGroup();
}

void FreeqAuth::clearSession()
{
    stopListener();
    m_session = FreeqSession{};
    QSettings s;
    s.beginGroup(QString::fromUtf8(kSettingsGroup));
    s.remove(QString());
    s.endGroup();
    emit loggedOut();
}

void FreeqAuth::stopListener()
{
    m_loginInProgress = false;
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
}

void FreeqAuth::cancelLogin()
{
    if (m_loginInProgress) {
        stopListener();
        emit statusMessage(QStringLiteral("Login cancelled"));
    }
}

void FreeqAuth::login(const QString &handle)
{
    const QString h = handle.trimmed();
    if (h.isEmpty()) {
        emit loginFailed(QStringLiteral("Enter your Bluesky / ATProto handle first"));
        return;
    }

    stopListener();
    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {
        emit loginFailed(QStringLiteral("Could not start local login listener: %1")
                             .arg(m_server->errorString()));
        stopListener();
        return;
    }
    connect(m_server, &QTcpServer::newConnection, this, &FreeqAuth::onNewConnection);

    const quint16 port = m_server->serverPort();
    // freeq-auth-broker allows http://127.0.0.1 / localhost any port as return_to.
    const QString returnTo =
        QStringLiteral("http://127.0.0.1:%1/callback").arg(port);
    const QString url =
        QStringLiteral("%1/auth/login?handle=%2&return_to=%3")
            .arg(m_brokerOrigin, percentEncode(h), percentEncode(returnTo));

    m_loginInProgress = true;
    emit statusMessage(QStringLiteral("Opening browser to sign in as %1…").arg(h));
    if (!QDesktopServices::openUrl(QUrl(url))) {
        emit loginFailed(QStringLiteral("Could not open browser. Visit:\n%1").arg(url));
        stopListener();
        return;
    }
    emit statusMessage(QStringLiteral("Waiting for browser login (loopback :%1)…").arg(port));
}

void FreeqAuth::onNewConnection()
{
    if (!m_server) {
        return;
    }
    while (m_server->hasPendingConnections()) {
        QTcpSocket *sock = m_server->nextPendingConnection();
        sock->setParent(this);
        connect(sock, &QTcpSocket::readyRead, this, &FreeqAuth::onClientReadyRead);
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
    }
}

void FreeqAuth::onClientReadyRead()
{
    auto *sock = qobject_cast<QTcpSocket *>(sender());
    if (!sock) {
        return;
    }
    // Read until headers end (enough for our tiny GETs).
    if (!sock->property("buf").isValid()) {
        sock->setProperty("buf", QByteArray());
    }
    QByteArray buf = sock->property("buf").toByteArray();
    buf += sock->readAll();
    sock->setProperty("buf", buf);
    const int hdrEnd = buf.indexOf("\r\n\r\n");
    if (hdrEnd < 0) {
        if (buf.size() > 8192) {
            serveHtml(sock, QByteArrayLiteral("Bad request"), 400);
            sock->disconnectFromHost();
        }
        return;
    }

    const QByteArray reqLine = buf.left(buf.indexOf("\r\n"));
    // GET /path?query HTTP/1.1
    const QList<QByteArray> parts = reqLine.split(' ');
    QByteArray target = parts.size() >= 2 ? parts.at(1) : QByteArrayLiteral("/");
    QByteArray path = target;
    QByteArray query;
    const int qpos = target.indexOf('?');
    if (qpos >= 0) {
        path = target.left(qpos);
        query = target.mid(qpos + 1);
    }

    if (path == "/complete" || path.startsWith("/complete")) {
        QUrlQuery uq(QString::fromUtf8(query));
        const QString oauth = uq.queryItemValue(QStringLiteral("oauth"));
        if (oauth.isEmpty()) {
            serveOkClose(sock, QStringLiteral("Login failed"),
                         QStringLiteral("Missing oauth payload."));
            emit loginFailed(QStringLiteral("Broker callback missing oauth payload"));
            stopListener();
            return;
        }
        if (!applyOauthPayload(oauth.toUtf8())) {
            serveOkClose(sock, QStringLiteral("Login failed"),
                         QStringLiteral("Could not parse login result."));
            emit loginFailed(QStringLiteral("Invalid oauth payload from broker"));
            stopListener();
            return;
        }
        persistSession();
        serveOkClose(sock, QStringLiteral("Signed in"),
                     QStringLiteral("You are signed in as <b>%1</b>. Return to Comic Chat.")
                         .arg(m_session.handle.toHtmlEscaped()));
        emit statusMessage(QStringLiteral("Logged in as %1").arg(m_session.handle));
        emit loginSucceeded(m_session);
        stopListener();
        return;
    }

    // Any other path (including /callback after redirect): fragment bridge page.
    serveHtml(sock, QByteArray(kFragmentBridgeHtml));
    // Keep connection briefly; browser will navigate to /complete.
}

void FreeqAuth::serveHtml(QTcpSocket *sock, const QByteArray &body, int status)
{
    if (!sock) {
        return;
    }
    const char *reason = (status == 200) ? "OK" : "Error";
    QByteArray resp;
    resp += "HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\n";
    resp += "Content-Type: text/html; charset=utf-8\r\n";
    resp += "Connection: close\r\n";
    resp += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    resp += "\r\n";
    resp += body;
    sock->write(resp);
    sock->disconnectFromHost();
}

void FreeqAuth::serveOkClose(QTcpSocket *sock, const QString &title, const QString &body)
{
    const QString html = QStringLiteral(
                             "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>%1</title>"
                             "<style>body{font-family:system-ui,sans-serif;max-width:28rem;"
                             "margin:3rem auto;padding:0 1rem}</style></head><body>"
                             "<h1>%1</h1><p>%2</p></body></html>")
                             .arg(title.toHtmlEscaped(), body);
    serveHtml(sock, html.toUtf8());
}

bool FreeqAuth::applyOauthPayload(const QByteArray &b64url)
{
    QByteArray padded = b64url;
    // QByteArray::fromBase64 needs standard alphabet with padding for some Qt builds;
    // use Base64Url encoding flag.
    QByteArray json = QByteArray::fromBase64(
        padded, QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    if (json.isEmpty()) {
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) {
        return false;
    }
    const QJsonObject o = doc.object();
    FreeqSession sess;
    sess.webToken = o.value(QStringLiteral("token")).toString();
    sess.brokerToken = o.value(QStringLiteral("broker_token")).toString();
    sess.nick = o.value(QStringLiteral("nick")).toString();
    sess.did = o.value(QStringLiteral("did")).toString();
    sess.handle = o.value(QStringLiteral("handle")).toString();
    sess.pdsUrl = o.value(QStringLiteral("pds_url")).toString();
    if (sess.brokerToken.isEmpty() && sess.webToken.isEmpty()) {
        return false;
    }
    if (sess.nick.isEmpty()) {
        sess.nick = sess.handle;
    }
    m_session = sess;
    return m_session.isValid() || m_session.hasWebToken();
}

void FreeqAuth::refreshWebToken()
{
    if (m_session.brokerToken.isEmpty()) {
        emit loginFailed(QStringLiteral("Not logged in — use Login with Bluesky first"));
        return;
    }

    const QUrl url(m_brokerOrigin + QStringLiteral("/session"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("comic-chat-qt/0.1 (freeq-auth)"));
    // No Origin header → broker treats as non-browser client (allowed).

    const QJsonObject body{{QStringLiteral("broker_token"), m_session.brokerToken}};
    QNetworkReply *reply =
        m_nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));

    emit statusMessage(QStringLiteral("Refreshing freeq session…"));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            const int code =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray errBody = reply->readAll();
            if (code == 401) {
                clearSession();
                emit loginFailed(
                    QStringLiteral("Session expired — please Login with Bluesky again"));
                return;
            }
            emit loginFailed(QStringLiteral("Session refresh failed: %1 %2")
                                 .arg(reply->errorString(), QString::fromUtf8(errBody)));
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            emit loginFailed(QStringLiteral("Bad /session response"));
            return;
        }
        const QJsonObject o = doc.object();
        m_session.webToken = o.value(QStringLiteral("token")).toString();
        const QString nick = o.value(QStringLiteral("nick")).toString();
        if (!nick.isEmpty()) {
            m_session.nick = nick;
        }
        const QString did = o.value(QStringLiteral("did")).toString();
        if (!did.isEmpty()) {
            m_session.did = did;
        }
        const QString handle = o.value(QStringLiteral("handle")).toString();
        if (!handle.isEmpty()) {
            m_session.handle = handle;
        }
        if (m_session.webToken.isEmpty()) {
            emit loginFailed(
                QStringLiteral("Broker returned empty web-token (server may not mint "
                               "tokens for this deployment)"));
            return;
        }
        persistSession();
        emit statusMessage(QStringLiteral("Session ready as %1").arg(m_session.handle));
        emit sessionRefreshed(m_session);
    });
}
