// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "app/ComicWidget.h"

#include "engine/art_paths.h"
#include "engine/backdrop_qt.h"
#include "engine/pose.h"
#include "platform/QtCanvas.h"

#include <QApplication>
#include <QDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QShowEvent>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>

namespace {
bool contentTypeIsImage(const QString &ct)
{
    return ct.startsWith(QLatin1String("image/"), Qt::CaseInsensitive);
}

class ImageLightboxDialog : public QDialog {
public:
    explicit ImageLightboxDialog(QWidget *parent, const QImage &image)
        : QDialog(parent, Qt::Dialog | Qt::WindowCloseButtonHint)
    {
        setWindowTitle(QObject::tr("Image preview"));
        setModal(true);

        auto *closeBtn = new QPushButton(QStringLiteral("×"), this);
        closeBtn->setToolTip(QObject::tr("Close"));
        closeBtn->setFlat(true);
        closeBtn->setCursor(Qt::PointingHandCursor);
        closeBtn->setStyleSheet(QStringLiteral(
            "QPushButton { color: #ffffff; background: #333333; "
            "border: none; font-weight: bold; font-size: 16px; padding: 4px 10px; }"
            "QPushButton:hover { background: #555555; }"));
        connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

        auto *topBar = new QHBoxLayout();
        topBar->addStretch();
        topBar->addWidget(closeBtn);
        topBar->setContentsMargins(6, 6, 6, 0);

        auto *imgLabel = new QLabel(this);
        imgLabel->setAlignment(Qt::AlignCenter);
        imgLabel->setStyleSheet(QStringLiteral("background: #1a1a1a;"));
        imgLabel->setCursor(Qt::PointingHandCursor);

        const QScreen *screen = QGuiApplication::primaryScreen();
        const QRect avail = screen ? screen->availableGeometry() : QRect();
        const QSize maxSize = avail.isEmpty()
                                  ? QSize(1200, 800)
                                  : QSize(avail.width() * 9 / 10, avail.height() * 9 / 10);

        QSize outSize = image.size();
        if (outSize.width() > maxSize.width() || outSize.height() > maxSize.height()) {
            outSize.scale(maxSize, Qt::KeepAspectRatio);
        }
        if (!outSize.isValid() || outSize.isEmpty()) {
            outSize = QSize(320, 240);
        }

        const QImage scaled =
            image.scaled(outSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        imgLabel->setPixmap(QPixmap::fromImage(scaled));
        imgLabel->setFixedSize(scaled.size());

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addLayout(topBar);
        layout->addWidget(imgLabel, 1, Qt::AlignCenter);

        setFixedSize(layout->sizeHint());
        if (!avail.isEmpty()) {
            move(QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter, size(), avail).topLeft());
        } else if (parent) {
            const QPoint parentCenter = parent->mapToGlobal(QPoint(parent->width() / 2, parent->height() / 2));
            move(parentCenter - QPoint(width() / 2, height() / 2));
        }
    }
};
} // namespace

ComicWidget::ComicWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(260, 240);
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
    setMouseTracking(true);

    connect(&m_rpg, &RpgActorClient::registryUpdated, this, [this](int n) {
        Q_UNUSED(n);
        // Registry often finishes after history panels are drawn — upgrade cast → rpg.
        for (const auto &nick : m_scene.nicksOnStage()) {
            ensureRpgSpriteAsync(QString::fromStdString(nick));
        }
        update();
    });
    connect(&m_rpg, &RpgActorClient::spriteReady, this, [this](const QString &nick) {
        // Sheet may have been cached by a parallel path; apply + refresh bodies.
        if (auto sheet = m_rpg.cachedSheetForNick(nick)) {
            applyRpgSheet(nick, *sheet);
            relayout();
            update();
        }
    });
    m_rpg.refreshRegistry();
}

void ComicWidget::setViewportHeight(int h)
{
    h = std::max(200, h);
    if (h == m_viewportH) {
        return;
    }
    m_viewportH = h;
    updateGeometry();
}

int ComicWidget::contentHeight() const
{
    const int usable = std::max(180, m_viewportH - 2 * m_margin - 22);
    return m_scene.contentHeightForHeight(usable) + 2 * m_margin + 22;
}

