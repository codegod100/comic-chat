// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "app/ComicWidget.h"

#include "engine/art_paths.h"
#include "engine/backdrop_qt.h"
#include "engine/pose.h"
#include "platform/QtCanvas.h"

#include <QPaintEvent>
#include <QPainter>
#include <QShowEvent>

ComicWidget::ComicWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(260, 240);
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
}

void ComicWidget::setViewportHeight(int h)
{
    h = std::max(200, h);
    if (h == m_viewportH) {
        return;
    }
    m_viewportH = h;
    // Size is applied by MainWindow::syncComicSize — do not emit contentResized
    // here or we recurse (sync → setViewportHeight → contentResized → sync).
    updateGeometry();
}

int ComicWidget::contentHeight() const
{
    const int usable = std::max(180, m_viewportH - 2 * m_margin - 22);
    return m_scene.contentHeightForHeight(usable) + 2 * m_margin + 22;
}

int ComicWidget::contentWidth() const
{
    const int usable = std::max(180, m_viewportH - 2 * m_margin - 22);
    return m_scene.contentWidthForHeight(usable) + 2 * m_margin;
}

void ComicWidget::addChatLine(const QString &text, const QString &nick)
{
    ensureAssetsLoaded();
    if (!m_assetsOk) {
        update();
        return;
    }
    const QString who = nick.isEmpty() ? QStringLiteral("you") : nick;
    m_scene.addLine(text.toStdString(), SM_SAY, who.toStdString());
    relayout();
    update();
}

void ComicWidget::clearPanels()
{
    m_scene.clear();
    relayout();
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
    return QSize(contentWidth(), contentHeight());
}

QSize ComicWidget::minimumSizeHint() const
{
    return QSize(260, 240);
}

void ComicWidget::relayout()
{
    updateGeometry();
    emit contentResized();
}

void ComicWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    ensureAssetsLoaded();
    relayout();
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

    std::vector<LoadedAvatar> cast = LoadAllAvatars();
    if (cast.empty()) {
        m_loadError = QStringLiteral("No avatars in %1")
                          .arg(QString::fromStdString(art.avatars));
        return;
    }

    // Warm first pose for each cast member so assign-time draws don't hitch as hard.
    int usable = 0;
    for (const auto &av : cast) {
        if (av.type == AT_COMPLEX) {
            if (!av.faces.empty() && !av.torsos.empty() &&
                GetPoseFromID(av.faces.front().poseID) &&
                GetPoseFromID(av.torsos.front().poseID)) {
                ++usable;
            }
        } else {
            const USHORT id =
                !av.bodies.empty()
                    ? av.bodies.front().poseID
                    : (!av.bodyPoses.empty() ? av.bodyPoses.front() : av.iconPose);
            if (GetPoseFromID(id)) {
                ++usable;
            }
        }
    }
    if (usable == 0) {
        m_loadError = QStringLiteral("No drawable avatars in %1")
                          .arg(QString::fromStdString(art.avatars));
        return;
    }

    m_scene.setArt(std::move(cast), backdrop);
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

    const int statusH = 20;
    RECT dest{m_margin, m_margin, width() - m_margin, height() - m_margin - statusH};
    m_scene.draw(&canvas, dest);

    canvas.setFont("Sans Serif", 10, false);
    canvas.setPen(CanvasColor::rgb(50, 50, 50), 1);
    canvas.drawText(m_margin, height() - 8, statusLine().toStdString());
}
