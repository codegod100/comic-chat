// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "app/ComicWidget.h"

#include "platform/QtCanvas.h"

#include <QPaintEvent>
#include <QPainter>

ComicWidget::ComicWidget(QWidget *parent)
    : QWidget(parent)
    , m_demoMessage(QStringLiteral("Comic Chat Qt port — Phase 0 scaffold"))
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
    // Demo uses device pixels 1:1 (engine will set TWIPS scale later).
    canvas.setLogicalScale(1.0, 1.0);

    // Outer "panel" frame
    RECT panel{40, 40, width() - 40, height() - 40};
    canvas.setBrush(CanvasColor::rgb(255, 255, 255));
    canvas.setPen(CanvasColor::rgb(20, 20, 20), 3);
    canvas.fillRect(panel);
    canvas.drawRect(panel);

    // Fake balloon outline (path) — stand-in until spline port
    canvas.setBrush(CanvasColor::rgb(255, 255, 255));
    canvas.setPen(CanvasColor::rgb(0, 0, 0), 2);
    const int cx = width() / 2;
    const int cy = height() / 2 - 40;
    canvas.moveTo(cx - 120, cy);
    canvas.lineTo(cx - 100, cy - 50);
    canvas.lineTo(cx + 100, cy - 50);
    canvas.lineTo(cx + 120, cy);
    canvas.lineTo(cx + 100, cy + 50);
    canvas.lineTo(cx - 100, cy + 50);
    canvas.lineTo(cx - 120, cy);
    canvas.strokeAndFill();

    // Tail
    canvas.moveTo(cx - 20, cy + 50);
    canvas.lineTo(cx - 40, cy + 100);
    canvas.lineTo(cx + 10, cy + 50);
    canvas.strokeAndFill();

    canvas.setFont("Sans Serif", 14, false);
    canvas.setPen(CanvasColor::rgb(0, 0, 0), 1);
    const std::string text = m_demoMessage.toStdString();
    const int tw = canvas.measureTextWidth(text);
    canvas.drawText(cx - tw / 2, cy + 8, text);

    canvas.setFont("Sans Serif", 10, false);
    canvas.setPen(CanvasColor::rgb(80, 80, 80), 1);
    canvas.drawText(50, height() - 24,
                    "Art dir: " COMIC_ART_DIR);
}
