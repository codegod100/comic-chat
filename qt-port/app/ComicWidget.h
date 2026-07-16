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

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void ensureAssetsLoaded();

    ComicScene m_scene;
    bool m_assetsTried = false;
    bool m_assetsOk = false;
    QString m_loadError;
};
