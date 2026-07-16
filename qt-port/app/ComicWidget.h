// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "engine/scene.h"
#include "net/RpgActorClient.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QString>
#include <QWidget>

class ComicWidget : public QWidget {
    Q_OBJECT
public:
    explicit ComicWidget(QWidget *parent = nullptr);

    void addChatLine(const QString &text, const QString &nick = QStringLiteral("you"));
    // IRCv3 / freeq media: tags may include media-url, content-type, media-alt.
    void addChatLine(const QString &text, const QString &nick,
                     const QHash<QString, QString> &tags);
    void clearPanels();
    QString statusLine() const;

    // freeq/ATProto: remember DID for a nick so rpg.actor can resolve by DID.
    void rememberAtprotoIdentity(const QString &handleOrNick, const QString &did);

    void setViewportHeight(int h);
    int viewportHeight() const { return m_viewportH; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void contentResized();

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
    static QString extractImageUrl(const QString &text);
    static bool looksLikeImageUrl(const QUrl &url);
    static QString stripUrls(const QString &text);

    ComicScene m_scene;
    RpgActorClient m_rpg;
    QNetworkAccessManager m_nam;
    bool m_assetsTried = false;
    bool m_assetsOk = false;
    QString m_loadError;
    int m_margin = 12;
    int m_viewportH = 400;
};
