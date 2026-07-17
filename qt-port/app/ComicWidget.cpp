// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "app/ComicWidget.h"

#include "engine/art_paths.h"
#include "engine/backdrop_qt.h"
#include "engine/pose.h"
#include "platform/QtCanvas.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPaintEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QSettings>
#include <QShowEvent>
#include <QUrl>

namespace {
bool contentTypeIsImage(const QString &ct)
{
    return ct.startsWith(QLatin1String("image/"), Qt::CaseInsensitive);
}
} // namespace

ComicWidget::ComicWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(260, 240);
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);

    connect(&m_rpg, &RpgActorClient::registryUpdated, this, [this](int n) {
        Q_UNUSED(n);
        update();
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

void ComicWidget::rememberAtprotoIdentity(const QString &handleOrNick, const QString &did)
{
    if (handleOrNick.isEmpty() || did.isEmpty()) {
        return;
    }
    m_rpg.rememberDidForNick(handleOrNick, did);
    // Preload sprite for this identity (registry or live PDS).
    ensureRpgSprite(handleOrNick);
}

void ComicWidget::ensureRpgSprite(const QString &nick)
{
    if (nick.isEmpty() || nick == QLatin1String("?")) {
        return;
    }
    if (m_scene.hasRpgSpriteForNick(nick.toStdString())) {
        return;
    }
    // Full walk sheet for left/right/down facing (rpg.actor 3×4 standard).
    // Registry first, then live PDS if not indexed yet (by handle or remembered DID).
    auto sheet = m_rpg.spriteSheetForNick(nick, 8000);
    if (!sheet || sheet->isNull()) {
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
    m_scene.setRpgSpriteForNick(nick.toStdString(), sheet->sheet, label.toStdString(),
                                /*isSheet=*/true, sheet->columns, sheet->rows);
    // Also index under handle if different, so bodyForNick(nick) and bodyForNick(handle) match.
    if (auto ref = m_rpg.lookupNick(nick)) {
        if (!ref->handle.isEmpty() &&
            ref->handle.compare(nick, Qt::CaseInsensitive) != 0) {
            m_scene.setRpgSpriteForNick(ref->handle.toStdString(), sheet->sheet,
                                        label.toStdString(), true, sheet->columns,
                                        sheet->rows);
        }
    }
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
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("comic-chat-qt/0.1 (inline-media)"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    // Cap download size via abort after big body — simple: rely on QImage decode fail.

    QNetworkReply *reply = m_nam.get(req);
    const QString who = nick;
    const QString cap = caption;
    connect(reply, &QNetworkReply::finished, this, [this, reply, who, cap, url]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            // Fall back to showing the URL as text so the message isn't lost.
            ensureAssetsLoaded();
            if (m_assetsOk) {
                ensureRpgSprite(who);
                const QString fallback =
                    cap.isEmpty() ? url.toString() : (cap + QLatin1Char(' ') + url.toString());
                m_scene.addLine(fallback.toStdString(), SM_SAY, who.toStdString());
                relayout();
                update();
            }
            return;
        }
        const QByteArray bytes = reply->readAll();
        // Soft size cap ~12 MiB
        if (bytes.size() > 12 * 1024 * 1024) {
            return;
        }
        ComicImage img;
        if (!img.loadFromData(reinterpret_cast<const unsigned char *>(bytes.constData()),
                              bytes.size())) {
            ensureAssetsLoaded();
            if (m_assetsOk) {
                ensureRpgSprite(who);
                m_scene.addLine((cap.isEmpty() ? url.toString() : cap).toStdString(), SM_SAY,
                                who.toStdString());
                relayout();
                update();
            }
            return;
        }
        ensureAssetsLoaded();
        if (!m_assetsOk) {
            return;
        }
        ensureRpgSprite(who);
        m_scene.addImageLine(img, cap.toStdString(), SM_SAY, who.toStdString());
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
                                      const QHash<QString, QString> &tags)
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
    ensureRpgSprite(who);

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
        // Load sprites for both speakers before laying out bodies.
        if (origNick != QLatin1String("?")) {
            ensureRpgSprite(origNick);
        }
        ensureRpgSprite(who);
        m_scene.addReplyExchange(origNick.toStdString(), origText.toStdString(),
                                 who.toStdString(), text.toStdString(), SM_SAY);
        relayout();
        update();

        // If reply is primarily an image, also attach media after the frame.
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
        QString caption = alt;
        if (caption.isEmpty()) {
            caption = stripUrls(text);
        }
        // Don't put the raw URL in the caption
        if (caption.contains(QLatin1String("http://"), Qt::CaseInsensitive) ||
            caption.contains(QLatin1String("https://"), Qt::CaseInsensitive)) {
            caption = stripUrls(caption);
        }
        fetchAndShowImage(QUrl(mediaUrl), caption, who);
        return;
    }

    // Normal text
    m_scene.addLine(text.toStdString(), SM_SAY, who.toStdString());
    relayout();
    update();
}

void ComicWidget::addChatLine(const QString &text, const QString &nick)
{
    handlePossiblyMedia(text, nick, {});
}

void ComicWidget::addChatLine(const QString &text, const QString &nick,
                              const QHash<QString, QString> &tags)
{
    handlePossiblyMedia(text, nick, tags);
}

void ComicWidget::clearPanels()
{
    m_scene.clear();
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

    QPainter painter(this);
    painter.fillRect(rect(), QColor(0xe8, 0xe4, 0xdc));

    QtCanvas canvas(&painter);
    canvas.setLogicalScale(1.0, 1.0);

    const int statusH = 20;
    RECT dest{m_margin, m_margin, width() - m_margin, height() - m_margin - statusH};
    m_scene.draw(&canvas, dest);

    canvas.setFont("Sans Serif", 10, false);
    canvas.setPen(CanvasColor::rgb(50, 50, 50), 1);
    canvas.drawText(m_margin, height() - 8, statusLine().toStdString());
}
