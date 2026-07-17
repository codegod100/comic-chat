// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "app/MainWindow.h"
#include "app/ComicWidget.h"
#include "net/IrcClient.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QGroupBox>
#include <QHash>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QPixmap>
#include <QPoint>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSize>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace {
// QListWidgetItem roles for freeq reply targeting.
constexpr int kRoleMsgId = Qt::UserRole;
constexpr int kRoleNick = Qt::UserRole + 1;
constexpr int kRoleText = Qt::UserRole + 2;
} // namespace

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
    connect(m_irc, &IrcClient::channelJoined, this, &MainWindow::onChannelJoined);
    connect(m_irc, &IrcClient::historyBatchEnded, this, &MainWindow::onHistoryBatchEnded);

    m_historyComicTimer = new QTimer(this);
    m_historyComicTimer->setSingleShot(true);
    m_historyComicTimer->setInterval(250);
    connect(m_historyComicTimer, &QTimer::timeout, this, &MainWindow::flushHistoryComic);
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
    m_log->setContextMenuPolicy(Qt::CustomContextMenu);
    m_log->setToolTip(QStringLiteral("Right-click a chat line to reply (freeq +reply)"));
    connect(m_log, &QListWidget::customContextMenuRequested, this,
            &MainWindow::onLogContextMenu);
    m_log->addItem(QStringLiteral(
        "Login with Bluesky (freeq broker), then Connect — or chat offline. "
        "Right-click messages to reply."));
    split->addWidget(m_comicScroll);
    split->addWidget(m_log);
    split->setStretchFactor(0, 5);
    split->setStretchFactor(1, 1);

    // freeq reply draft banner (hidden until right-click → Reply).
    m_replyBanner = new QWidget(central);
    auto *replyRow = new QHBoxLayout(m_replyBanner);
    replyRow->setContentsMargins(0, 0, 0, 0);
    m_replyLabel = new QLabel(m_replyBanner);
    m_replyLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_replyLabel->setStyleSheet(
        QStringLiteral("QLabel { color: #1a4a8a; background: #e8f2ff; padding: 4px 8px; "
                       "border-radius: 4px; }"));
    m_cancelReplyBtn = new QPushButton(QStringLiteral("Cancel reply"), m_replyBanner);
    connect(m_cancelReplyBtn, &QPushButton::clicked, this, &MainWindow::onCancelReply);
    replyRow->addWidget(m_replyLabel, 1);
    replyRow->addWidget(m_cancelReplyBtn);
    m_replyBanner->setVisible(false);

    auto *row = new QHBoxLayout;
    m_say = new QLineEdit(central);
    m_say->setPlaceholderText(QStringLiteral("Say something… (Enter)"));
    m_say->installEventFilter(this);
    connect(m_say, &QLineEdit::returnPressed, this, &MainWindow::onSay);

    m_room = new QComboBox(central);
    m_room->setMinimumWidth(140);
    m_room->setToolTip(QStringLiteral("Comic room / backdrop"));
    connect(m_room, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onRoomChanged);

    auto *clearBtn = new QPushButton(QStringLiteral("Clear panels"), central);
    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        m_comic->clearPanels();
        appendLog(QStringLiteral("Panels cleared."));
        statusBar()->showMessage(m_comic->statusLine(), 3000);
        QTimer::singleShot(0, this, &MainWindow::syncComicSize);
    });

    row->addWidget(new QLabel(QStringLiteral("Room"), central));
    row->addWidget(m_room);
    row->addWidget(m_say, 1);
    row->addWidget(clearBtn);

    layout->addWidget(authBox);
    layout->addWidget(ircBox);
    layout->addWidget(split, 1);
    layout->addWidget(m_replyBanner);
    layout->addLayout(row);
    setCentralWidget(central);

    updateAuthUi();
    // Fill room list after first layout (art paths resolve relative to app).
    QTimer::singleShot(0, this, [this]() {
        populateRoomSelector();
        syncComicSize();
    });
    statusBar()->showMessage(QStringLiteral("Ready"));
}

