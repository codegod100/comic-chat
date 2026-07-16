// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "app/ComicWidget.h"

#include "engine/art_paths.h"
#include "engine/backdrop_qt.h"
#include "engine/pose.h"
#include "platform/QtCanvas.h"

#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>

ComicWidget::ComicWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(480, 360);
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
}

void ComicWidget::addChatLine(const QString &text)
{
    ensureAssetsLoaded();
    if (!m_assetsOk) {
        update();
        return;
    }
    m_scene.addLine(text.toStdString(), SM_SAY);
    update();
}

void ComicWidget::clearPanels()
{
    m_scene.clear();
    update();
}

QString ComicWidget::statusLine() const
{
    if (!m_loadError.isEmpty()) {
        return m_loadError;
    }
    return QString::fromStdString(m_scene.status());
}

QSize ComicWidget::sizeHint() const
{
    return QSize(720, 520);
}

QSize ComicWidget::minimumSizeHint() const
{
    return QSize(400, 300);
}

void ComicWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    ensureAssetsLoaded();
}

void ComicWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    update();
}

void ComicWidget::ensureAssetsLoaded()
{
    if (m_assetsTried) {
        return;
    }
    m_assetsTried = true;

    const ArtPaths art = resolveArtPaths();
    setAvatarArtDir(art.avatars);

    ComicImage backdrop;
    if (!LoadBackdropImage(art.backdrop, "room8bs", backdrop)) {
        m_loadError = QStringLiteral("No backdrop in %1")
                          .arg(QString::fromStdString(art.backdrop));
        return;
    }

    LoadedAvatar avatar;
    if (!LoadDemoAvatar(avatar)) {
        m_loadError = QStringLiteral("No avatar in %1")
                          .arg(QString::fromStdString(art.avatars));
        return;
    }

    // Warm pose cache
    if (avatar.type == AT_COMPLEX) {
        if (avatar.facePoses.empty() || avatar.torsoPoses.empty() ||
            !GetPoseFromID(avatar.facePoses.front()) ||
            !GetPoseFromID(avatar.torsoPoses.front())) {
            m_loadError = QStringLiteral("Failed loading poses for %1")
                              .arg(QString::fromStdString(avatar.name));
            return;
        }
    } else {
        const USHORT id =
            !avatar.bodyPoses.empty() ? avatar.bodyPoses.front() : avatar.iconPose;
        if (!GetPoseFromID(id)) {
            m_loadError = QStringLiteral("Failed loading body for %1")
                              .arg(QString::fromStdString(avatar.name));
            return;
        }
    }

    m_scene.setArt(avatar, backdrop);
    m_assetsOk = true;
    m_loadError.clear();
}

void ComicWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    ensureAssetsLoaded();

    QPainter painter(this);
    painter.fillRect(rect(), QColor(0xe8, 0xe4, 0xdc));

    QtCanvas canvas(&painter);
    canvas.setLogicalScale(1.0, 1.0);

    RECT dest{16, 16, width() - 16, height() - 36};
    m_scene.draw(&canvas, dest);

    canvas.setFont("Sans Serif", 10, false);
    canvas.setPen(CanvasColor::rgb(50, 50, 50), 1);
    canvas.drawText(16, height() - 14, statusLine().toStdString());
}
