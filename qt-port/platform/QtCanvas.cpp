// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "platform/QtCanvas.h"

#include <QFontMetrics>

#include <cmath>
#include <stdexcept>

QtCanvas::QtCanvas(QPainter *painter)
    : m_painter(painter)
    , m_ownsPainter(false)
{
    if (!m_painter) {
        throw std::invalid_argument("QtCanvas: null QPainter");
    }
    m_painter->setRenderHint(QPainter::Antialiasing, true);
    m_painter->setRenderHint(QPainter::TextAntialiasing, true);
}

QtCanvas::QtCanvas(int width, int height, QImage::Format format)
    : m_ownsPainter(true)
{
    m_ownedImage = std::make_unique<QImage>(width, height, format);
    m_ownedImage->fill(Qt::white);
    m_painter = new QPainter(m_ownedImage.get());
    m_painter->setRenderHint(QPainter::Antialiasing, true);
    m_painter->setRenderHint(QPainter::TextAntialiasing, true);
}

QtCanvas::~QtCanvas()
{
    if (m_ownsPainter && m_painter) {
        m_painter->end();
        delete m_painter;
        m_painter = nullptr;
    }
}

QColor QtCanvas::toQColor(const CanvasColor &c)
{
    return QColor(c.r, c.g, c.b, c.a);
}

QPointF QtCanvas::mapPoint(int x, int y) const
{
    return QPointF(m_ox + x * m_sx, m_oy + y * m_sy);
}

QRectF QtCanvas::mapRect(const RECT &r) const
{
    const QPointF tl = mapPoint(r.left, r.top);
    const QPointF br = mapPoint(r.right, r.bottom);
    return QRectF(tl, br).normalized();
}

void QtCanvas::save()
{
    m_painter->save();
    m_fontStack.push(m_painter->font());
}

void QtCanvas::restore()
{
    m_painter->restore();
    if (!m_fontStack.empty()) {
        m_painter->setFont(m_fontStack.top());
        m_fontStack.pop();
    }
}

void QtCanvas::setClipRect(const RECT &r)
{
    m_painter->setClipRect(mapRect(r));
}

void QtCanvas::resetClip()
{
    m_painter->setClipping(false);
}

void QtCanvas::setPen(const CanvasColor &c, int widthLogical)
{
    const qreal w = std::max(0.5, std::abs(widthLogical * m_sx));
    m_painter->setPen(QPen(toQColor(c), w));
}

void QtCanvas::setBrush(const CanvasColor &c)
{
    m_painter->setBrush(toQColor(c));
}

void QtCanvas::setNoBrush()
{
    m_painter->setBrush(Qt::NoBrush);
}

void QtCanvas::setFont(const std::string &family, int pointSize, bool bold)
{
    QFont f(QString::fromStdString(family));
    f.setPointSize(std::max(1, pointSize));
    f.setBold(bold);
    m_painter->setFont(f);
}

void QtCanvas::beginPath()
{
    m_path = QPainterPath();
    m_pathActive = true;
}

void QtCanvas::closePath()
{
    if (m_pathActive) {
        m_path.closeSubpath();
    }
}

void QtCanvas::moveTo(int x, int y)
{
    m_current = mapPoint(x, y);
    if (!m_pathActive) {
        m_path = QPainterPath();
        m_pathActive = true;
    }
    m_path.moveTo(m_current);
}

void QtCanvas::lineTo(int x, int y)
{
    if (!m_pathActive) {
        m_path = QPainterPath();
        m_path.moveTo(m_current);
        m_pathActive = true;
    }
    m_current = mapPoint(x, y);
    m_path.lineTo(m_current);
}

void QtCanvas::cubicTo(int c1x, int c1y, int c2x, int c2y, int ex, int ey)
{
    if (!m_pathActive) {
        m_path = QPainterPath();
        m_path.moveTo(m_current);
        m_pathActive = true;
    }
    const QPointF c1 = mapPoint(c1x, c1y);
    const QPointF c2 = mapPoint(c2x, c2y);
    m_current = mapPoint(ex, ey);
    m_path.cubicTo(c1, c2, m_current);
}

void QtCanvas::polyBezierTo(const POINT *pts, int count)
{
    // GDI PolyBezierTo: groups of 3 points (cp1, cp2, end).
    for (int i = 0; i + 2 < count; i += 3) {
        cubicTo(pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, pts[i + 2].x,
                pts[i + 2].y);
    }
}

