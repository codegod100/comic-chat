// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "engine/scene.h"

#include <QWidget>
#include <QString>

class ComicWidget : public QWidget {
    Q_OBJECT
public:
    explicit ComicWidget(QWidget *parent = nullptr);

    void addChatLine(const QString &text, const QString &nick = QStringLiteral("you"));
    void clearPanels();
    QString statusLine() const;

    // Viewport height drives square panel size; width grows with panel count.
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
    void relayout();
    int contentHeight() const;
    int contentWidth() const;

    ComicScene m_scene;
    bool m_assetsTried = false;
    bool m_assetsOk = false;
    QString m_loadError;
    int m_margin = 12;
    int m_viewportH = 400;
};
