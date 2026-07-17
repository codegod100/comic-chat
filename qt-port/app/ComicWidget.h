// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "engine/scene.h"
#include "net/RpgActorClient.h"

#include <QHash>
#include <QList>
#include <QNetworkAccessManager>
#include <QPixmap>
#include <QString>
#include <QStringList>
#include <QWidget>

class ComicWidget : public QWidget {
    Q_OBJECT
public:
    explicit ComicWidget(QWidget *parent = nullptr);

    void addChatLine(const QString &text, const QString &nick = QStringLiteral("you"));
    // IRCv3 / freeq media: tags may include media-url, content-type, media-alt,
    // msgid, +reply (threaded reply — new panel with original + reply).
    void addChatLine(const QString &text, const QString &nick,
                     const QHash<QString, QString> &tags);
    // Cache only (self echo / join history) — no new comic panel.
    void rememberIrcMessage(const QString &text, const QString &nick,
                            const QHash<QString, QString> &tags);
    // Local send before server msgid: bind later via rememberIrcMessage/echo.
    void noteOutgoingMessage(const QString &text, const QString &nick);
    // Look up freeq msgid cache (for log + reply parents).
    bool hasCachedMessage(const QString &msgid) const;
    bool lookupCachedMessage(const QString &msgid, QString *nickOut,
                             QString *textOut) const;
    void clearPanels();
    QString statusLine() const;

    // freeq/ATProto: remember DID for a nick so rpg.actor can resolve by DID.
    void rememberAtprotoIdentity(const QString &handleOrNick, const QString &did);

    // Room / backdrop (base names from comicart/backdrop/*.bmp).
    QStringList availableRooms() const;
    QString currentRoom() const { return m_roomName; }
    bool setRoom(const QString &baseName);
    // Thumbnail for combo/list previews (loads from disk; may be null).
    QPixmap roomThumbnail(const QString &baseName, const QSize &size = QSize(64, 48)) const;

    void setViewportHeight(int h);
    int viewportHeight() const { return m_viewportH; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void contentResized();
    void roomChanged(const QString &baseName);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void ensureAssetsLoaded();
    void ensureRpgSprite(const QString &nick);
    void relayout();
    int contentHeight() const;
    int contentWidth() const;

    // Detect image URL from freeq tags or plain text; start download if needed.
    void handlePossiblyMedia(const QString &text, const QString &nick,
                             const QHash<QString, QString> &tags);
    void fetchAndShowImage(const QUrl &url, const QString &caption, const QString &nick);
    void cacheMessage(const QString &msgid, const QString &nick, const QString &text);
    // freeq: +reply / draft/reply → parent msgid.
    static QString replyParentId(const QHash<QString, QString> &tags);
    static QString messageId(const QHash<QString, QString> &tags);
    static QString extractImageUrl(const QString &text);
    static bool looksLikeImageUrl(const QUrl &url);
    static QString stripUrls(const QString &text);

    struct CachedChatLine {
        QString nick;
        QString text;
    };

    ComicScene m_scene;
    RpgActorClient m_rpg;
    QNetworkAccessManager m_nam;
    // freeq msgid → last text/nick (for +reply parent lookup).
    QHash<QString, CachedChatLine> m_msgById;
    QStringList m_msgIdOrder; // eviction order
    // Outgoing lines waiting for echo-message msgid.
    struct PendingOut {
        QString nick;
        QString text;
    };
    QList<PendingOut> m_pendingOut;
    static constexpr int kMaxCachedMsgs = 500;
    bool m_assetsTried = false;
    bool m_assetsOk = false;
    QString m_loadError;
    QString m_backdropDir;
    QString m_roomName = QStringLiteral("room8bs");
    int m_margin = 12;
    int m_viewportH = 400;
};
