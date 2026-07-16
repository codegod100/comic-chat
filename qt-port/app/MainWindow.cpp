// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "app/MainWindow.h"
#include "app/ComicWidget.h"
#include "net/IrcClient.h"

#include <QCheckBox>
#include <QEvent>
#include <QFrame>
#include <QGroupBox>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Comic Chat (Qt port)"));
    resize(1000, 760);

    m_auth = new FreeqAuth(this);
    connect(m_auth, &FreeqAuth::statusMessage, this, &MainWindow::onAuthStatus);
    connect(m_auth, &FreeqAuth::loginSucceeded, this, &MainWindow::onLoginSucceeded);
    connect(m_auth, &FreeqAuth::loginFailed, this, &MainWindow::onLoginFailed);
    connect(m_auth, &FreeqAuth::sessionRefreshed, this, &MainWindow::onSessionRefreshed);
    connect(m_auth, &FreeqAuth::loggedOut, this, [this]() {
        updateAuthUi();
        appendLog(QStringLiteral("Logged out of freeq / ATProto."));
    });

    m_irc = new IrcClient(this);
    connect(m_irc, &IrcClient::channelMessage, this, &MainWindow::onIrcMessage);
    connect(m_irc, &IrcClient::statusMessage, this, &MainWindow::onIrcStatus);
    connect(m_irc, &IrcClient::serverNotice, this, &MainWindow::onIrcStatus);
    connect(m_irc, &IrcClient::errorOccurred, this, &MainWindow::onIrcError);
    connect(m_irc, &IrcClient::connected, this, &MainWindow::onIrcConnected);
    connect(m_irc, &IrcClient::disconnected, this, &MainWindow::onIrcDisconnected);
    connect(m_irc, &IrcClient::saslSucceeded, this, [this](const QString &did) {
        appendLog(QStringLiteral("Authenticated%1")
                      .arg(did.isEmpty() ? QString() : QStringLiteral(" as %1").arg(did)));
    });
    connect(m_irc, &IrcClient::saslFailed, this, [this](const QString &r) {
        appendLog(QStringLiteral("SASL failed: %1").arg(r));
    });

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);

    // ── Identity (Bluesky / freeq) ─────────────────────────────────────
    auto *authBox = new QGroupBox(QStringLiteral("Identity (ATProto via freeq)"), central);
    auto *authForm = new QHBoxLayout(authBox);
    m_handle = new QLineEdit(authBox);
    m_handle->setPlaceholderText(QStringLiteral("you.bsky.social"));
    m_handle->setMinimumWidth(180);
    if (m_auth->isLoggedIn()) {
        m_handle->setText(m_auth->session().handle);
    }
    m_loginBtn = new QPushButton(QStringLiteral("Login with Bluesky"), authBox);
    m_logoutBtn = new QPushButton(QStringLiteral("Logout"), authBox);
    m_authLabel = new QLabel(authBox);
    m_authLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    connect(m_loginBtn, &QPushButton::clicked, this, &MainWindow::onLogin);
    connect(m_logoutBtn, &QPushButton::clicked, this, &MainWindow::onLogout);
    authForm->addWidget(new QLabel(QStringLiteral("Handle"), authBox));
    authForm->addWidget(m_handle, 1);
    authForm->addWidget(m_loginBtn);
    authForm->addWidget(m_logoutBtn);
    authForm->addWidget(m_authLabel, 2);

    // ── IRC ────────────────────────────────────────────────────────────
    auto *ircBox = new QGroupBox(QStringLiteral("IRC"), central);
    auto *ircForm = new QHBoxLayout(ircBox);

    m_host = new QLineEdit(QStringLiteral("irc.freeq.at"), ircBox);
    m_host->setPlaceholderText(QStringLiteral("host"));
    m_port = new QLineEdit(QStringLiteral("6697"), ircBox);
    m_port->setMaximumWidth(70);
    m_port->setPlaceholderText(QStringLiteral("port"));
    m_nick = new QLineEdit(QStringLiteral("ComicQt"), ircBox);
    m_nick->setMaximumWidth(140);
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

    m_comicScroll = new QScrollArea(split);
    m_comicScroll->setWidgetResizable(false);
    m_comicScroll->setFrameShape(QFrame::NoFrame);
    m_comicScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_comicScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_comicScroll->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_comic = new ComicWidget;
    m_comicScroll->setWidget(m_comic);
    m_comicScroll->viewport()->installEventFilter(this);
    connect(m_comic, &ComicWidget::contentResized, this, &MainWindow::syncComicSize);

    m_log = new QListWidget(split);
    m_log->setMaximumHeight(150);
    m_log->addItem(QStringLiteral(
        "Login with Bluesky (freeq broker), then Connect — or chat offline."));
    split->addWidget(m_comicScroll);
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
        QTimer::singleShot(0, this, &MainWindow::syncComicSize);
    });

    row->addWidget(m_say, 1);
    row->addWidget(clearBtn);

    layout->addWidget(authBox);
    layout->addWidget(ircBox);
    layout->addWidget(split, 1);
    layout->addLayout(row);
    setCentralWidget(central);

    updateAuthUi();
    statusBar()->showMessage(QStringLiteral("Ready"));
    QTimer::singleShot(0, this, &MainWindow::syncComicSize);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_comicScroll->viewport() && event->type() == QEvent::Resize) {
        syncComicSize();
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::syncComicSize()
{
    if (!m_comic || !m_comicScroll || m_syncingComic) {
        return;
    }
    m_syncingComic = true;

    const int vh = std::max(220, m_comicScroll->viewport()->height());
    m_comic->setViewportHeight(vh);
    const QSize hint = m_comic->sizeHint();
    const int w = std::max(hint.width(), m_comicScroll->viewport()->width());
    const int h = hint.height();
    if (m_comic->width() != w || m_comic->height() != h) {
        m_comic->resize(w, h);
    }
    m_comicScroll->horizontalScrollBar()->setValue(
        m_comicScroll->horizontalScrollBar()->maximum());

    m_syncingComic = false;
}

void MainWindow::appendLog(const QString &line)
{
    m_log->addItem(line);
    m_log->scrollToBottom();
}

void MainWindow::updateAuthUi()
{
    const bool in = m_auth && m_auth->isLoggedIn();
    m_logoutBtn->setEnabled(in);
    if (in) {
        const FreeqSession &s = m_auth->session();
        m_authLabel->setText(QStringLiteral("Signed in: %1").arg(s.handle));
        if (!s.nick.isEmpty()) {
            m_nick->setText(s.nick);
        } else if (!s.handle.isEmpty()) {
            // IRC-safe-ish nick from handle local part
            QString n = s.handle.section(QLatin1Char('.'), 0, 0);
            n.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_\\-\\[\\]\\{\\}]")),
                      QStringLiteral("_"));
            if (!n.isEmpty()) {
                m_nick->setText(n.left(16));
            }
        }
        m_handle->setText(s.handle);
    } else {
        m_authLabel->setText(QStringLiteral("Guest — login optional for freeq SASL"));
    }
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
    m_loginBtn->setEnabled(!on);
    m_handle->setEnabled(!on);
}