int ComicWidget::contentWidth() const
{
    const int usable = std::max(180, m_viewportH - 2 * m_margin - 22);
    return m_scene.contentWidthForHeight(usable) + 2 * m_margin;
}

void ComicWidget::rememberAtprotoIdentity(const QString &handleOrNick, const QString &did,
                                          bool preloadSprite)
{
    if (handleOrNick.isEmpty() || did.isEmpty()) {
        return;
    }
    m_rpg.rememberDidForNick(handleOrNick, did);
    if (preloadSprite) {
        ensureRpgSpriteAsync(handleOrNick);
    }
}

void ComicWidget::applyRpgSheet(const QString &nick, const RpgSpriteSheet &sheet)
{
    if (nick.isEmpty() || sheet.isNull()) {
        return;
    }
    QString label = nick;
    if (auto ref = m_rpg.lookupNick(nick)) {
        if (!ref->displayName.isEmpty()) {
            label = ref->displayName;
        } else if (!ref->handle.isEmpty()) {
            label = ref->handle;
        }
    }
    m_scene.setRpgSpriteForNick(nick.toStdString(), sheet.sheet, label.toStdString(),
                                /*isSheet=*/true, sheet.columns, sheet.rows);
    if (auto ref = m_rpg.lookupNick(nick)) {
        if (!ref->handle.isEmpty() &&
            ref->handle.compare(nick, Qt::CaseInsensitive) != 0) {
            m_scene.setRpgSpriteForNick(ref->handle.toStdString(), sheet.sheet,
                                        label.toStdString(), true, sheet.columns,
                                        sheet.rows);
            m_scene.refreshBodiesForNick(ref->handle.toStdString());
        }
    }
    // Upgrade already-drawn panels that still show cast placeholders.
    m_scene.refreshBodiesForNick(nick.toStdString());
}

void ComicWidget::ensureRpgSprite(const QString &nick, bool blocking)
{
    if (nick.isEmpty() || nick == QLatin1String("?")) {
        return;
    }
    if (m_scene.hasRpgSpriteForNick(nick.toStdString())) {
        return;
    }
    // Instant: already-downloaded sheet.
    if (auto cached = m_rpg.cachedSheetForNick(nick)) {
        applyRpgSheet(nick, *cached);
        return;
    }
    if (!blocking) {
        ensureRpgSpriteAsync(nick);
        return;
    }
    // Blocking only for interactive single messages (not history join).
    auto sheet = m_rpg.spriteSheetForNick(nick, 4000, /*allowLiveFetch=*/true);
    if (sheet && !sheet->isNull()) {
        applyRpgSheet(nick, *sheet);
    }
}

void ComicWidget::ensureRpgSpriteAsync(const QString &nick)
{
    if (nick.isEmpty() || nick == QLatin1String("?")) {
        return;
    }
    if (m_scene.hasRpgSpriteForNick(nick.toStdString())) {
        return;
    }
    if (auto cached = m_rpg.cachedSheetForNick(nick)) {
        applyRpgSheet(nick, *cached);
        update();
        return;
    }
    const QString key = nick.trimmed().toLower();
    if (m_rpgFetchInFlight.contains(key)) {
        return;
    }
    m_rpgFetchInFlight.insert(key);
    // Off the IRC/TLS stack: nested QEventLoop is OK once processLine has returned.
    QTimer::singleShot(0, this, [this, nick, key]() {
        if (m_scene.hasRpgSpriteForNick(nick.toStdString())) {
            m_rpgFetchInFlight.remove(key);
            return;
        }
        auto sheet = m_rpg.spriteSheetForNick(nick, 4000, /*allowLiveFetch=*/true);
        m_rpgFetchInFlight.remove(key);
        if (sheet && !sheet->isNull()) {
            applyRpgSheet(nick, *sheet);
            update();
            emit contentResized();
        }
    });
}

bool ComicWidget::looksLikeImageUrl(const QUrl &url)
{
    if (!url.isValid() || (url.scheme() != QLatin1String("http") &&
                           url.scheme() != QLatin1String("https"))) {
        return false;
    }
    const QString path = url.path().toLower();
    static const char *exts[] = {".png", ".jpg", ".jpeg", ".gif", ".webp", ".bmp",
                                 ".avif", ".svg"};
    for (const char *e : exts) {
        if (path.endsWith(QLatin1String(e))) {
            return true;
        }
    }
    // freeq / common CDN blob paths often omit extensions
    if (path.contains(QLatin1String("/blob")) || path.contains(QLatin1String("/media")) ||
        path.contains(QLatin1String("/img")) || path.contains(QLatin1String("/image")) ||
        path.contains(QLatin1String("getblob")) || path.contains(QLatin1String("/xrpc/"))) {
        return true;
    }
    return false;
}

