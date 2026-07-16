// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// ComicImage replaces CDIB for the Qt port (QImage-backed).

#pragma once

#include "platform/ICanvas.h"
#include "platform/types.h"

#include <QImage>
#include <cstdio>
#include <string>

class ComicImage : public IImage {
public:
    ComicImage() = default;
    explicit ComicImage(QImage img);

    int width() const override;
    int height() const override;

    bool isNull() const;
    const QImage &qimage() const { return m_img; }
    QImage &qimage() { return m_img; }

    // Load a full BMP/DIB from a filesystem path.
    bool loadFile(const std::string &path);
    // Load any format QImage understands (PNG/JPEG/…) from memory.
    bool loadFromData(const unsigned char *data, int size, const char *formatHint = nullptr);
    // Load a BMP stream starting at the current FILE position (used inside .avb).
    bool loadFromBmpStream(FILE *fp);

    // Replace contents with a QImage (ARGB32).
    void setQImage(QImage img);

    // Comic Chat GDI-style masks: white in mask = transparent, black = keep.
    void applyMask(const ComicImage &mask);
    // Color-key transparency (exact RGB).
    void makeColorKey(COLORREF key);
    // Treat near-white pixels as transparent (1bpp line art / SRCAND style).
    void makeWhiteTransparent(int threshold = 250);
    // For unmasked line art: flood-fill exterior, white-fill enclosed regions,
    // solid black ink — so open chests aren't "wallpaper showing through".
    void fillLineArtInteriors(int threshold = 250);

    // Draw helpers onto ICanvas (expects QtCanvas under the hood).
    // x,y = top-left of destination in canvas logical coords when scale is +Y-down;
    // with ComicScene's flipped Y, callers pass bottom-left and positive h carefully.
    void draw(ICanvas *canvas, int x, int y) const;
    void draw(ICanvas *canvas, int x, int y, int w, int h) const;

private:
    QImage m_img;
};