void MainWindow::populateRoomSelector()
{
    if (!m_room || !m_comic) {
        return;
    }
    const QStringList rooms = m_comic->availableRooms();
    const QSize thumb(72, 48);
    m_room->blockSignals(true);
    m_room->clear();
    m_room->setIconSize(thumb);
    for (const QString &base : rooms) {
        // Friendly label; keep base name in item data for loading.
        QString label = base;
        if (base.compare(QStringLiteral("room8bs"), Qt::CaseInsensitive) == 0) {
            label = QStringLiteral("Room");
        } else if (base.compare(QStringLiteral("field"), Qt::CaseInsensitive) == 0) {
            label = QStringLiteral("Field");
        } else if (base.compare(QStringLiteral("pastoral"), Qt::CaseInsensitive) == 0) {
            label = QStringLiteral("Pastoral");
        } else if (!label.isEmpty()) {
            label[0] = label[0].toUpper();
        }
        const QPixmap pm = m_comic->roomThumbnail(base, thumb);
        if (!pm.isNull()) {
            m_room->addItem(QIcon(pm), label, base);
        } else {
            m_room->addItem(label, base);
        }
    }
    const QString cur = m_comic->currentRoom();
    int idx = -1;
    for (int i = 0; i < m_room->count(); ++i) {
        if (m_room->itemData(i).toString().compare(cur, Qt::CaseInsensitive) == 0) {
            idx = i;
            break;
        }
    }
    if (idx >= 0) {
        m_room->setCurrentIndex(idx);
    }
    m_room->blockSignals(false);
    m_room->setEnabled(m_room->count() > 0);

    // Ensure first paint shows room preview (empty strip).
    if (idx >= 0) {
        m_comic->setRoom(m_room->itemData(idx).toString());
    }
}

