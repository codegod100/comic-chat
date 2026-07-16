// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "app/MainWindow.h"
#include "app/ComicWidget.h"
#include "net/IrcClient.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Comic Chat (Qt port)"));
    resize(960, 740);

    m_irc = new IrcClient(this);
    connect(m_irc, &IrcClient::channelMessage, this, &MainWindow::onIrcMessage);
    connect(m_irc, &IrcClient::statusMessage, this, &MainWindow::onIrcStatus);
    connect(m_irc, &IrcClient::serverNotice, this, &MainWindow::onIrcStatus);
    connect(m_irc, &IrcClient::errorOccurred, this, &MainWindow::onIrcError);
    connect(m_irc, &IrcClient::connected, this, &MainWindow::onIrcConnected);
    connect(m_irc, &IrcClient::disconnected, this, &MainWindow::onIrcDisconnected);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);

    // --- IRC bar ---
    auto *ircBox = new QGroupBox(QStringLiteral("IRC"), central);
    auto *ircForm = new QHBoxLayout(ircBox);

    m_host = new QLineEdit(QStringLiteral("irc.libera.chat"), ircBox);
    m_host->setPlaceholderText(QStringLiteral("host"));
    m_port = new QLineEdit(QStringLiteral("6697"), ircBox);
    m_port->setMaximumWidth(70);
    m_port->setPlaceholderText(QStringLiteral("port"));
    m_nick = new QLineEdit(QStringLiteral("ComicQt"), ircBox);
    m_nick->setMaximumWidth(120);
    m_nick->setPlaceholderText(QStringLiteral("nick"));
    m_channel = new QLineEdit(QStringLiteral("#comicchat"), ircBox);
    m_channel->setMaximumWidth(140);
    m_channel->setPlaceholderText(QStringLiteral("#channel"));
    m_tls = new QCheckBox(QStringLiteral("TLS"), ircBox);
    m_tls->setChecked(true);

    m_connectBtn = new QPushButton(QStringLiteral("Connect"), ircBox);
    m_disconnectBtn = new QPushButton(QStringLiteral("Disconnect"), ircBox);
    m_disconnectBtn->setEnabled(false);
    connect(m_connectBtn, &QPushButton::clicked, this, &MainWindow::onConnect);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &MainWindow::onDisconnect);

    ircForm->addWidget(new QLabel(QStringLiteral("Host"), ircBox));
    ircForm->addWidget(m_host, 2);
    ircForm->addWidget(m_port);
    ircForm->addWidget(new QLabel(QStringLiteral("Nick"), ircBox));
    ircForm->addWidget(m_nick);
    ircForm->addWidget(new QLabel(QStringLiteral("Channel"), ircBox));
    ircForm->addWidget(m_channel);
    ircForm->addWidget(m_tls);
    ircForm->addWidget(m_connectBtn);
    ircForm->addWidget(m_disconnectBtn);

    auto *split = new QSplitter(Qt::Vertical, central);
    m_comic = new ComicWidget(split);
    m_log = new QListWidget(split);
    m_log->setMaximumHeight(140);
    m_log->addItem(QStringLiteral(
        "Phase 5: connect to IRC, or type offline (local panels only)."));
    m_log->addItem(QStringLiteral(
        "Tip: Libera often needs TLS on 6697. Try a test channel you control."));
    split->addWidget(m_comic);
    split->addWidget(m_log);
    split->setStretchFactor(0, 5);
    split->setStretchFactor(1, 1);

    auto *row = new QHBoxLayout;
    m_say = new QLineEdit(central);
    m_say->setPlaceholderText(QStringLiteral("Say something… (Enter)"));
    connect(m_say, &QLineEdit::returnPressed, this, &MainWindow::onSay);

    auto *clearBtn = new QPushButton(QStringLiteral("Clear panels"), central);
    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        m_comic->clearPanels();
        appendLog(QStringLiteral("Panels cleared."));
        statusBar()->showMessage(m_comic->statusLine(), 3000);
    });

    row->addWidget(m_say, 1);
    row->addWidget(clearBtn);

    layout->addWidget(ircBox);
    layout->addWidget(split, 1);
    layout->addLayout(row);
    setCentralWidget(central);

    statusBar()->showMessage(QStringLiteral("Ready — offline or connect to IRC"));
}

void MainWindow::appendLog(const QString &line)
{
    m_log->addItem(line);
    m_log->scrollToBottom();
}

void MainWindow::setConnectedUi(bool on)
{
    m_connectBtn->setEnabled(!on);
    m_disconnectBtn->setEnabled(on);
    m_host->setEnabled(!on);
    m_port->setEnabled(!on);
    m_nick->setEnabled(!on);
    m_channel->setEnabled(!on);
    m_tls->setEnabled(!on);
}

void MainWindow::onConnect()
{
    bool ok = false;
    const quint16 port = m_port->text().toUShort(&ok);
    if (!ok || m_host->text().trimmed().isEmpty()) {
        appendLog(QStringLiteral("Invalid host/port"));
        return;
    }
    m_irc->connectToServer(m_host->text(), port, m_nick->text(), m_channel->text(),
                           m_tls->isChecked());
    setConnectedUi(true);
}

void MainWindow::onDisconnect()
{
    m_irc->disconnectFromServer();
    setConnectedUi(false);
}

void MainWindow::onSay()
{
    const QString text = m_say->text().trimmed();
    if (text.isEmpty()) {
        return;
    }

    const QString nick =
        m_irc->isConnected() ? m_irc->nick() : QStringLiteral("you");

    if (m_irc->isConnected()) {
        m_irc->sendChannelMessage(text);
    }

    appendLog(QStringLiteral("%1: %2").arg(nick, text));
    m_comic->addChatLine(text, nick);
    m_say->clear();
    statusBar()->showMessage(m_comic->statusLine(), 4000);
}

void MainWindow::onIrcMessage(const QString &nick, const QString &text)
{
    // Don't double-draw our own echo if server relays it — still show others.
    if (m_irc && nick.compare(m_irc->nick(), Qt::CaseInsensitive) == 0) {
        // We already added on send; skip echo
        appendLog(QStringLiteral("(echo) %1: %2").arg(nick, text));
        return;
    }
    appendLog(QStringLiteral("%1: %2").arg(nick, text));
    m_comic->addChatLine(text, nick);
    statusBar()->showMessage(m_comic->statusLine(), 4000);
}

void MainWindow::onIrcStatus(const QString &msg)
{
    appendLog(msg);
    statusBar()->showMessage(msg, 5000);
}

void MainWindow::onIrcError(const QString &msg)
{
    appendLog(QStringLiteral("Error: %1").arg(msg));
    statusBar()->showMessage(msg, 8000);
    setConnectedUi(false);
}

void MainWindow::onIrcConnected()
{
    setConnectedUi(true);
    appendLog(QStringLiteral("IRC connected."));
}

void MainWindow::onIrcDisconnected()
{
    setConnectedUi(false);
    appendLog(QStringLiteral("IRC disconnected."));
}
