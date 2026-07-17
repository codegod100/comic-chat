// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// Practical freeq ATProto login via the hosted auth broker (auth.freeq.at):
//   1. Open browser → /auth/login?handle=…&return_to=http://127.0.0.1:<port>/callback
//   2. Loopback serves a tiny page that rewrites #oauth=… into a query string
//   3. We get {token, broker_token, nick, did, handle, pds_url}
//   4. IRC uses method "web-token" SASL; refresh via POST /session before reconnect
//
// Matches freeq-auth-broker + freeq-sdk client behaviour. No DPoP/PAR in-process.

#pragma once

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>

class QTcpServer;
class QTcpSocket;

struct FreeqSession {
    QString webToken;    // single-use SASL token (minted by freeq server)
    QString brokerToken; // durable; use with POST /session to mint fresh webToken
    QString nick;        // freeq IRC nick (from mint_web_token / mobile_nick_from_handle)
    QString did;         // ATProto DID — cryptographic identity
    QString handle;      // ATProto handle (e.g. nandi.uk) — freeq's display identity
    QString pdsUrl;
    bool isValid() const
    {
        // Logged in if we can refresh (broker token) and know who we are.
        return !brokerToken.isEmpty() && (!did.isEmpty() || !handle.isEmpty());
    }
    bool hasWebToken() const { return !webToken.isEmpty(); }

    // What freeq treats as "you": prefer ATProto handle, then broker nick, then DID.
    QString displayIdentity() const
    {
        if (!handle.isEmpty()) {
            return handle;
        }
        if (!nick.isEmpty()) {
            return nick;
        }
        return did;
    }

    // Nick to send in IRC NICK. freeq users commonly connect as their handle
    // (nandi.uk, standefer.com). Prefer handle, then broker-minted nick.
    QString ircNick() const
    {
        if (!handle.isEmpty()) {
            return handle;
        }
        if (!nick.isEmpty()) {
            return nick;
        }
        return QStringLiteral("ComicQt");
    }
};

class FreeqAuth : public QObject {
    Q_OBJECT
public:
    explicit FreeqAuth(QObject *parent = nullptr);
    ~FreeqAuth() override;

    // Default production broker (standalone freeq deployment).
    void setBrokerOrigin(const QString &origin);
    QString brokerOrigin() const { return m_brokerOrigin; }

    const FreeqSession &session() const { return m_session; }
    bool isLoggedIn() const { return m_session.isValid(); }

    // Load durable fields (broker_token, handle, …) from QSettings.
    void loadCachedSession();
    void clearSession();

public slots:
    // Open browser for Bluesky/ATProto OAuth via freeq broker.
    void login(const QString &handle);
    void cancelLogin();

    // Mint a fresh single-use web-token from broker_token (call before IRC connect).
    // Emits sessionRefreshed or loginFailed.
    void refreshWebToken();

signals:
    void statusMessage(const QString &msg);
    void loginSucceeded(const FreeqSession &session);
    void loginFailed(const QString &reason);
    void sessionRefreshed(const FreeqSession &session);
    void loggedOut();

private slots:
    void onNewConnection();
    void onClientReadyRead();

private:
    void stopListener();
    void serveHtml(QTcpSocket *sock, const QByteArray &body, int status = 200);
    void serveOkClose(QTcpSocket *sock, const QString &title, const QString &body);
    bool applyOauthPayload(const QByteArray &b64url);
    void persistSession() const;
    // Finish OAuth without re-entering readyRead (nested event loops / UAF).
    void finishLoginSuccess(QTcpSocket *sock);
    void finishLoginFailure(QTcpSocket *sock, const QString &htmlMsg, const QString &signalMsg);
    static QString percentEncode(const QString &s);

    QNetworkAccessManager m_nam;
    QTcpServer *m_server = nullptr;
    QString m_brokerOrigin = QStringLiteral("https://auth.freeq.at");
    FreeqSession m_session;
    bool m_loginInProgress = false;
};