void MainWindow::onRoomChanged(int index)
{
    if (!m_comic || !m_room || index < 0) {
        return;
    }
    const QString base = m_room->itemData(index).toString();
    if (base.isEmpty()) {
        return;
    }
    if (m_comic->setRoom(base)) {
        appendLog(QStringLiteral("Room: %1").arg(m_room->currentText()));
        statusBar()->showMessage(
            QStringLiteral("Room preview: %1").arg(m_room->currentText()), 4000);
        m_comic->update();
        QTimer::singleShot(0, this, &MainWindow::syncComicSize);
    } else {
        appendLog(QStringLiteral("Could not load room “%1”").arg(base));
        statusBar()->showMessage(m_comic->statusLine(), 6000);
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_comicScroll->viewport() && event->type() == QEvent::Resize) {
        syncComicSize();
    }
    if (watched == m_say && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape && !m_replyMsgId.isEmpty()) {
            onCancelReply();
            return true;
        }
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

void MainWindow::appendChatLog(const QString &displayLine, const QString &nick,
                               const QString &text, const QString &msgid)
{
    auto *item = new QListWidgetItem(displayLine);
    if (!msgid.isEmpty()) {
        item->setData(kRoleMsgId, msgid);
        item->setData(kRoleNick, nick);
        item->setData(kRoleText, text);
        item->setToolTip(QStringLiteral("Right-click to reply · msgid %1").arg(msgid));
    }
    m_log->addItem(item);
    m_log->scrollToBottom();
}

void MainWindow::setReplyTarget(const QString &msgid, const QString &nick, const QString &text)
{
    m_replyMsgId = msgid.trimmed();
    m_replyNick = nick;
    m_replyText = text;
    updateReplyBanner();
    if (m_say) {
        m_say->setFocus();
        m_say->setPlaceholderText(QStringLiteral("Reply to %1… (Enter · Esc cancel)")
                                      .arg(nick.isEmpty() ? QStringLiteral("message") : nick));
    }
    statusBar()->showMessage(QStringLiteral("Replying to %1").arg(nick), 4000);
}

void MainWindow::clearReplyTarget()
{
    m_replyMsgId.clear();
    m_replyNick.clear();
    m_replyText.clear();
    updateReplyBanner();
    if (m_say) {
        m_say->setPlaceholderText(QStringLiteral("Say something… (Enter)"));
    }
}

void MainWindow::updateReplyBanner()
{
    if (!m_replyBanner || !m_replyLabel) {
        return;
    }
    if (m_replyMsgId.isEmpty()) {
        m_replyBanner->setVisible(false);
        return;
    }
    QString snippet = m_replyText;
    if (snippet.size() > 80) {
        snippet = snippet.left(77) + QStringLiteral("…");
    }
    snippet.replace(QLatin1Char('\n'), QLatin1Char(' '));
    m_replyLabel->setText(QStringLiteral("↩ Replying to %1: “%2”")
                              .arg(m_replyNick.isEmpty() ? QStringLiteral("?") : m_replyNick,
                                   snippet));
    m_replyBanner->setVisible(true);
}

void MainWindow::onCancelReply()
{
    clearReplyTarget();
    statusBar()->showMessage(QStringLiteral("Reply cancelled"), 2000);
}

void MainWindow::onLogContextMenu(const QPoint &pos)
{
    if (!m_log) {
        return;
    }
    QListWidgetItem *item = m_log->itemAt(pos);
    if (!item) {
        return;
    }
    const QString msgid = item->data(kRoleMsgId).toString();
    const QString nick = item->data(kRoleNick).toString();
    const QString text = item->data(kRoleText).toString();

    QMenu menu(this);
    QAction *replyAct = menu.addAction(QStringLiteral("Reply"));
    replyAct->setEnabled(!msgid.isEmpty() && m_irc && m_irc->isConnected());
    if (msgid.isEmpty()) {
        replyAct->setText(QStringLiteral("Reply (no msgid)"));
    } else if (!m_irc || !m_irc->isConnected()) {
        replyAct->setText(QStringLiteral("Reply (not connected)"));
    }
    QAction *copyAct = menu.addAction(QStringLiteral("Copy text"));
    QAction *chosen = menu.exec(m_log->mapToGlobal(pos));
    if (chosen == replyAct && !msgid.isEmpty()) {
        setReplyTarget(msgid, nick, text.isEmpty() ? item->text() : text);
    } else if (chosen == copyAct) {
        const QString t = text.isEmpty() ? item->text() : text;
        if (QClipboard *cb = QApplication::clipboard()) {
            cb->setText(t);
        }
        statusBar()->showMessage(QStringLiteral("Copied"), 1500);
    }
}

void MainWindow::updateAuthUi()
{
    const bool in = m_auth && m_auth->isLoggedIn();
    m_logoutBtn->setEnabled(in);
    if (in) {
        const FreeqSession &s = m_auth->session();
        // freeq identity: ATProto handle is the human name; DID is crypto id.
        // IRC nick is usually the same handle (nandi.uk) or mobile_nick_from_handle.
        const QString identity = s.displayIdentity();
        QString label = QStringLiteral("Signed in: %1").arg(identity);
        if (!s.did.isEmpty()) {
            label += QStringLiteral("  ·  %1").arg(s.did);
        }
        m_authLabel->setText(label);
        // Prefer full ATProto handle as IRC nick (matches freeq web/mobile users).
        m_nick->setText(s.ircNick());
        if (!s.handle.isEmpty()) {
            m_handle->setText(s.handle);
        } else if (!identity.isEmpty()) {
            m_handle->setText(identity);
        }
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
    appendLog(QStringLiteral("ATProto login OK — handle=%1 nick=%2 did=%3")
                  .arg(session.handle, session.nick, session.did));
    // Bind freeq identity → rpg.actor (by DID / live PDS if not in public index).
    // Defer: rememberAtprotoIdentity may block on nested QEventLoop HTTP; never
    // run that re-entrantly from FreeqAuth's OAuth socket path.
    if (m_comic) {
        const FreeqSession sess = session;
        QTimer::singleShot(0, this, [this, sess]() {
            if (!m_comic) {
                return;
            }
            if (!sess.handle.isEmpty()) {
                m_comic->rememberAtprotoIdentity(sess.handle, sess.did);
            }
            if (!sess.nick.isEmpty() && sess.nick != sess.handle) {
                m_comic->rememberAtprotoIdentity(sess.nick, sess.did);
            }
            if (!sess.displayIdentity().isEmpty() &&
                sess.displayIdentity() != sess.handle &&
                sess.displayIdentity() != sess.nick) {
                m_comic->rememberAtprotoIdentity(sess.displayIdentity(), sess.did);
            }
            statusBar()->showMessage(m_comic->statusLine(), 4000);
        });
    }
    statusBar()->showMessage(
        QStringLiteral("Logged in as %1 — hit Connect").arg(session.displayIdentity()), 8000);
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

    // When authenticated, force IRC nick from freeq/ATProto identity (handle).
    // Guests keep whatever is in the Nick field.
    QString nick;
    if (session.isValid() || session.hasWebToken()) {
        nick = session.ircNick();
        m_nick->setText(nick);
        appendLog(QStringLiteral("Using ATProto identity as nick: %1").arg(nick));
    } else {
        nick = m_nick->text().trimmed();
        if (nick.isEmpty()) {
            nick = QStringLiteral("ComicQt");
        }
    }

    if (session.hasWebToken()) {
        m_irc->setWebToken(session.webToken);
        m_irc->setAuthenticatedDidHint(session.did);
        appendLog(QStringLiteral("Connecting with freeq SASL web-token as %1…").arg(nick));
    } else {
        m_irc->setWebToken(QString());
        appendLog(QStringLiteral("Connecting as guest (no web-token)…"));
    }

    // rpg.actor: index IRC nick + handle → DID before chat starts.
    if (m_comic && !session.did.isEmpty()) {
        m_comic->rememberAtprotoIdentity(nick, session.did);
        if (!session.handle.isEmpty() &&
            session.handle.compare(nick, Qt::CaseInsensitive) != 0) {
            m_comic->rememberAtprotoIdentity(session.handle, session.did);
        }
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

    // Comic speaker label: prefer ATProto handle (rpg.actor / freeq identity).
    QString who;
    if (m_auth && m_auth->isLoggedIn()) {
        who = m_auth->session().displayIdentity();
    }
    if (who.isEmpty() && m_irc->isConnected()) {
        who = m_irc->nick();
    }
    if (who.isEmpty()) {
        who = QStringLiteral("you");
    }

    const QString replyId = m_replyMsgId;
    const QString replyNick = m_replyNick;
    const QString replyText = m_replyText;

    if (m_irc->isConnected()) {
        // Bind server msgid from echo-message so replies to us resolve.
        m_comic->noteOutgoingMessage(text, who);
        if (!replyId.isEmpty()) {
            m_irc->sendChannelReply(replyId, text);
        } else {
            m_irc->sendChannelMessage(text);
        }
    }

    if (!replyId.isEmpty()) {
        // Optimistic local reply frame (echo with +reply will also try; we
        // clear the draft so echo can still decorate if needed).
        appendLog(QStringLiteral("  ↩ %1: %2")
                      .arg(replyNick.isEmpty() ? QStringLiteral("?") : replyNick,
                           replyText));
        appendChatLog(QStringLiteral("%1 (reply): %2").arg(who, text), who, text,
                      /*msgid=*/QString());
        QHash<QString, QString> fakeTags;
        fakeTags.insert(QStringLiteral("+reply"), replyId);
        // Cache parent is already present; ensureRpg uses who.
        m_comic->addChatLine(text, who, fakeTags);
        clearReplyTarget();
    } else {
        appendChatLog(QStringLiteral("%1: %2").arg(who, text), who, text, QString());
        m_comic->addChatLine(text, who);
    }
    m_comic->trimToRecentPanels(kMaxComicHistory);
    m_say->clear();
    statusBar()->showMessage(m_comic->statusLine(), 4000);
    QTimer::singleShot(0, this, &MainWindow::syncComicSize);
}

void MainWindow::onChannelJoined(const QString &channel)
{
    appendLog(QStringLiteral("Joined %1 — loading history…").arg(channel));
    m_historyComicQueue.clear();
    m_historyComicTotal = 0;
}

void MainWindow::onHistoryBatchEnded()
{
    // Defer out of IrcClient::processLine so flush isn't re-entrant under TLS read.
    QTimer::singleShot(0, this, &MainWindow::flushHistoryComic);
}

void MainWindow::flushHistoryComic()
{
    if (m_flushingHistoryComic || !m_comic || m_historyComicQueue.isEmpty()) {
        return;
    }
    m_flushingHistoryComic = true;
    // Take ownership so nested event loops (rpg.actor HTTP) can't mutate mid-walk.
    const QList<HistoryComicLine> queue = std::move(m_historyComicQueue);
    m_historyComicQueue.clear();

    if (m_log) {
        m_log->setUpdatesEnabled(true);
        m_log->scrollToBottom();
    }

    // Comic strip: only the last N history messages (log already has the full set).
    // fastJoin=true: no blocking sprite/media HTTP — panels appear immediately.
    const int n = queue.size();
    const int total = std::max(m_historyComicTotal, n);
    m_historyComicTotal = 0;
    appendLog(QStringLiteral("Comic strip: showing last %1 of %2 history messages")
                  .arg(n)
                  .arg(total));
    for (int i = 0; i < n; ++i) {
        const HistoryComicLine &h = queue.at(i);
        m_comic->addChatLine(h.text, h.nick, h.tags, /*fastJoin=*/true);
    }
    m_comic->trimToRecentPanels(kMaxComicHistory);

    // Async rpg.actor upgrade for unique speakers (after UI is responsive).
    // Also include DIDs from account tags so live fetch can resolve by DID.
    QSet<QString> nicks;
    for (int i = 0; i < n; ++i) {
        const HistoryComicLine &h = queue.at(i);
        nicks.insert(h.nick);
        const QString did = h.tags.value(QStringLiteral("account"));
        if (!did.isEmpty() && did.startsWith(QLatin1String("did:"))) {
            nicks.insert(did);
            if (m_comic) {
                m_comic->rememberAtprotoIdentity(h.nick, did, /*preloadSprite=*/false);
            }
        }
    }
    int delay = 0;
    for (const QString &nick : nicks) {
        QTimer::singleShot(delay, this, [this, nick]() {
            if (m_comic) {
                m_comic->ensureRpgSpriteAsync(nick);
            }
        });
        delay += 40; // stagger so we don't spike the network all at once
    }

    m_flushingHistoryComic = false;
    QTimer::singleShot(0, this, &MainWindow::syncComicSize);
}

void MainWindow::onIrcMessage(const QString &nick, const QString &text,
                              const QHash<QString, QString> &tags, bool history)
{
    const QString replyTo = tags.value(QStringLiteral("+reply")).isEmpty()
                                ? tags.value(QStringLiteral("reply"))
                                : tags.value(QStringLiteral("+reply"));
    const bool isReply = !replyTo.isEmpty();
    const QString msgid = tags.value(QStringLiteral("msgid"));

    auto appendReplyLog = [&](const QString &speaker) {
        QString origNick;
        QString origText;
        const bool haveParent =
            m_comic && m_comic->lookupCachedMessage(replyTo, &origNick, &origText);
        if (haveParent) {
            // Parent line is right-clickable (reply to original).
            appendChatLog(QStringLiteral("  ↩ %1: %2").arg(origNick, origText), origNick,
                          origText, replyTo);
        } else {
            appendLog(QStringLiteral("  ↩ (original not in buffer)"));
        }
        appendChatLog(QStringLiteral("%1 (reply): %2").arg(speaker, text), speaker, text,
                      msgid);
    };

    auto bindAccountDid = [&](bool preloadSprite) {
        if (!m_comic) {
            return;
        }
        m_comic->rememberIrcMessage(text, nick, tags);
        const QString did = tags.value(QStringLiteral("account"));
        if (!did.isEmpty() && did.startsWith(QLatin1String("did:"))) {
            // History: never kick off per-line sprite fetches (join was very slow).
            m_comic->rememberAtprotoIdentity(nick, did, /*preloadSprite=*/preloadSprite);
        }
    };

    // Live echo of our own PRIVMSG (not history): don't re-draw — onSay already
    // did. Stamp msgid onto the last log line so right-click reply works.
    if (!history && m_irc && nick.compare(m_irc->nick(), Qt::CaseInsensitive) == 0) {
        bindAccountDid(/*preloadSprite=*/true);
        if (isReply) {
            if (!msgid.isEmpty() && m_log && m_log->count() > 0) {
                QListWidgetItem *last = m_log->item(m_log->count() - 1);
                if (last && last->data(kRoleMsgId).toString().isEmpty() &&
                    last->text().contains(QStringLiteral("(reply)"))) {
                    last->setData(kRoleMsgId, msgid);
                    last->setData(kRoleNick, nick);
                    last->setData(kRoleText, text);
                    last->setToolTip(
                        QStringLiteral("Right-click to reply · msgid %1").arg(msgid));
                }
            }
            return;
        }
        if (!msgid.isEmpty() && m_log && m_log->count() > 0) {
            QListWidgetItem *last = m_log->item(m_log->count() - 1);
            if (last && last->data(kRoleMsgId).toString().isEmpty()) {
                last->setData(kRoleMsgId, msgid);
                last->setData(kRoleNick, nick);
                last->setData(kRoleText, text);
                last->setToolTip(
                    QStringLiteral("Right-click to reply · msgid %1").arg(msgid));
            }
        }
        return;
    }

    // Cache + identity for every line (history and live).
    bindAccountDid(/*preloadSprite=*/!history);

    // Batch log widget updates during history flood (huge win on join).
    if (history && m_log && m_log->updatesEnabled()) {
        m_log->setUpdatesEnabled(false);
    }

    const QString mediaUrl = tags.value(QStringLiteral("media-url"));
    if (isReply) {
        appendReplyLog(nick);
    } else if (!mediaUrl.isEmpty()) {
        appendChatLog(QStringLiteral("%1: [image] %2").arg(nick, mediaUrl), nick, text,
                      msgid);
    } else {
        appendChatLog(QStringLiteral("%1: %2").arg(nick, text), nick, text, msgid);
    }

    // History: full log above; comic only gets last kMaxComicHistory (flush at batch end).
    if (history) {
        ++m_historyComicTotal;
        m_historyComicQueue.append(HistoryComicLine{nick, text, tags});
        // Only need the last N for the strip — drop older queued lines early.
        while (m_historyComicQueue.size() > kMaxComicHistory) {
            m_historyComicQueue.removeFirst();
        }
        if (m_historyComicTimer) {
            m_historyComicTimer->start(); // debounce if no BATCH trailer
        }
        return;
    }

    // Live traffic → comic strip (auto-trimmed to last N panels).
    m_comic->addChatLine(text, nick, tags, /*fastJoin=*/false);
    m_comic->trimToRecentPanels(kMaxComicHistory);
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
    clearReplyTarget();
    appendLog(QStringLiteral("IRC disconnected."));
}
