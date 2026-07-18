// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "engine/scene.h"
#include "net/RpgActorClient.h"

#include <QHash>
#include <QImage>
#include <QList>
#include <QNetworkAccessManager>
#include <QPixmap>
#include <QRect>
#include <QSet>
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
    // fastJoin: skip blocking rpg.actor network (history flush); sprites upgrade async.
    void addChatLine(const QString &text, const QString &nick,
                     const QHash<QString, QString> &tags, bool fastJoin = false);
    // Cache only (self echo / join history) — no new comic panel.
    void rememberIrcMessage(const QString &text, const QString &nick,
                            const QHash<QString, QString> &tags);
    // Local send before server msgid: bind later via rememberIrcMessage/echo.
    void noteOutgoingMessage(const QString &text, const QString &nick);
    // freeq react: stamp emoji badge on the balloon for parentMsgid (comic strip).
    // remove=true forces removal; remove=false toggles (ATProto semantics).
    void applyReact(const QString &parentMsgid, const QString &emoji,
                    const QString &reactorNick, bool remove = false);
    // Look up freeq msgid cache (for log + reply parents).
    bool hasCachedMessage(const QString &msgid) const;
    bool lookupCachedMessage(const QString &msgid, QString *nickOut,
                             QString *textOut) const;
    void clearPanels();
    // Keep only the newest N panels in the strip (default 10).
    void trimToRecentPanels(int maxPanels = kMaxComicPanels);
    int maxComicPanels() const { return kMaxComicPanels; }
    QString statusLine() const;

    // freeq/ATProto: remember DID for a nick so rpg.actor can resolve by DID.
    // preloadSprite=false during history flood (avoids nested HTTP under IRC).
    void rememberAtprotoIdentity(const QString &handleOrNick, const QString &did,
                                 bool preloadSprite = true);
    // Non-blocking: apply cache hit now; network fetch finishes later + repaint.
    void ensureRpgSpriteAsync(const QString &nick);

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
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    bool event(QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void ensureAssetsLoaded();
    // blocking=false: cache only + schedule network; true: may nested-loop HTTP.
    void ensureRpgSprite(const QString &nick, bool blocking = false);
    void applyRpgSheet(const QString &nick, const RpgSpriteSheet &sheet);
    void relayout();
    int contentHeight() const;
    int contentWidth() const;

    // Detect image URL from freeq tags or plain text; start download if needed.
    void handlePossiblyMedia(const QString &text, const QString &nick,
                             const QHash<QString, QString> &tags, bool fastJoin = false);
    void fetchAndShowImage(const QUrl &url, const QString &caption, const QString &nick,
                           const QString &msgid = {});
    void cacheMessage(const QString &msgid, const QString &nick, const QString &text);
    // freeq: +reply / draft/reply → parent msgid.
    static QString replyParentId(const QHash<QString, QString> &tags);
    static QString messageId(const QHash<QString, QString> &tags);
    // freeq react tag: +react / draft/react (emoji or shortname). Empty if none.
    static QString reactEmoji(const QHash<QString, QString> &tags);
    static bool isReactRemove(const QHash<QString, QString> &tags);
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
    QSet<QString> m_rpgFetchInFlight; // nick keys with async fetch pending
    QSet<QString> m_imageFetchInFlight; // url|nick — avoid duplicate panels
    QSet<QString> m_imagesShown;        // already added to strip
    static constexpr int kMaxCachedMsgs = 500;
    // Comic strip only shows a short recent window (log keeps full history).
    static constexpr int kMaxComicPanels = 10;
    bool m_assetsTried = false;
    bool m_assetsOk = false;
    QString m_loadError;
    QString m_backdropDir;
    QString m_roomName = QStringLiteral("room8bs");
    int m_margin = 12;
    int m_viewportH = 400;

    // Hit-test targets for inline image previews rebuilt each paintEvent.
    struct ClickableImage {
        QRect screenRect;
        QImage fullImage;
    };
    std::vector<ClickableImage> m_clickableImages;

    // React chips hit testing (screen rect → tooltip)
    struct ClickableReact {
        QRect screenRect;
        QString parentMsgid;
        QString emoji;
        QStringList reactors; // nicks
    };
    std::vector<ClickableReact> m_clickableReacts;
};
