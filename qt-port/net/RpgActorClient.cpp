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
#include <QUrlQuery>

namespace {
constexpr char kRegistryUrl[] = "https://rpg.actor/api/actors/full";
constexpr char kBskyResolve[] =
    "https://public.api.bsky.app/xrpc/com.atproto.identity.resolveHandle";
// rpg.actor sprite standard: 144×192, 3×4 grid, 48×48 cells.
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
    QString k = s.trimmed().toLower();
    if (k.startsWith(QLatin1Char('@'))) {
        k = k.mid(1);
    }
    return k;
}

void RpgActorClient::rememberDidForNick(const QString &nick, const QString &did)
{
    const QString k = nickKey(nick);
    const QString d = did.trimmed();
    if (k.isEmpty() || d.isEmpty()) {
        return;
    }
    m_nickToDid.insert(k, d);
    // Also index DID → this nick so lookup by either works once we have a ref.
    if (!m_byDid.contains(nickKey(d))) {
        m_byDid.insert(nickKey(d), k);
    }
}

void RpgActorClient::cacheRef(const RpgActorRef &ref)
{
    if (ref.handle.isEmpty() && ref.did.isEmpty()) {
        return;
    }
    const QString hKey = nickKey(ref.handle.isEmpty() ? ref.did : ref.handle);
    m_byHandle.insert(hKey, ref);
    if (!ref.did.isEmpty()) {
        m_byDid.insert(nickKey(ref.did), hKey);
        m_byHandle.insert(nickKey(ref.did), ref); // direct DID lookup
    }
    if (!ref.handle.isEmpty() && ref.handle != hKey) {
        m_byHandle.insert(nickKey(ref.handle), ref);
    }
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
    m_byDid.clear();
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
        if (o.contains(QStringLiteral("hasSprite"))) {
            ref.hasSprite = o.value(QStringLiteral("hasSprite")).toBool() && !ref.spriteUrl.isEmpty();
        }

        if (ref.handle.isEmpty() && ref.did.isEmpty()) {
            continue;
        }

        cacheRef(ref);

        if (!ref.displayName.isEmpty()) {
            const QString dKey = nickKey(ref.displayName);
            displayCount[dKey] = displayCount.value(dKey, 0) + 1;
            if (!displayFirst.contains(dKey)) {
                displayFirst.insert(dKey, nickKey(ref.handle.isEmpty() ? ref.did : ref.handle));
            }
        }

        const int dot = ref.handle.indexOf(QLatin1Char('.'));
        if (dot > 0) {
            const QString local = nickKey(ref.handle.left(dot));
            localPartCount[local] = localPartCount.value(local, 0) + 1;
            if (!localPartFirst.contains(local)) {
                localPartFirst.insert(local,
                                      nickKey(ref.handle.isEmpty() ? ref.did : ref.handle));
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
    if (it != m_byHandle.constEnd() && it->hasSprite) {
        return it.value();
    }
    // DID remembered from freeq login
    auto didIt = m_nickToDid.constFind(key);
    if (didIt != m_nickToDid.constEnd()) {
        auto byDid = m_byHandle.constFind(nickKey(didIt.value()));
        if (byDid != m_byHandle.constEnd() && byDid->hasSprite) {
            return byDid.value();
        }
        auto map = m_byDid.constFind(nickKey(didIt.value()));
        if (map != m_byDid.constEnd()) {
            auto h = m_byHandle.constFind(map.value());
            if (h != m_byHandle.constEnd() && h->hasSprite) {
                return h.value();
            }
        }
    }
    auto loc = m_byLocalPart.constFind(key);
    if (loc != m_byLocalPart.constEnd()) {
        auto h = m_byHandle.constFind(loc.value());
        if (h != m_byHandle.constEnd() && h->hasSprite) {
            return h.value();
        }
    }
    auto dn = m_byDisplayName.constFind(key);
    if (dn != m_byDisplayName.constEnd()) {
        auto h = m_byHandle.constFind(dn.value());
        if (h != m_byHandle.constEnd() && h->hasSprite) {
            return h.value();
        }
    }
    // Entry without sprite still returned? No — callers need sprites.
    if (it != m_byHandle.constEnd()) {
        return it.value(); // may have hasSprite=false
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

QString RpgActorClient::resolveHandleToDid(const QString &handle, int timeoutMs)
{
    QUrl url(QString::fromUtf8(kBskyResolve));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("handle"), handle);
    url.setQuery(q);
    QByteArray body;
    if (!downloadBytes(url, body, timeoutMs)) {
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    return doc.object().value(QStringLiteral("did")).toString();
}

QString RpgActorClient::resolveDidToPds(const QString &did, int timeoutMs)
{
    // PLC directory for did:plc; did:web would need different resolution.
    if (!did.startsWith(QLatin1String("did:plc:"))) {
        return {};
    }
    const QUrl url(QStringLiteral("https://plc.directory/%1").arg(did));
    QByteArray body;
    if (!downloadBytes(url, body, timeoutMs)) {
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    const QJsonArray services = doc.object().value(QStringLiteral("service")).toArray();
    for (const QJsonValue &v : services) {
        const QJsonObject s = v.toObject();
        if (s.value(QStringLiteral("id")).toString() == QLatin1String("#atproto_pds") ||
            s.value(QStringLiteral("type")).toString() ==
                QLatin1String("AtprotoPersonalDataServer")) {
            return s.value(QStringLiteral("serviceEndpoint")).toString();
        }
    }
    return {};
}

std::optional<RpgActorRef> RpgActorClient::fetchSpriteFromPds(const QString &did,
                                                              const QString &handle,
                                                              const QString &pds, int timeoutMs)
{
    if (did.isEmpty() || pds.isEmpty()) {
        return std::nullopt;
    }
    QString base = pds;
    while (base.endsWith(QLatin1Char('/'))) {
        base.chop(1);
    }
    const QUrl listUrl(QStringLiteral("%1/xrpc/com.atproto.repo.listRecords?repo=%2&collection="
                                      "actor.rpg.sprite&limit=5")
                           .arg(base, QString::fromUtf8(QUrl::toPercentEncoding(did))));
    QByteArray body;
    if (!downloadBytes(listUrl, body, timeoutMs)) {
        return std::nullopt;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    const QJsonArray records = doc.object().value(QStringLiteral("records")).toArray();
    if (records.isEmpty()) {
        return std::nullopt;
    }
    // Prefer rkey "self" if present, else first record.
    QJsonObject value;
    for (const QJsonValue &v : records) {
        const QJsonObject rec = v.toObject();
        const QString uri = rec.value(QStringLiteral("uri")).toString();
        if (uri.endsWith(QLatin1String("/self"))) {
            value = rec.value(QStringLiteral("value")).toObject();
            break;
        }
        if (value.isEmpty()) {
            value = rec.value(QStringLiteral("value")).toObject();
        }
    }
    if (value.isEmpty()) {
        return std::nullopt;
    }

    const QJsonObject sheet = value.value(QStringLiteral("spriteSheet")).toObject();
    const QString cid = sheet.value(QStringLiteral("ref")).toObject().value(QStringLiteral("$link")).toString();
    if (cid.isEmpty()) {
        return std::nullopt;
    }

    RpgActorRef ref;
    ref.did = did;
    ref.handle = handle;
    ref.sheetW = value.value(QStringLiteral("width")).toInt(144);
    ref.sheetH = value.value(QStringLiteral("height")).toInt(192);
    ref.columns = value.value(QStringLiteral("columns")).toInt(kDefaultCols);
    ref.rows = value.value(QStringLiteral("rows")).toInt(kDefaultRows);
    ref.spriteUrl = QStringLiteral("%1/xrpc/com.atproto.sync.getBlob?did=%2&cid=%3")
                        .arg(base, QString::fromUtf8(QUrl::toPercentEncoding(did)),
                             QString::fromUtf8(QUrl::toPercentEncoding(cid)));
    ref.hasSprite = true;
    cacheRef(ref);
    return ref;
}

std::optional<RpgActorRef> RpgActorClient::fetchLiveActor(const QString &nickOrDid, int timeoutMs)
{
    const QString key = nickKey(nickOrDid);
    if (key.isEmpty() || m_liveMiss.value(key, false)) {
        return std::nullopt;
    }

    // Per-slice timeouts so total stays near timeoutMs.
    const int slice = std::max(1500, timeoutMs / 3);

    QString did;
    QString handle;
    if (key.startsWith(QLatin1String("did:"))) {
        did = nickOrDid.trimmed();
    } else if (m_nickToDid.contains(key)) {
        did = m_nickToDid.value(key);
        handle = key;
    } else {
        handle = key;
        did = resolveHandleToDid(handle, slice);
    }
    if (did.isEmpty()) {
        m_liveMiss.insert(key, true);
        return std::nullopt;
    }

    // Try rpg.actor index by DID (may be indexed under another handle).
    {
        const QUrl apiUrl(QStringLiteral("https://rpg.actor/api/actor/%1")
                              .arg(QString::fromUtf8(QUrl::toPercentEncoding(did))));
        QByteArray body;
        if (downloadBytes(apiUrl, body, slice)) {
            const QJsonDocument doc = QJsonDocument::fromJson(body);
            if (doc.isObject() && !doc.object().contains(QStringLiteral("error"))) {
                const QJsonObject o = doc.object();
                RpgActorRef ref;
                ref.did = o.value(QStringLiteral("did")).toString(did);
                ref.handle = o.value(QStringLiteral("handle")).toString(handle);
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
                if (ref.hasSprite) {
                    if (ref.handle.isEmpty()) {
                        ref.handle = handle;
                    }
                    cacheRef(ref);
                    // Also bind under the nick we were asked for.
                    m_byHandle.insert(key, ref);
                    return ref;
                }
            }
        }
    }

    const QString pds = resolveDidToPds(did, slice);
    auto ref = fetchSpriteFromPds(did, handle.isEmpty() ? key : handle, pds, slice);
    if (!ref) {
        m_liveMiss.insert(key, true);
        return std::nullopt;
    }
    m_byHandle.insert(key, *ref);
    return ref;
}

ComicImage RpgActorClient::extractFrame(const QImage &sheet, const RpgActorRef &ref, int row,
                                        int col) const
{
    ComicImage out;
    if (sheet.isNull()) {
        return out;
    }

    int cols = ref.columns > 0 ? ref.columns : kDefaultCols;
    int rows = ref.rows > 0 ? ref.rows : kDefaultRows;
    int cellW = sheet.width() / std::max(1, cols);
    int cellH = sheet.height() / std::max(1, rows);
    if (sheet.width() == 144 && sheet.height() == 192 && cols == 3 && rows == 4) {
        cellW = 48;
        cellH = 48;
    }

    col = std::max(0, std::min(cols - 1, col));
    row = std::max(0, std::min(rows - 1, row));
    const int x = col * cellW;
    const int y = row * cellH;
    if (x + cellW > sheet.width() || y + cellH > sheet.height()) {
        out.setQImage(sheet);
        return out;
    }
    out.setQImage(sheet.copy(x, y, cellW, cellH));
    return out;
}

std::optional<RpgSpriteSheet> RpgActorClient::loadSheetForRef(const RpgActorRef &ref,
                                                              const QString &cacheKey,
                                                              int timeoutMs)
{
    if (!cacheKey.isEmpty()) {
        auto hit = m_sheetCache.constFind(cacheKey);
        if (hit != m_sheetCache.constEnd() && !hit->isNull()) {
            return hit.value();
        }
    }
    if (!ref.spriteUrl.isEmpty()) {
        auto byUrl = m_sheetCache.constFind(ref.spriteUrl);
        if (byUrl != m_sheetCache.constEnd() && !byUrl->isNull()) {
            if (!cacheKey.isEmpty()) {
                m_sheetCache.insert(cacheKey, byUrl.value());
            }
            return byUrl.value();
        }
    }

    QByteArray bytes;
    if (!downloadBytes(QUrl(ref.spriteUrl), bytes, timeoutMs)) {
        if (!ref.did.isEmpty()) {
            const QString norm = QStringLiteral("https://rpg.actor/api/sprite/normalized?did=%1")
                                     .arg(QString::fromUtf8(QUrl::toPercentEncoding(ref.did)));
            if (!downloadBytes(QUrl(norm), bytes, timeoutMs)) {
                return std::nullopt;
            }
        } else {
            return std::nullopt;
        }
    }

    QImage img;
    if (!img.loadFromData(bytes)) {
        return std::nullopt;
    }
    RpgSpriteSheet asset;
    asset.sheet.setQImage(img);
    asset.columns = ref.columns > 0 ? ref.columns : kDefaultCols;
    asset.rows = ref.rows > 0 ? ref.rows : kDefaultRows;
    if (!cacheKey.isEmpty()) {
        m_sheetCache.insert(cacheKey, asset);
    }
    if (!ref.spriteUrl.isEmpty()) {
        m_sheetCache.insert(ref.spriteUrl, asset);
    }
    if (!ref.handle.isEmpty()) {
        m_sheetCache.insert(nickKey(ref.handle), asset);
    }
    return asset;
}

std::optional<RpgSpriteSheet> RpgActorClient::spriteSheetForNick(const QString &nick,
                                                                 int timeoutMs)
{
    const QString key = nickKey(nick);
    if (key.isEmpty()) {
        return std::nullopt;
    }

    auto cached = m_sheetCache.constFind(key);
    if (cached != m_sheetCache.constEnd() && !cached->isNull()) {
        return cached.value();
    }

    auto ref = lookupKey(key);
    if (!ref || !ref->hasSprite || ref->spriteUrl.isEmpty()) {
        ref = fetchLiveActor(nick, timeoutMs);
    }
    if (!ref || !ref->hasSprite || ref->spriteUrl.isEmpty()) {
        return std::nullopt;
    }

    auto sheet = loadSheetForRef(*ref, key, timeoutMs);
    if (sheet) {
        emit spriteReady(nick);
    }
    return sheet;
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

    auto sheet = spriteSheetForNick(nick, timeoutMs);
    if (!sheet || sheet->isNull()) {
        return std::nullopt;
    }

    RpgActorRef meta;
    meta.columns = sheet->columns;
    meta.rows = sheet->rows;
    ComicImage frame = extractFrame(sheet->sheet.qimage(), meta, kDownRow, kIdleCol);
    if (frame.isNull()) {
        return std::nullopt;
    }
    m_spriteCache.insert(key, frame);
    return frame;
}
