// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "app/ComicWidget.h"

#include "engine/spline.h"
#include "platform/QtCanvas.h"

#include <QPaintEvent>
#include <QPainter>

ComicWidget::ComicWidget(QWidget *parent)
    : QWidget(parent)
    , m_demoMessage(QStringLiteral("Hello from a real CCardinal spline!"))
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

void ComicWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), QColor(0xf5, 0xf0, 0xe6));

    QtCanvas canvas(&painter);
    canvas.setLogicalScale(1.0, 1.0);

    // Outer panel frame
    RECT panel{40, 40, width() - 40, height() - 40};
    canvas.setBrush(CanvasColor::rgb(255, 255, 255));
    canvas.setPen(CanvasColor::rgb(20, 20, 20), 3);
    canvas.fillRect(panel);
    canvas.drawRect(panel);

    // Balloon outline from original Cardinal spline engine
    const int cx = width() / 2;
    const int cy = height() / 2 - 20;
    POINT cps[8] = {
        {cx - 130, cy},      {cx - 110, cy - 70}, {cx, cy - 85},     {cx + 110, cy - 70},
        {cx + 130, cy},      {cx + 100, cy + 65}, {cx - 30, cy + 75}, {cx - 110, cy + 55},
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

    // Simple tail (line segments)
    canvas.beginPath();
    canvas.moveTo(cx - 25, cy + 70);
    canvas.lineTo(cx - 55, cy + 130);
    canvas.lineTo(cx + 15, cy + 72);
    canvas.closePath();
    canvas.strokeAndFill();

    canvas.setFont("Sans Serif", 14, false);
    canvas.setPen(CanvasColor::rgb(0, 0, 0), 1);
    const std::string text = m_demoMessage.toStdString();
    const int tw = canvas.measureTextWidth(text);
    canvas.drawText(cx - tw / 2, cy + 6, text);

    canvas.setFont("Sans Serif", 10, false);
    canvas.setPen(CanvasColor::rgb(80, 80, 80), 1);
    canvas.drawText(50, height() - 24, "Phase 1: CCardinal spline (engine/spline.cpp)");
}
