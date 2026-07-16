// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "engine/image.h"

#include "platform/QtCanvas.h"

#include <QFile>
#include <algorithm>
#include <vector>

#pragma pack(push, 1)
struct BmpFileHeader {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
};
#pragma pack(pop)

ComicImage::ComicImage(QImage img)
    : m_img(std::move(img))
{
}

int ComicImage::width() const
{
    return m_img.width();
}

int ComicImage::height() const
{
    return m_img.height();
}

bool ComicImage::isNull() const
{
    return m_img.isNull();
}

bool ComicImage::loadFile(const std::string &path)
{
    QImage img;
    if (!img.load(QString::fromStdString(path))) {
        return false;
    }
    m_img = img.convertToFormat(QImage::Format_ARGB32);
    return true;
}

bool ComicImage::loadFromBmpStream(FILE *fp)
{
    if (!fp) {
        return false;
    }
    const long start = ftell(fp);
    BmpFileHeader hdr{};
    if (fread(&hdr, 1, sizeof(hdr), fp) != sizeof(hdr)) {
        return false;
    }
    if (hdr.bfType != 0x4D42) {
        return false;
    }
    // bfSize is total file size; for embedded streams this is the BMP blob size.
    if (hdr.bfSize < sizeof(hdr)) {
        return false;
    }
    if (fseek(fp, start, SEEK_SET) != 0) {
        return false;
    }
    std::vector<unsigned char> buf(hdr.bfSize);
    if (fread(buf.data(), 1, hdr.bfSize, fp) != hdr.bfSize) {
        return false;
    }
    QImage img;
    if (!img.loadFromData(buf.data(), static_cast<int>(buf.size()), "BMP")) {
        return false;
    }
    m_img = img.convertToFormat(QImage::Format_ARGB32);
    return true;
}

void ComicImage::applyMask(const ComicImage &mask)
{
    if (m_img.isNull() || mask.isNull()) {
        return;
    }
    // Comic Chat 1bpp: mask white = outside (transparent), mask black = inside.
    // Inside the mask: drawing black = ink, drawing white = solid white fill
    // (skin / interior). Making interior white transparent was punching holes
    // so the backdrop showed through the face.
    QImage m = mask.qimage().convertToFormat(QImage::Format_ARGB32);
    m_img = m_img.convertToFormat(QImage::Format_ARGB32);
    if (m.size() != m_img.size()) {
        m = m.scaled(m_img.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }
    const int w = m_img.width();
    const int h = m_img.height();
    for (int y = 0; y < h; ++y) {
        auto *dst = reinterpret_cast<QRgb *>(m_img.scanLine(y));
        const auto *ms = reinterpret_cast<const QRgb *>(m.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            if (qGray(ms[x]) > 128) {
                dst[x] = qRgba(0, 0, 0, 0); // outside silhouette
            } else if (qGray(dst[x]) >= 250) {
                dst[x] = qRgba(255, 255, 255, 255); // interior fill (skin)
            } else {
                dst[x] = qRgba(0, 0, 0, 255); // ink
            }
        }
    }
}

void ComicImage::makeColorKey(COLORREF key)
{
    if (m_img.isNull()) {
        return;
    }
    m_img = m_img.convertToFormat(QImage::Format_ARGB32);
    const int kr = static_cast<int>(key & 0xff);
    const int kg = static_cast<int>((key >> 8) & 0xff);
    const int kb = static_cast<int>((key >> 16) & 0xff);
    for (int y = 0; y < m_img.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(m_img.scanLine(y));
        for (int x = 0; x < m_img.width(); ++x) {
            if (qRed(line[x]) == kr && qGreen(line[x]) == kg && qBlue(line[x]) == kb) {
                line[x] = qRgba(0, 0, 0, 0);
            }
        }
    }
}

void ComicImage::makeWhiteTransparent(int threshold)
{
    if (m_img.isNull()) {
        return;
    }
    // Unmasked poses (many torsos): GDI SRCAND — white clear, black solid ink.
    // No white body fill is stored in those bitmaps (outline + solid black only).
    m_img = m_img.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < m_img.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(m_img.scanLine(y));
        for (int x = 0; x < m_img.width(); ++x) {
            if (qRed(line[x]) >= threshold && qGreen(line[x]) >= threshold &&
                qBlue(line[x]) >= threshold) {
                line[x] = qRgba(0, 0, 0, 0);
            } else {
                line[x] = qRgba(0, 0, 0, 255);
            }
        }
    }
}

void ComicImage::draw(ICanvas *canvas, int x, int y) const
{
    if (!canvas || m_img.isNull()) {
        return;
    }
    // Temporary QtImage view — drawImage copies pixels via QPainter.
    QtImage qi(m_img);
    canvas->drawImage(qi, x, y);
}

void ComicImage::draw(ICanvas *canvas, int x, int y, int w, int h) const
{
    if (!canvas || m_img.isNull()) {
        return;
    }
    QtImage qi(m_img);
    canvas->drawImage(qi, x, y, w, h);
}
