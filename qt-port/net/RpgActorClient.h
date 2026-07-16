// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// Client for https://rpg.actor — universal RPG character registry (AT Protocol).
// Looks up actor.rpg.sprite by handle / nick and caches idle frames for comic panels.
// Dev guide: https://rpg.actor/dev-guide

#pragma once

#include "engine/image.h"

#include <QObject>
#include <QString>
#include <QHash>
#include <QByteArray>
#include <QNetworkAccessManager>

#include <optional>
#include <string>

struct RpgActorRef {
    QString did;
    QString handle;      // e.g. libre.reverie.house
    QString displayName;
    QString spriteUrl;   // displayUrl or raw blob URL (full walk sheet)
    int sheetW = 144;
    int sheetH = 192;
    int columns = 3;
    int rows = 4;
    bool hasSprite = false;
};

// Resolves IRC nicks → rpg.actor records and extracts a facing-down idle frame.
class RpgActorClient : public QObject {
    Q_OBJECT
public:
    explicit RpgActorClient(QObject *parent = nullptr);

    // Kick off async load of GET https://rpg.actor/api/actors/full
    void refreshRegistry();
    bool registryReady() const { return m_registryReady; }
    int actorCount() const { return m_byHandle.size(); }

    // Match nick to a registry entry (exact handle, unique local-part, displayName).
    std::optional<RpgActorRef> lookupNick(const QString &nick) const;

    // Return cached idle-frame sprite for nick, downloading if needed.
    // Blocks the calling thread up to timeoutMs on first fetch (cached after).
    // Returns nullopt if no record / no sprite / download failed.
    std::optional<ComicImage> spriteForNick(const QString &nick, int timeoutMs = 4000);

    // Non-blocking: true if we already have the idle frame cached.
    bool hasCachedSprite(const QString &nick) const;

signals:
    void registryUpdated(int actorCount);
    void spriteReady(const QString &nick);

private:
    void parseActorsJson(const QByteArray &json);
    static QString nickKey(const QString &s);
    std::optional<RpgActorRef> lookupKey(const QString &key) const;
    ComicImage extractIdleFrame(const QImage &sheet, const RpgActorRef &ref) const;
    bool downloadBytes(const QUrl &url, QByteArray &out, int timeoutMs);

    QNetworkAccessManager m_nam;
    bool m_registryReady = false;
    bool m_registryLoading = false;

    // lowercase handle → ref
    QHash<QString, RpgActorRef> m_byHandle;
    // lowercase displayName → handle (only unambiguous)
    QHash<QString, QString> m_byDisplayName;
    // lowercase local part of handle (before first '.') → handle (only if unique)
    QHash<QString, QString> m_byLocalPart;

    // nick key → idle frame
    QHash<QString, ComicImage> m_spriteCache;
    // sprite URL → idle frame (share across nicks pointing at same sheet)
    QHash<QString, ComicImage> m_urlCache;
};
