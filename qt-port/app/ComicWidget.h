// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "engine/avatario.h"
#include "engine/image.h"

#include <QWidget>
#include <QString>
#include <memory>

class ComicWidget : public QWidget {
    Q_OBJECT
public:
    explicit ComicWidget(QWidget *parent = nullptr);

    void setDemoMessage(const QString &msg);
    QString demoMessage() const { return m_demoMessage; }

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void ensureAssetsLoaded();

    QString m_demoMessage;
    bool m_assetsTried = false;
    bool m_assetsOk = false;
    QString m_statusLine;
    ComicImage m_backdrop;
    LoadedAvatar m_avatar;
    USHORT m_bodyPose = 0;
    USHORT m_facePose = 0;
    USHORT m_torsoPose = 0;
};