void MainWindow::onLogin()
{
    QString h = m_handle->text().trimmed();
    if (h.isEmpty()) {
        h = m_nick->text().trimmed();
    }
    m_auth->login(h);
}

void MainWindow::onLogout()
{
    m_auth->clearSession();
    updateAuthUi();
}

void MainWindow::onAuthStatus(const QString &msg)
{
    appendLog(msg);
    statusBar()->showMessage(msg, 6000);
}

void MainWindow::onLoginSucceeded(const FreeqSession &session)
{
    updateAuthUi();
    appendLog(QStringLiteral("ATProto login OK — %1 (%2)")
                  .arg(session.handle, session.did));
    // Fresh web-token already present from broker callback; ready to connect.
    statusBar()->showMessage(QStringLiteral("Logged in as %1 — hit Connect").arg(session.handle),
                             8000);
}

void MainWindow::onLoginFailed(const QString &reason)
{
    m_connectAfterRefresh = false;
    appendLog(QStringLiteral("Login error: %1").arg(reason));
    statusBar()->showMessage(reason, 10000);
    updateAuthUi();
}

void MainWindow::onSessionRefreshed(const FreeqSession &session)
{
    updateAuthUi();
    if (m_connectAfterRefresh) {
        m_connectAfterRefresh = false;
        doIrcConnect(session);
    }
}

void MainWindow::doIrcConnect(const FreeqSession &session)
{
    bool ok = false;
    const quint16 port = m_port->text().toUShort(&ok);
    if (!ok || m_host->text().trimmed().isEmpty()) {
        appendLog(QStringLiteral("Invalid host/port"));
        return;
    }

    QString nick = m_nick->text().trimmed();
    if (nick.isEmpty() && !session.nick.isEmpty()) {
        nick = session.nick;
    }
    if (nick.isEmpty()) {
        nick = QStringLiteral("ComicQt");
    }

    if (session.hasWebToken()) {
        m_irc->setWebToken(session.webToken);
        m_irc->setAuthenticatedDidHint(session.did);
        appendLog(QStringLiteral("Connecting with freeq SASL web-token…"));
    } else {
        m_irc->setWebToken(QString());
        appendLog(QStringLiteral("Connecting as guest (no web-token)…"));
    }

    m_irc->connectToServer(m_host->text(), port, nick, m_channel->text(), m_tls->isChecked());
    setConnectedUi(true);
}

void MainWindow::onConnect()
{
    // If logged in, mint a fresh single-use web-token then connect.
    if (m_auth->isLoggedIn()) {
        m_connectAfterRefresh = true;
        setConnectedUi(true); // disable double-clicks while refreshing
        m_connectBtn->setEnabled(false);
        m_auth->refreshWebToken();
        return;
    }
    // Guest connect — no SASL
    FreeqSession empty;
    doIrcConnect(empty);
}

void MainWindow::onDisconnect()
{
    m_connectAfterRefresh = false;
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
    QTimer::singleShot(0, this, &MainWindow::syncComicSize);
}

void MainWindow::onIrcMessage(const QString &nick, const QString &text,
                              const QHash<QString, QString> &tags)
{
    if (m_irc && nick.compare(m_irc->nick(), Qt::CaseInsensitive) == 0) {
        appendLog(QStringLiteral("(echo) %1: %2").arg(nick, text));
        return;
    }
    const QString mediaUrl = tags.value(QStringLiteral("media-url"));
    if (!mediaUrl.isEmpty()) {
        appendLog(QStringLiteral("%1: [image] %2").arg(nick, mediaUrl));
    } else {
        appendLog(QStringLiteral("%1: %2").arg(nick, text));
    }
    m_comic->addChatLine(text, nick, tags);
    statusBar()->showMessage(m_comic->statusLine(), 4000);
    QTimer::singleShot(0, this, &MainWindow::syncComicSize);
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
