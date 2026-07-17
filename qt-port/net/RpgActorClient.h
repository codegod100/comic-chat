// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// Client for https://rpg.actor — universal RPG character registry (AT Protocol).
// Looks up actor.rpg.sprite by handle / nick / DID and caches idle frames.
//
// Lookup order:
//   1. Cached public registry (GET /api/actors/full) by handle, DID, local-part, displayName
//   2. Live PDS fallback: resolve handle → DID → listRecords actor.rpg.sprite
//      (covers new actors not yet in the registry index — e.g. nandi-test.bsky.social)
//
// Dev guide: https://rpg.actor/dev-guide

#pragma once

#include "engine/image.h"

#include <QByteArray>
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

#include <optional>
#include <string>

struct RpgActorRef {
    QString did;
    QString handle;      // e.g. nandi-test.bsky.social
    QString displayName;
    QString spriteUrl;   // displayUrl or raw blob URL (full walk sheet)
    int sheetW = 144;
    int sheetH = 192;
    int columns = 3;
    int rows = 4;
    bool hasSprite = false;
};

// Full walk sheet + grid meta (rpg.actor standard: 144×192, 3×4).
// Rows: 0=down, 1=left, 2=right, 3=up. Cols: 0=step, 1=idle, 2=step.
struct RpgSpriteSheet {
    ComicImage sheet;
    int columns = 3;
    int rows = 4;
    bool isNull() const { return sheet.isNull(); }
};

class RpgActorClient : public QObject {
    Q_OBJECT
public:
    explicit RpgActorClient(QObject *parent = nullptr);

    void refreshRegistry();
    bool registryReady() const { return m_registryReady; }
    int actorCount() const { return m_byHandle.size(); }

    // Match nick/handle/DID against registry only (no network).
    std::optional<RpgActorRef> lookupNick(const QString &nick) const;

    // Full walk sheet for directional facing (preferred for multi-speaker panels).
    // allowLiveFetch: if false, only registry + already-cached sheets (no PDS hop).
    std::optional<RpgSpriteSheet> spriteSheetForNick(const QString &nick, int timeoutMs = 5000,
                                                     bool allowLiveFetch = true);

    // Memory cache only — never hits the network (fast path for join/history).
    std::optional<RpgSpriteSheet> cachedSheetForNick(const QString &nick) const;

    // Idle *down* frame only (compat). Prefer spriteSheetForNick for facing.
    std::optional<ComicImage> spriteForNick(const QString &nick, int timeoutMs = 5000);

    // Also bind a known DID (from freeq login) to a nick for faster lookup.
    void rememberDidForNick(const QString &nick, const QString &did);

    bool hasCachedSprite(const QString &nick) const;

signals:
    void registryUpdated(int actorCount);
    void spriteReady(const QString &nick);

private:
    void parseActorsJson(const QByteArray &json);
    static QString nickKey(const QString &s);
    std::optional<RpgActorRef> lookupKey(const QString &key) const;
    // Live: handle/DID → PDS actor.rpg.sprite record.
    std::optional<RpgActorRef> fetchLiveActor(const QString &nickOrDid, int timeoutMs);
    QString resolveHandleToDid(const QString &handle, int timeoutMs);
    QString resolveDidToPds(const QString &did, int timeoutMs);
    std::optional<RpgActorRef> fetchSpriteFromPds(const QString &did, const QString &handle,
                                                  const QString &pds, int timeoutMs);
    void cacheRef(const RpgActorRef &ref);
    ComicImage extractFrame(const QImage &sheet, const RpgActorRef &ref, int row, int col) const;
    bool downloadBytes(const QUrl &url, QByteArray &out, int timeoutMs);
    std::optional<RpgSpriteSheet> loadSheetForRef(const RpgActorRef &ref, const QString &cacheKey,
                                                  int timeoutMs);

    QNetworkAccessManager m_nam;
    bool m_registryReady = false;
    bool m_registryLoading = false;

    QHash<QString, RpgActorRef> m_byHandle;     // handle or did key → ref
    QHash<QString, QString> m_byDid;            // did → handle key
    QHash<QString, QString> m_byDisplayName;    // displayName → handle key
    QHash<QString, QString> m_byLocalPart;      // unique local-part → handle key
    QHash<QString, QString> m_nickToDid;        // nick key → did (from freeq login)

    QHash<QString, RpgSpriteSheet> m_sheetCache; // nick/url → full sheet
    QHash<QString, ComicImage> m_spriteCache;    // nick → down idle frame
    QHash<QString, bool> m_liveMiss; // nick keys that already failed live fetch this session
};
