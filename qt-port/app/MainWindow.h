// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include <QMainWindow>

class ComicWidget;
class IrcClient;
class QCheckBox;
class QLineEdit;
class QListWidget;
class QPushButton;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void onSay();
    void onConnect();
    void onDisconnect();
    void onIrcMessage(const QString &nick, const QString &text);
    void onIrcStatus(const QString &msg);
    void onIrcError(const QString &msg);
    void onIrcConnected();
    void onIrcDisconnected();

private:
    void appendLog(const QString &line);
    void setConnectedUi(bool on);

    ComicWidget *m_comic = nullptr;
    QListWidget *m_log = nullptr;
    QLineEdit *m_say = nullptr;

    QLineEdit *m_host = nullptr;
    QLineEdit *m_port = nullptr;
    QLineEdit *m_nick = nullptr;
    QLineEdit *m_channel = nullptr;
    QCheckBox *m_tls = nullptr;
    QPushButton *m_connectBtn = nullptr;
    QPushButton *m_disconnectBtn = nullptr;

    IrcClient *m_irc = nullptr;
};
