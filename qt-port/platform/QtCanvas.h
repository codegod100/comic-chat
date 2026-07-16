// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "platform/ICanvas.h"

#include <QFont>
#include <QImage>
#include <QPainter>
#include <QPainterPath>

#include <memory>
#include <stack>

class QtImage : public IImage {
public:
    explicit QtImage(QImage img) : m_img(std::move(img)) {}

    int width() const override { return m_img.width(); }
    int height() const override { return m_img.height(); }
    const QImage &qimage() const { return m_img; }
    QImage &qimage() { return m_img; }

private:
    QImage m_img;
};

// Paints into a QPaintDevice (QImage or QWidget via QPainter).
class QtCanvas : public ICanvas {
public:
    // Begin painting on an existing painter (e.g. inside paintEvent).
    explicit QtCanvas(QPainter *painter);

    // Own a QImage buffer and paint into it.
    explicit QtCanvas(int width, int height,
                      QImage::Format format = QImage::Format_ARGB32_Premultiplied);

    ~QtCanvas() override;

    QImage *image() { return m_ownedImage.get(); }
    QPainter *painter() { return m_painter; }

    void save() override;
    void restore() override;
    void setClipRect(const RECT &r) override;
    void resetClip() override;

    void setPen(const CanvasColor &c, int widthLogical = 1) override;
    void setBrush(const CanvasColor &c) override;
    void setNoBrush() override;
    void setFont(const std::string &family, int pointSize, bool bold = false) override;

    void beginPath() override;
    void closePath() override;
    void moveTo(int x, int y) override;
    void lineTo(int x, int y) override;
    void cubicTo(int c1x, int c1y, int c2x, int c2y, int ex, int ey) override;
    void polyBezierTo(const POINT *pts, int count) override;
    void stroke() override;
    void fill() override;
    void strokeAndFill() override;

    void drawLine(int x1, int y1, int x2, int y2) override;
    void drawRect(const RECT &r) override;
    void fillRect(const RECT &r) override;
    void drawEllipse(const RECT &r) override;
    void fillEllipse(const RECT &r) override;

    void drawText(int x, int y, const std::string &utf8) override;
    int measureTextWidth(const std::string &utf8) override;
    TextMetrics fontMetrics() override;

    void drawImage(const IImage &img, int destX, int destY) override;
    void drawImage(const IImage &img,
                   int destX, int destY, int destW, int destH) override;
    void drawImageKeyed(const IImage &img,
                        int destX, int destY,
                        COLORREF transparent) override;

    void setLogicalScale(double sx, double sy) override;
    void setLogicalOrigin(int x, int y) override;

private:
    QPointF mapPoint(int x, int y) const;
    QRectF mapRect(const RECT &r) const;
    static QColor toQColor(const CanvasColor &c);

    QPainter *m_painter = nullptr;
    bool m_ownsPainter = false;
    std::unique_ptr<QImage> m_ownedImage;

    QPainterPath m_path;
    bool m_pathActive = false;
    QPointF m_current;

    double m_sx = 1.0;
    double m_sy = 1.0;
    int m_ox = 0;
    int m_oy = 0;

    std::stack<QFont> m_fontStack;
};
