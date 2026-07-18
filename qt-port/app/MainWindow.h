// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "net/FreeqAuth.h"

#include <QMainWindow>

class ComicWidget;
class IrcClient;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QPoint;
class QPushButton;
class QScrollArea;
class QTimer;
class QWidget;

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
                      const QHash<QString, QString> &tags, bool history);
    void onIrcReact(const QString &parentMsgId, const QString &emoji,
                    const QString &nick, bool remove);
    void onIrcStatus(const QString &msg);
    void onIrcError(const QString &msg);
    void onIrcConnected();
    void onIrcDisconnected();
    void onChannelJoined(const QString &channel);
    void onHistoryBatchEnded();
    void flushHistoryComic();
    void onAuthStatus(const QString &msg);
    void onLoginSucceeded(const FreeqSession &session);
    void onLoginFailed(const QString &reason);
    void onSessionRefreshed(const FreeqSession &session);
    void syncComicSize();
    void onRoomChanged(int index);
    void onLogContextMenu(const QPoint &pos);
    void onCancelReply();

private:
    void appendLog(const QString &line);
    // Chat line in the log with freeq msgid for right-click → Reply.
    void appendChatLog(const QString &displayLine, const QString &nick, const QString &text,
                       const QString &msgid);
    void setConnectedUi(bool on);
    void updateAuthUi();
    void doIrcConnect(const FreeqSession &session);
    void populateRoomSelector();
    void setReplyTarget(const QString &msgid, const QString &nick, const QString &text);
    void clearReplyTarget();
    void updateReplyBanner();

    ComicWidget *m_comic = nullptr;
    QScrollArea *m_comicScroll = nullptr;
    QListWidget *m_log = nullptr;
    QLineEdit *m_say = nullptr;
    QComboBox *m_room = nullptr;
    QWidget *m_replyBanner = nullptr;
    QLabel *m_replyLabel = nullptr;
    QPushButton *m_cancelReplyBtn = nullptr;

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

    // freeq +reply draft (set via log context menu).
    QString m_replyMsgId;
    QString m_replyNick;
    QString m_replyText;

    // History lines: log shows all; comic only flushes the last kMaxComicHistory.
    struct HistoryComicLine {
        QString nick;
        QString text;
        QHash<QString, QString> tags;
    };
    QList<HistoryComicLine> m_historyComicQueue;
    int m_historyComicTotal = 0; // full count before trimming queue
    QTimer *m_historyComicTimer = nullptr;
    bool m_flushingHistoryComic = false;
    static constexpr int kMaxComicHistory = 10;
};