void QtCanvas::stroke()
{
    if (m_pathActive) {
        m_painter->strokePath(m_path, m_painter->pen());
        m_path = QPainterPath();
        m_pathActive = false;
    }
}

void QtCanvas::fill()
{
    if (m_pathActive) {
        m_painter->fillPath(m_path, m_painter->brush());
        m_path = QPainterPath();
        m_pathActive = false;
    }
}

void QtCanvas::strokeAndFill()
{
    if (m_pathActive) {
        m_painter->fillPath(m_path, m_painter->brush());
        m_painter->strokePath(m_path, m_painter->pen());
        m_path = QPainterPath();
        m_pathActive = false;
    }
}

void QtCanvas::drawLine(int x1, int y1, int x2, int y2)
{
    m_painter->drawLine(mapPoint(x1, y1), mapPoint(x2, y2));
}

void QtCanvas::drawRect(const RECT &r)
{
    m_painter->setBrush(Qt::NoBrush);
    m_painter->drawRect(mapRect(r));
}

void QtCanvas::fillRect(const RECT &r)
{
    m_painter->fillRect(mapRect(r), m_painter->brush());
}

void QtCanvas::drawEllipse(const RECT &r)
{
    m_painter->setBrush(Qt::NoBrush);
    m_painter->drawEllipse(mapRect(r));
}

void QtCanvas::fillEllipse(const RECT &r)
{
    m_painter->drawEllipse(mapRect(r));
}

void QtCanvas::drawText(int x, int y, const std::string &utf8)
{
    // GDI TextOut uses baseline-ish left origin; QPainter drawText with
    // a point draws on the baseline when using the default.
    const QPointF p = mapPoint(x, y);
    m_painter->drawText(p, QString::fromUtf8(utf8.data(), int(utf8.size())));
}

int QtCanvas::measureTextWidth(const std::string &utf8)
{
    const QFontMetrics fm(m_painter->font());
    const int px = fm.horizontalAdvance(QString::fromUtf8(utf8.data(), int(utf8.size())));
    if (m_sx == 0.0) {
        return 0;
    }
    return static_cast<int>(std::lround(px / m_sx));
}

TextMetrics QtCanvas::fontMetrics()
{
    const QFontMetrics fm(m_painter->font());
    TextMetrics m;
    if (m_sy == 0.0) {
        return m;
    }
    m.ascent = static_cast<int>(std::lround(fm.ascent() / m_sy));
    m.descent = static_cast<int>(std::lround(fm.descent() / m_sy));
    m.leading = static_cast<int>(std::lround(fm.leading() / m_sy));
    m.height = m.ascent + m.descent;
    return m;
}

void QtCanvas::drawImage(const IImage &img, int destX, int destY)
{
    const auto *qi = dynamic_cast<const QtImage *>(&img);
    if (!qi) {
        return;
    }
    const QImage &src = qi->qimage();
    m_painter->drawImage(mapPoint(destX, destY), src);
}

void QtCanvas::drawImage(const IImage &img,
                         int destX, int destY, int destW, int destH)
{
    const auto *qi = dynamic_cast<const QtImage *>(&img);
    if (!qi) {
        return;
    }
    const RECT r{destX, destY, destX + destW, destY + destH};
    m_painter->drawImage(mapRect(r), qi->qimage());
}

void QtCanvas::drawImageKeyed(const IImage &img,
                              int destX, int destY,
                              COLORREF transparent)
{
    const auto *qi = dynamic_cast<const QtImage *>(&img);
    if (!qi) {
        return;
    }
    QImage src = qi->qimage().convertToFormat(QImage::Format_ARGB32);
    const QRgb key = qRgb(static_cast<int>(transparent & 0xff),
                          static_cast<int>((transparent >> 8) & 0xff),
                          static_cast<int>((transparent >> 16) & 0xff));
    for (int y = 0; y < src.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(src.scanLine(y));
        for (int x = 0; x < src.width(); ++x) {
            if ((line[x] & 0x00ffffff) == (key & 0x00ffffff)) {
                line[x] = qRgba(0, 0, 0, 0);
            }
        }
    }
    m_painter->drawImage(mapPoint(destX, destY), src);
}

void QtCanvas::setLogicalScale(double sx, double sy)
{
    m_sx = sx;
    m_sy = sy;
}

void QtCanvas::setLogicalOrigin(int x, int y)
{
    m_ox = x;
    m_oy = y;
}
