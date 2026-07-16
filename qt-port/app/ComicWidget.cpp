// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "app/ComicWidget.h"

#include "engine/art_paths.h"
#include "engine/backdrop_qt.h"
#include "engine/pose.h"
#include "engine/spline.h"
#include "platform/QtCanvas.h"

#include <QPaintEvent>
#include <QPainter>
#include <QShowEvent>

ComicWidget::ComicWidget(QWidget *parent)
    : QWidget(parent)
    , m_demoMessage(QStringLiteral("Hello from real avatars!"))
{
    setMinimumSize(480, 360);
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
}

void ComicWidget::setDemoMessage(const QString &msg)
{
    m_demoMessage = msg;
    update();
}

QSize ComicWidget::sizeHint() const
{
    return QSize(720, 480);
}

void ComicWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    ensureAssetsLoaded();
}

void ComicWidget::ensureAssetsLoaded()
{
    if (m_assetsTried) {
        return;
    }
    m_assetsTried = true;

    const ArtPaths art = resolveArtPaths();
    setAvatarArtDir(art.avatars);

    if (!LoadBackdropImage(art.backdrop, "room8bs", m_backdrop)) {
        m_statusLine = QStringLiteral("No backdrop in %1")
                           .arg(QString::fromStdString(art.backdrop));
        return;
    }

    if (!LoadDemoAvatar(m_avatar)) {
        m_statusLine = QStringLiteral("No avatar in %1")
                           .arg(QString::fromStdString(art.avatars));
        return;
    }

    if (m_avatar.type == AT_COMPLEX) {
        if (!m_avatar.facePoses.empty()) {
            m_facePose = m_avatar.facePoses.front();
        }
        if (!m_avatar.torsoPoses.empty()) {
            m_torsoPose = m_avatar.torsoPoses.front();
        }
    } else if (!m_avatar.bodyPoses.empty()) {
        m_bodyPose = m_avatar.bodyPoses.front();
    } else if (m_avatar.iconPose) {
        m_bodyPose = m_avatar.iconPose;
    }

    // Touch poses so load errors show early
    if (m_avatar.type == AT_COMPLEX) {
        if (!GetPoseFromID(m_facePose) || !GetPoseFromID(m_torsoPose)) {
            m_statusLine = QStringLiteral("Failed to load poses for %1")
                               .arg(QString::fromStdString(m_avatar.name));
            return;
        }
    } else {
        if (!GetPoseFromID(m_bodyPose)) {
            m_statusLine = QStringLiteral("Failed to load body pose for %1")
                               .arg(QString::fromStdString(m_avatar.name));
            return;
        }
    }

    m_assetsOk = true;
    m_statusLine = QStringLiteral("Avatar: %1 | Art: %2")
                       .arg(QString::fromStdString(m_avatar.name),
                            QString::fromStdString(art.root));
}

void ComicWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    ensureAssetsLoaded();

    QPainter painter(this);
    painter.fillRect(rect(), QColor(0xf5, 0xf0, 0xe6));

    QtCanvas canvas(&painter);
    canvas.setLogicalScale(1.0, 1.0);

    RECT panel{40, 40, width() - 40, height() - 80};

    // Backdrop (or white)
    DrawBackdrop(&canvas, m_backdrop, panel);
    canvas.setPen(CanvasColor::rgb(20, 20, 20), 3);
    canvas.setNoBrush();
    canvas.drawRect(panel);

    // Avatar body near bottom-center of panel
    if (m_assetsOk) {
        const int targetH = (panel.bottom - panel.top) * 55 / 100;
        if (m_avatar.type == AT_COMPLEX) {
            CPose *torso = GetPoseFromID(m_torsoPose);
            CPose *face = GetPoseFromID(m_facePose);
            if (torso && torso->m_drawing && face && face->m_drawing) {
                const int tw = torso->m_drawing->width();
                const int th = torso->m_drawing->height();
                const int fw = face->m_drawing->width();
                const int fh = face->m_drawing->height();
                // Scale so combined height ~ targetH (rough head-on-torso stack)
                const int stackH = th + fh / 2;
                const double scale = stackH > 0 ? double(targetH) / stackH : 1.0;
                const int dw = int(tw * scale);
                const int dh = int(th * scale);
                const int fdw = int(fw * scale);
                const int fdh = int(fh * scale);
                const int bx = (panel.left + panel.right) / 2 - dw / 2;
                const int by = panel.bottom - 20 - dh;
                torso->drawMasked(&canvas, bx, by, dw, dh);
                // Head roughly on upper torso
                const int fx = bx + (dw - fdw) / 2;
                const int fy = by - fdh / 3;
                face->drawMasked(&canvas, fx, fy, fdw, fdh);
            }
        } else {
            CPose *body = GetPoseFromID(m_bodyPose);
            if (body && body->m_drawing) {
                const int bw = body->m_drawing->width();
                const int bh = body->m_drawing->height();
                const double scale = bh > 0 ? double(targetH) / bh : 1.0;
                const int dw = int(bw * scale);
                const int dh = int(bh * scale);
                const int bx = (panel.left + panel.right) / 2 - dw / 2;
                const int by = panel.bottom - 20 - dh;
                body->drawMasked(&canvas, bx, by, dw, dh);
            }
        }
    }

    // Speech balloon (Cardinal spline)
    const int cx = width() / 2;
    const int cy = panel.top + (panel.bottom - panel.top) / 4;
    POINT cps[8] = {
        {cx - 140, cy},      {cx - 120, cy - 55}, {cx, cy - 70},      {cx + 120, cy - 55},
        {cx + 140, cy},      {cx + 110, cy + 50}, {cx - 20, cy + 58}, {cx - 115, cy + 45},
    };
    CCardinal balloon(cps, 8, TRUE);
    canvas.setBrush(CanvasColor::rgb(255, 255, 255));
    canvas.setPen(CanvasColor::rgb(0, 0, 0), 2);
    canvas.beginPath();
    POINT lo = balloon.SegLo();
    canvas.moveTo(lo.x, lo.y);
    balloon.Draw(&canvas);
    canvas.closePath();
    canvas.strokeAndFill();

    canvas.beginPath();
    canvas.moveTo(cx - 20, cy + 55);
    canvas.lineTo(cx - 40, cy + 110);
    canvas.lineTo(cx + 15, cy + 55);
    canvas.closePath();
    canvas.strokeAndFill();

    canvas.setFont("Sans Serif", 14, false);
    canvas.setPen(CanvasColor::rgb(0, 0, 0), 1);
    const std::string text = m_demoMessage.toStdString();
    const int tw = canvas.measureTextWidth(text);
    canvas.drawText(cx - tw / 2, cy + 6, text);

    canvas.setFont("Sans Serif", 10, false);
    canvas.setPen(CanvasColor::rgb(60, 60, 60), 1);
    canvas.drawText(50, height() - 28, m_statusLine.toStdString());
}
