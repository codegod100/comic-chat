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
    QImage m = mask.qimage().convertToFormat(QImage::Format_ARGB32);
    m_img = m_img.convertToFormat(QImage::Format_ARGB32);
    const int w = std::min(m_img.width(), m.width());
    const int h = std::min(m_img.height(), m.height());
    for (int y = 0; y < h; ++y) {
        auto *dst = reinterpret_cast<QRgb *>(m_img.scanLine(y));
        const auto *src = reinterpret_cast<const QRgb *>(m.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            // Mask: near-black => transparent (historical mono mask).
            const int gray = qGray(src[x]);
            if (gray < 16) {
                dst[x] = qRgba(0, 0, 0, 0);
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
