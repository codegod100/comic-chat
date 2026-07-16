// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "net/RpgActorClient.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace {
constexpr char kRegistryUrl[] = "https://rpg.actor/api/actors/full";
// rpg.actor sprite standard: 144×192, 3×4 grid, 48×48 cells.
// Row 0 = down, col 1 = idle (see https://rpg.actor/dev-guide).
constexpr int kDefaultCols = 3;
constexpr int kDefaultRows = 4;
constexpr int kIdleCol = 1;
constexpr int kDownRow = 0;
} // namespace

RpgActorClient::RpgActorClient(QObject *parent)
    : QObject(parent)
{
}

QString RpgActorClient::nickKey(const QString &s)
{
    return s.trimmed().toLower();
}

void RpgActorClient::refreshRegistry()
{
    if (m_registryLoading) {
        return;
    }
    m_registryLoading = true;

    QNetworkRequest req{QUrl(QString::fromUtf8(kRegistryUrl))};
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("comic-chat-qt/0.1 (+rpg.actor)"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_registryLoading = false;
        if (reply->error() != QNetworkReply::NoError) {
            return;
        }
        parseActorsJson(reply->readAll());
        m_registryReady = true;
        emit registryUpdated(m_byHandle.size());
    });
}

void RpgActorClient::parseActorsJson(const QByteArray &json)
{
    m_byHandle.clear();
    m_byDisplayName.clear();
    m_byLocalPart.clear();

    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) {
        return;
    }
    const QJsonArray actors = doc.object().value(QStringLiteral("actors")).toArray();

    QHash<QString, int> localPartCount;
    QHash<QString, int> displayCount;
    QHash<QString, QString> localPartFirst;
    QHash<QString, QString> displayFirst;

    for (const QJsonValue &v : actors) {
        if (!v.isObject()) {
            continue;
        }
        const QJsonObject o = v.toObject();
        RpgActorRef ref;
        ref.did = o.value(QStringLiteral("did")).toString();
        ref.handle = o.value(QStringLiteral("handle")).toString();
        if (ref.handle.isEmpty()) {
            ref.handle = o.value(QStringLiteral("displayHandle")).toString();
        }
        ref.displayName = o.value(QStringLiteral("displayName")).toString();

        const QJsonObject sprite = o.value(QStringLiteral("sprite")).toObject();
        if (!sprite.isEmpty()) {
            ref.sheetW = sprite.value(QStringLiteral("width")).toInt(144);
            ref.sheetH = sprite.value(QStringLiteral("height")).toInt(192);
            ref.columns = sprite.value(QStringLiteral("columns")).toInt(kDefaultCols);
            ref.rows = sprite.value(QStringLiteral("rows")).toInt(kDefaultRows);
            ref.spriteUrl = sprite.value(QStringLiteral("displayUrl")).toString();
            if (ref.spriteUrl.isEmpty()) {
                ref.spriteUrl = sprite.value(QStringLiteral("url")).toString();
            }
            ref.hasSprite = !ref.spriteUrl.isEmpty();
        }
        // Prefer hasSprite flag from API when present
        if (o.contains(QStringLiteral("hasSprite"))) {
            ref.hasSprite = o.value(QStringLiteral("hasSprite")).toBool() && !ref.spriteUrl.isEmpty();
        }

        if (ref.handle.isEmpty() && ref.did.isEmpty()) {
            continue;
        }

        const QString hKey = nickKey(ref.handle.isEmpty() ? ref.did : ref.handle);
        m_byHandle.insert(hKey, ref);

        if (!ref.displayName.isEmpty()) {
            const QString dKey = nickKey(ref.displayName);
            displayCount[dKey] = displayCount.value(dKey, 0) + 1;
            if (!displayFirst.contains(dKey)) {
                displayFirst.insert(dKey, hKey);
            }
        }

        const int dot = ref.handle.indexOf(QLatin1Char('.'));
        if (dot > 0) {
            const QString local = nickKey(ref.handle.left(dot));
            localPartCount[local] = localPartCount.value(local, 0) + 1;
            if (!localPartFirst.contains(local)) {
                localPartFirst.insert(local, hKey);
            }
        }
    }

    for (auto it = displayFirst.constBegin(); it != displayFirst.constEnd(); ++it) {
        if (displayCount.value(it.key(), 0) == 1) {
            m_byDisplayName.insert(it.key(), it.value());
        }
    }
    for (auto it = localPartFirst.constBegin(); it != localPartFirst.constEnd(); ++it) {
        if (localPartCount.value(it.key(), 0) == 1) {
            m_byLocalPart.insert(it.key(), it.value());
        }
    }
}

