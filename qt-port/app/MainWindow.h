// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "net/FreeqAuth.h"

#include <QMainWindow>

class ComicWidget;
class IrcClient;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QScrollArea;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onSay();
    void onConnect();
    void onDisconnect();
    void onLogin();
    void onLogout();
    void onIrcMessage(const QString &nick, const QString &text,
                      const QHash<QString, QString> &tags);
    void onIrcStatus(const QString &msg);
    void onIrcError(const QString &msg);
    void onIrcConnected();
    void onIrcDisconnected();
    void onAuthStatus(const QString &msg);
    void onLoginSucceeded(const FreeqSession &session);
    void onLoginFailed(const QString &reason);
    void onSessionRefreshed(const FreeqSession &session);
    void syncComicSize();

private:
    void appendLog(const QString &line);
    void setConnectedUi(bool on);
    void updateAuthUi();
    void doIrcConnect(const FreeqSession &session);

    ComicWidget *m_comic = nullptr;
    QScrollArea *m_comicScroll = nullptr;
    QListWidget *m_log = nullptr;
    QLineEdit *m_say = nullptr;

    QLineEdit *m_host = nullptr;
    QLineEdit *m_port = nullptr;
    QLineEdit *m_nick = nullptr;
    QLineEdit *m_channel = nullptr;
    QLineEdit *m_handle = nullptr;
    QCheckBox *m_tls = nullptr;
    QPushButton *m_connectBtn = nullptr;
    QPushButton *m_disconnectBtn = nullptr;
    QPushButton *m_loginBtn = nullptr;
    QPushButton *m_logoutBtn = nullptr;
    QLabel *m_authLabel = nullptr;

    IrcClient *m_irc = nullptr;
    FreeqAuth *m_auth = nullptr;
    bool m_syncingComic = false;
    bool m_connectAfterRefresh = false;
};