QString ComicWidget::extractImageUrl(const QString &text)
{
    // Prefer first http(s) URL that looks like an image.
    static const QRegularExpression re(
        QStringLiteral(R"((https?://[^\s<>"'\]]+))"),
        QRegularExpression::CaseInsensitiveOption);
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const QString raw = it.next().captured(1);
        // Trim trailing punctuation common in chat
        QString u = raw;
        while (!u.isEmpty() && QStringLiteral(".,);]}>\"").contains(u.back())) {
            u.chop(1);
        }
        const QUrl url(u);
        if (looksLikeImageUrl(url)) {
            return u;
        }
    }
    // If the whole message is a single URL, treat as image candidate.
    const QString t = text.trimmed();
    const QUrl only(t);
    if (only.isValid() && (only.scheme() == QLatin1String("http") ||
                           only.scheme() == QLatin1String("https"))) {
        return t;
    }
    return {};
}

QString ComicWidget::stripUrls(const QString &text)
{
    static const QRegularExpression re(
        QStringLiteral(R"((https?://[^\s<>"'\]]+))"),
        QRegularExpression::CaseInsensitiveOption);
    QString out = text;
    out.replace(re, QString());
    return out.simplified();
}

void ComicWidget::fetchAndShowImage(const QUrl &url, const QString &caption, const QString &nick)
{
    if (!url.isValid()) {
        return;
    }
    // One panel per (url, nick) — history/live can otherwise fire the same fetch
    // multiple times and stamp the photo onto many frames.
    const QString who = nick.isEmpty() ? QStringLiteral("you") : nick;
    const QString flightKey =
        url.toString(QUrl::FullyEncoded) + QLatin1Char('\n') + who.toLower();
    if (m_imageFetchInFlight.contains(flightKey) || m_imagesShown.contains(flightKey)) {
        return;
    }
    m_imageFetchInFlight.insert(flightKey);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("comic-chat-qt/0.1 (inline-media)"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_nam.get(req);
    const QString cap = caption;
    connect(reply, &QNetworkReply::finished, this, [this, reply, who, cap, url, flightKey]() {
        reply->deleteLater();
        m_imageFetchInFlight.remove(flightKey);

        auto finishText = [&](const QString &line) {
            if (m_imagesShown.contains(flightKey)) {
                return;
            }
            ensureAssetsLoaded();
            if (!m_assetsOk) {
                return;
            }
            ensureRpgSprite(who, /*blocking=*/false);
            m_scene.addLine(line.toStdString(), SM_SAY, who.toStdString());
            m_scene.trimToMaxPanels(kMaxComicPanels);
            m_imagesShown.insert(flightKey);
            while (m_imagesShown.size() > 64) {
                m_imagesShown.erase(m_imagesShown.begin());
            }
            relayout();
            update();
        };

        if (reply->error() != QNetworkReply::NoError) {
            const QString fallback =
                cap.isEmpty() ? url.toString() : (cap + QLatin1Char(' ') + url.toString());
            finishText(fallback);
            return;
        }
        const QByteArray bytes = reply->readAll();
        if (bytes.size() > 12 * 1024 * 1024) {
            return;
        }
        ComicImage img;
        if (!img.loadFromData(reinterpret_cast<const unsigned char *>(bytes.constData()),
                              bytes.size())) {
            finishText(cap.isEmpty() ? url.toString() : cap);
            return;
        }
        if (m_imagesShown.contains(flightKey)) {
            return;
        }
        ensureAssetsLoaded();
        if (!m_assetsOk) {
            return;
        }
        ensureRpgSprite(who, /*blocking=*/false);
        m_scene.addImageLine(img, cap.toStdString(), SM_SAY, who.toStdString());
        m_scene.trimToMaxPanels(kMaxComicPanels);
        m_imagesShown.insert(flightKey);
        while (m_imagesShown.size() > 64) {
            m_imagesShown.erase(m_imagesShown.begin());
        }
        relayout();
        update();
    });
}

QString ComicWidget::messageId(const QHash<QString, QString> &tags)
{
    // freeq / IRCv3: server-assigned msgid (sometimes Message-ID style).
    if (tags.contains(QStringLiteral("msgid"))) {
        return tags.value(QStringLiteral("msgid"));
    }
    if (tags.contains(QStringLiteral("Message-ID"))) {
        return tags.value(QStringLiteral("Message-ID"));
    }
    return {};
}

QString ComicWidget::replyParentId(const QHash<QString, QString> &tags)
{
    // freeq-sdk client.reply() → +reply=<parent msgid>
    // Also accept draft/reply and un-plus'd variants seen on the wire.
    static const char *keys[] = {"+reply", "reply", "+draft/reply", "draft/reply",
                                 "in-reply-to"};
    for (const char *k : keys) {
        const QString v = tags.value(QString::fromLatin1(k));
        if (!v.isEmpty()) {
            return v;
        }
    }
    return {};
}

void ComicWidget::cacheMessage(const QString &msgid, const QString &nick, const QString &text)
{
    if (msgid.isEmpty() || text.isEmpty()) {
        return;
    }
    if (!m_msgById.contains(msgid)) {
        m_msgIdOrder.append(msgid);
        while (m_msgIdOrder.size() > kMaxCachedMsgs) {
            const QString old = m_msgIdOrder.takeFirst();
            m_msgById.remove(old);
        }
    }
    m_msgById.insert(msgid, CachedChatLine{nick, text});
}

void ComicWidget::noteOutgoingMessage(const QString &text, const QString &nick)
{
    const QString who = nick.isEmpty() ? QStringLiteral("you") : nick;
    const QString t = text.trimmed();
    if (t.isEmpty()) {
        return;
    }
    m_pendingOut.append(PendingOut{who, t});
    while (m_pendingOut.size() > 50) {
        m_pendingOut.removeFirst();
    }
}

bool ComicWidget::hasCachedMessage(const QString &msgid) const
{
    return !msgid.isEmpty() && m_msgById.contains(msgid);
}

bool ComicWidget::lookupCachedMessage(const QString &msgid, QString *nickOut,
                                      QString *textOut) const
{
    if (msgid.isEmpty()) {
        return false;
    }
    auto it = m_msgById.constFind(msgid);
    if (it == m_msgById.constEnd()) {
        for (auto i = m_msgById.constBegin(); i != m_msgById.constEnd(); ++i) {
            if (i.key().compare(msgid, Qt::CaseInsensitive) == 0) {
                it = i;
                break;
            }
        }
    }
    if (it == m_msgById.constEnd()) {
        return false;
    }
    if (nickOut) {
        *nickOut = it->nick;
    }
    if (textOut) {
        *textOut = it->text;
    }
    return true;
}

void ComicWidget::rememberIrcMessage(const QString &text, const QString &nick,
                                     const QHash<QString, QString> &tags)
{
    const QString who = nick.isEmpty() ? QStringLiteral("you") : nick;
    const QString mid = messageId(tags);
    cacheMessage(mid, who, text);

    // Bind local optimistic sends to server msgid (echo-message).
    if (!mid.isEmpty() && !m_pendingOut.isEmpty()) {
        const QString t = text.trimmed();
        for (int i = 0; i < m_pendingOut.size(); ++i) {
            if (m_pendingOut.at(i).text == t &&
                m_pendingOut.at(i).nick.compare(who, Qt::CaseInsensitive) == 0) {
                m_pendingOut.removeAt(i);
                break;
            }
        }
    }
}

void ComicWidget::handlePossiblyMedia(const QString &text, const QString &nick,
                                      const QHash<QString, QString> &tags, bool fastJoin)
{
    ensureAssetsLoaded();
    if (!m_assetsOk) {
        update();
        return;
    }
    const QString who = nick.isEmpty() ? QStringLiteral("you") : nick;

    // freeq account-tag: account=did:plc:… — best key for rpg.actor live fetch.
    const QString accountDid = tags.value(QStringLiteral("account"));
    if (!accountDid.isEmpty() && accountDid.startsWith(QLatin1String("did:"))) {
        m_rpg.rememberDidForNick(who, accountDid);
    }
    // History join: never block on HTTP. Live: async upgrade (cache hit is instant).
    ensureRpgSprite(who, /*blocking=*/false);

    // freeq: remember every line by msgid so later +reply can re-stage the original.
    const QString msgid = messageId(tags);
    cacheMessage(msgid, who, text);

    // Threaded reply → dedicated panel with original + reply (freeq ReplyBadge UX).
    const QString parentId = replyParentId(tags);
    if (!parentId.isEmpty()) {
        QString origNick;
        QString origText;
        if (!lookupCachedMessage(parentId, &origNick, &origText) || origText.isEmpty()) {
            origNick = QStringLiteral("?");
            origText = QStringLiteral("(original not in buffer)");
        }
        if (origNick != QLatin1String("?")) {
            ensureRpgSprite(origNick, /*blocking=*/false);
        }
        ensureRpgSprite(who, /*blocking=*/false);
        m_scene.addReplyExchange(origNick.toStdString(), origText.toStdString(),
                                 who.toStdString(), text.toStdString(), SM_SAY);
        m_scene.trimToMaxPanels(kMaxComicPanels);
        relayout();
        update();

        // Image replies: always fetch (async QNetworkReply — non-blocking).
        QString mediaUrl = tags.value(QStringLiteral("media-url"));
        const QString contentType = tags.value(QStringLiteral("content-type"));
        bool isImage = false;
        if (!mediaUrl.isEmpty()) {
            isImage = contentType.isEmpty() || contentTypeIsImage(contentType) ||
                      looksLikeImageUrl(QUrl(mediaUrl));
        }
        if (!isImage) {
            const QString found = extractImageUrl(text);
            if (!found.isEmpty() && looksLikeImageUrl(QUrl(found))) {
                mediaUrl = found;
                isImage = true;
            }
        }
        if (isImage && !mediaUrl.isEmpty()) {
            QString alt = tags.value(QStringLiteral("media-alt"));
            if (alt.isEmpty()) {
                alt = stripUrls(text);
            }
            fetchAndShowImage(QUrl(mediaUrl), alt, who);
        }
        return;
    }

    // freeq IRCv3 tags (see freeq-sdk media.rs)
    QString mediaUrl = tags.value(QStringLiteral("media-url"));
    const QString contentType = tags.value(QStringLiteral("content-type"));
    QString alt = tags.value(QStringLiteral("media-alt"));

    bool isImage = false;
    if (!mediaUrl.isEmpty()) {
        isImage = contentType.isEmpty() || contentTypeIsImage(contentType) ||
                  looksLikeImageUrl(QUrl(mediaUrl));
    }

    if (!isImage) {
        // Bare URL in body (plain clients / no tags)
        const QString found = extractImageUrl(text);
        if (!found.isEmpty() && looksLikeImageUrl(QUrl(found))) {
            mediaUrl = found;
            isImage = true;
            if (alt.isEmpty()) {
                alt = stripUrls(text);
            }
        }
    }

    if (isImage && !mediaUrl.isEmpty()) {
        // Always async-fetch images (join + live). QNetworkReply does not block IRC.
        QString caption = alt;
        if (caption.isEmpty()) {
            caption = stripUrls(text);
        }
        if (caption.contains(QLatin1String("http://"), Qt::CaseInsensitive) ||
            caption.contains(QLatin1String("https://"), Qt::CaseInsensitive)) {
            caption = stripUrls(caption);
        }
        // Ensure speaker is on stage with a temporary text panel only if no image
        // yet — fetchAndShowImage adds the photo panel when ready. For join, still
        // kick the download so history media appears shortly after load.
        fetchAndShowImage(QUrl(mediaUrl), caption, who);
        return;
    }

    // Normal text
    m_scene.addLine(text.toStdString(), SM_SAY, who.toStdString());
    m_scene.trimToMaxPanels(kMaxComicPanels);
    relayout();
    update();
}

void ComicWidget::addChatLine(const QString &text, const QString &nick)
{
    handlePossiblyMedia(text, nick, {}, false);
}

void ComicWidget::addChatLine(const QString &text, const QString &nick,
                              const QHash<QString, QString> &tags, bool fastJoin)
{
    handlePossiblyMedia(text, nick, tags, fastJoin);
}

void ComicWidget::clearPanels()
{
    m_scene.clear();
    relayout();
    update();
}

void ComicWidget::trimToRecentPanels(int maxPanels)
{
    m_scene.trimToMaxPanels(maxPanels);
    relayout();
    update();
}

QStringList ComicWidget::availableRooms() const
{
    std::string dir = m_backdropDir.toStdString();
    if (dir.empty()) {
        dir = resolveArtPaths().backdrop;
    }
    QStringList out;
    for (const auto &n : ListBackdropNames(dir)) {
        out << QString::fromStdString(n);
    }
    return out;
}

bool ComicWidget::setRoom(const QString &baseName)
{
    const QString want = baseName.trimmed();
    if (want.isEmpty()) {
        return false;
    }

    ensureAssetsLoaded();
    if (m_backdropDir.isEmpty()) {
        return false;
    }

    if (want.compare(m_roomName, Qt::CaseInsensitive) == 0 && m_assetsOk) {
        update(); // still refresh so empty-state preview paints
        return true;
    }

    ComicImage backdrop;
    if (!LoadBackdropImage(m_backdropDir.toStdString(), want.toStdString(), backdrop)) {
        m_loadError = QStringLiteral("Could not load room “%1”").arg(want);
        update();
        return false;
    }

    m_roomName = want;
    QSettings s;
    s.setValue(QStringLiteral("comic/room"), m_roomName);

    if (m_assetsOk) {
        m_scene.setBackdrop(backdrop);
        m_loadError.clear();
    } else {
        // Avatars not ready yet — room choice is remembered for ensureAssetsLoaded.
    }
    update();
    emit roomChanged(m_roomName);
    return true;
}

QPixmap ComicWidget::roomThumbnail(const QString &baseName, const QSize &size) const
{
    if (baseName.trimmed().isEmpty() || !size.isValid() || size.isEmpty()) {
        return {};
    }
    std::string dir = m_backdropDir.toStdString();
    if (dir.empty()) {
        dir = resolveArtPaths().backdrop;
    }
    ComicImage img;
    if (!LoadBackdropImage(dir, baseName.toStdString(), img) || img.isNull()) {
        return {};
    }
    const QImage scaled =
        img.qimage().scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const QImage cropped = scaled.copy((scaled.width() - size.width()) / 2,
                                       (scaled.height() - size.height()) / 2, size.width(),
                                       size.height());
    return QPixmap::fromImage(cropped);
}

QString ComicWidget::statusLine() const
{
    if (!m_loadError.isEmpty()) {
        return m_loadError;
    }
    QString s = QString::fromStdString(m_scene.status());
    if (!m_roomName.isEmpty()) {
        s += QStringLiteral(" | room: %1").arg(m_roomName);
    }
    if (m_rpg.registryReady()) {
        s += QStringLiteral(" | rpg.actor: %1").arg(m_rpg.actorCount());
    } else {
        s += QStringLiteral(" | rpg.actor: …");
    }
    return s;
}

QSize ComicWidget::sizeHint() const
{
    return QSize(contentWidth(), contentHeight());
}

QSize ComicWidget::minimumSizeHint() const
{
    return QSize(260, 240);
}

void ComicWidget::relayout()
{
    updateGeometry();
    emit contentResized();
}

void ComicWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    ensureAssetsLoaded();
    relayout();
}

void ComicWidget::mousePressEvent(QMouseEvent *event)
{
    if (!event || event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    for (const auto &ci : m_clickableImages) {
        if (ci.screenRect.contains(event->pos())) {
            ImageLightboxDialog dlg(this, ci.fullImage);
            dlg.exec();
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void ComicWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!event) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    bool over = false;
    for (const auto &ci : m_clickableImages) {
        if (ci.screenRect.contains(event->pos())) {
            over = true;
            break;
        }
    }
    setCursor(over ? Qt::PointingHandCursor : Qt::ArrowCursor);
    QWidget::mouseMoveEvent(event);
}

void ComicWidget::ensureAssetsLoaded()
{
    if (m_assetsTried) {
        return;
    }
    m_assetsTried = true;

    const ArtPaths art = resolveArtPaths();
    setAvatarArtDir(art.avatars);
    m_backdropDir = QString::fromStdString(art.backdrop);

    // Prefer last-chosen room, then classic default, then first on disk.
    QSettings s;
    QString room = s.value(QStringLiteral("comic/room"), m_roomName).toString().trimmed();
    if (room.isEmpty()) {
        room = QStringLiteral("room8bs");
    }
    const QStringList rooms = availableRooms();
    if (!rooms.isEmpty()) {
        bool found = false;
        for (const QString &r : rooms) {
            if (r.compare(room, Qt::CaseInsensitive) == 0) {
                room = r;
                found = true;
                break;
            }
        }
        if (!found) {
            // Prefer room8bs if present, else first file.
            room = rooms.first();
            for (const QString &r : rooms) {
                if (r.compare(QStringLiteral("room8bs"), Qt::CaseInsensitive) == 0) {
                    room = r;
                    break;
                }
            }
        }
    }
    m_roomName = room;

    ComicImage backdrop;
    if (!LoadBackdropImage(art.backdrop, m_roomName.toStdString(), backdrop)) {
        m_loadError = QStringLiteral("No backdrop in %1")
                          .arg(QString::fromStdString(art.backdrop));
        return;
    }

    std::vector<LoadedAvatar> cast = LoadAllAvatars();
    if (cast.empty()) {
        m_loadError = QStringLiteral("No avatars in %1")
                          .arg(QString::fromStdString(art.avatars));
        return;
    }

    int usable = 0;
    for (const auto &av : cast) {
        if (av.type == AT_COMPLEX) {
            if (!av.faces.empty() && !av.torsos.empty() &&
                GetPoseFromID(av.faces.front().poseID) &&
                GetPoseFromID(av.torsos.front().poseID)) {
                ++usable;
            }
        } else {
            const USHORT id =
                !av.bodies.empty()
                    ? av.bodies.front().poseID
                    : (!av.bodyPoses.empty() ? av.bodyPoses.front() : av.iconPose);
            if (GetPoseFromID(id)) {
                ++usable;
            }
        }
    }
    if (usable == 0) {
        m_loadError = QStringLiteral("No drawable avatars in %1")
                          .arg(QString::fromStdString(art.avatars));
        return;
    }

    m_scene.setArt(std::move(cast), backdrop);
    m_assetsOk = true;
    m_loadError.clear();
}

void ComicWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    ensureAssetsLoaded();

    m_clickableImages.clear();

    QPainter painter(this);
    painter.fillRect(rect(), QColor(0xe8, 0xe4, 0xdc));

    QtCanvas canvas(&painter);
    canvas.setLogicalScale(1.0, 1.0);

    const int statusH = 20;
    RECT dest{m_margin, m_margin, width() - m_margin, height() - m_margin - statusH};
    m_scene.draw(&canvas, dest);

    // Build hit-test targets for inline image previews.
    const int contentH = std::max(1, dest.bottom - dest.top);
    const int side = m_scene.panelSideForHeight(contentH);
    const int y0 = dest.top + std::max(0, (contentH - side) / 2);
    constexpr int kGap = 14;
    const auto &panels = m_scene.panels();
    for (int pi = 0; pi < static_cast<int>(panels.size()); ++pi) {
        const RECT pr{dest.left + pi * (side + kGap), y0,
                      dest.left + pi * (side + kGap) + side, y0 + side};
        const double sx = double(pr.right - pr.left) / m_scene.unitWidth();
        const double sy = double(pr.bottom - pr.top) / m_scene.unitHeight();
        for (const auto &bal : panels[pi].balloons) {
            if (!bal.hasImage()) {
                continue;
            }
            const int screenLeft = pr.left + static_cast<int>(std::lround(bal.imageBox.left * sx));
            const int screenRight = pr.left + static_cast<int>(std::lround(bal.imageBox.right * sx));
            const int screenTop = pr.top - static_cast<int>(std::lround(bal.imageBox.top * sy));
            const int screenBottom =
                pr.top - static_cast<int>(std::lround(bal.imageBox.bottom * sy));
            const QRect r(screenLeft, screenTop, screenRight - screenLeft,
                          screenBottom - screenTop);
            m_clickableImages.push_back({r, bal.image.qimage()});
        }
    }

    canvas.setFont("Sans Serif", 10, false);
    canvas.setPen(CanvasColor::rgb(50, 50, 50), 1);
    canvas.drawText(m_margin, height() - 8, statusLine().toStdString());
}