std::optional<RpgActorRef> RpgActorClient::lookupKey(const QString &key) const
{
    if (key.isEmpty()) {
        return std::nullopt;
    }
    auto it = m_byHandle.constFind(key);
    if (it != m_byHandle.constEnd()) {
        return it.value();
    }
    auto loc = m_byLocalPart.constFind(key);
    if (loc != m_byLocalPart.constEnd()) {
        auto h = m_byHandle.constFind(loc.value());
        if (h != m_byHandle.constEnd()) {
            return h.value();
        }
    }
    auto dn = m_byDisplayName.constFind(key);
    if (dn != m_byDisplayName.constEnd()) {
        auto h = m_byHandle.constFind(dn.value());
        if (h != m_byHandle.constEnd()) {
            return h.value();
        }
    }
    return std::nullopt;
}

std::optional<RpgActorRef> RpgActorClient::lookupNick(const QString &nick) const
{
    return lookupKey(nickKey(nick));
}

bool RpgActorClient::hasCachedSprite(const QString &nick) const
{
    return m_spriteCache.contains(nickKey(nick));
}

bool RpgActorClient::downloadBytes(const QUrl &url, QByteArray &out, int timeoutMs)
{
    if (!url.isValid()) {
        return false;
    }
    QNetworkRequest req{url};
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("comic-chat-qt/0.1 (+rpg.actor)"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_nam.get(req);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(std::max(500, timeoutMs));
    loop.exec();

    if (!reply->isFinished() || reply->error() != QNetworkReply::NoError) {
        reply->abort();
        reply->deleteLater();
        return false;
    }
    out = reply->readAll();
    reply->deleteLater();
    return !out.isEmpty();
}

ComicImage RpgActorClient::extractIdleFrame(const QImage &sheet, const RpgActorRef &ref) const
{
    ComicImage out;
    if (sheet.isNull()) {
        return out;
    }

    int cols = ref.columns > 0 ? ref.columns : kDefaultCols;
    int rows = ref.rows > 0 ? ref.rows : kDefaultRows;
    // Prefer standard 48×48 cells when sheet matches baseline 144×192.
    int cellW = sheet.width() / std::max(1, cols);
    int cellH = sheet.height() / std::max(1, rows);
    if (sheet.width() == 144 && sheet.height() == 192 && cols == 3 && rows == 4) {
        cellW = 48;
        cellH = 48;
    }

    const int col = (cols >= 3) ? kIdleCol : (cols / 2);
    const int row = kDownRow;
    const int x = col * cellW;
    const int y = row * cellH;
    if (x + cellW > sheet.width() || y + cellH > sheet.height()) {
        // Fallback: whole sheet scaled later
        out.setQImage(sheet);
        return out;
    }
    out.setQImage(sheet.copy(x, y, cellW, cellH));
    return out;
}

std::optional<ComicImage> RpgActorClient::spriteForNick(const QString &nick, int timeoutMs)
{
    const QString key = nickKey(nick);
    if (key.isEmpty()) {
        return std::nullopt;
    }

    auto cached = m_spriteCache.constFind(key);
    if (cached != m_spriteCache.constEnd() && !cached->isNull()) {
        return cached.value();
    }

    auto ref = lookupKey(key);
    if (!ref || !ref->hasSprite || ref->spriteUrl.isEmpty()) {
        return std::nullopt;
    }

    // Share by sprite URL
    auto urlHit = m_urlCache.constFind(ref->spriteUrl);
    if (urlHit != m_urlCache.constEnd() && !urlHit->isNull()) {
        m_spriteCache.insert(key, urlHit.value());
        return urlHit.value();
    }

    QByteArray bytes;
    if (!downloadBytes(QUrl(ref->spriteUrl), bytes, timeoutMs)) {
        // Try normalized endpoint as fallback (server re-encodes custom sheets)
        if (!ref->did.isEmpty()) {
            const QString norm = QStringLiteral("https://rpg.actor/api/sprite/normalized?did=%1")
                                     .arg(QString::fromUtf8(QUrl::toPercentEncoding(ref->did)));
            if (!downloadBytes(QUrl(norm), bytes, timeoutMs)) {
                return std::nullopt;
            }
        } else {
            return std::nullopt;
        }
    }

    QImage sheet;
    if (!sheet.loadFromData(bytes)) {
        return std::nullopt;
    }
    ComicImage frame = extractIdleFrame(sheet, *ref);
    if (frame.isNull()) {
        return std::nullopt;
    }
    m_urlCache.insert(ref->spriteUrl, frame);
    m_spriteCache.insert(key, frame);
    emit spriteReady(nick);
    return frame;
}
