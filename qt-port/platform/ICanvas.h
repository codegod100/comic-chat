// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// Abstract drawing surface replacing MFC CDC / GDI for the comic engine.

#pragma once

#include "platform/types.h"

#include <string>

// Simple RGB color (same packing as COLORREF: 0x00bbggrr).
struct CanvasColor {
    int r = 0;
    int g = 0;
    int b = 0;
    int a = 255;

    static CanvasColor fromCOLORREF(COLORREF c)
    {
        return CanvasColor{static_cast<int>(c & 0xff),
                           static_cast<int>((c >> 8) & 0xff),
                           static_cast<int>((c >> 16) & 0xff),
                           255};
    }

    static CanvasColor rgb(int r, int g, int b, int a = 255)
    {
        return CanvasColor{r, g, b, a};
    }
};

struct TextMetrics {
    int ascent = 0;
    int descent = 0;
    int height = 0;   // ascent + descent (+ leading if desired)
    int leading = 0;
};

// Opaque image handle for blits (implemented as QImage in QtCanvas).
class IImage {
public:
    virtual ~IImage() = default;
    virtual int width() const = 0;
    virtual int height() const = 0;
};

class ICanvas {
public:
    virtual ~ICanvas() = default;

    // --- state ---
    virtual void save() = 0;
    virtual void restore() = 0;
    virtual void setClipRect(const RECT &r) = 0;
    virtual void resetClip() = 0;

    virtual void setPen(const CanvasColor &c, int widthLogical = 1) = 0;
    virtual void setBrush(const CanvasColor &c) = 0;
    virtual void setNoBrush() = 0;
    virtual void setFont(const std::string &family, int pointSize, bool bold = false) = 0;

    // --- path / primitives (logical coordinates) ---
    virtual void beginPath() = 0;
    virtual void closePath() = 0;
    virtual void moveTo(int x, int y) = 0;
    virtual void lineTo(int x, int y) = 0;
    // Cubic Bézier: control1, control2, end (GDI PolyBezierTo segment).
    virtual void cubicTo(int c1x, int c1y, int c2x, int c2y, int ex, int ey) = 0;
    // GDI-style: pts[0..count) is sequence of (c1,c2,end)* for PolyBezierTo.
    virtual void polyBezierTo(const POINT *pts, int count) = 0;
    virtual void stroke() = 0;   // stroke current path and clear it
    virtual void fill() = 0;     // fill current path and clear it
    virtual void strokeAndFill() = 0;

    virtual void drawLine(int x1, int y1, int x2, int y2) = 0;
    virtual void drawRect(const RECT &r) = 0;
    virtual void fillRect(const RECT &r) = 0;
    virtual void drawEllipse(const RECT &r) = 0;
    virtual void fillEllipse(const RECT &r) = 0;

    // --- text ---
    virtual void drawText(int x, int y, const std::string &utf8) = 0;
    // Baseline origin, matching typical GDI TextOut in our ported call sites.
    virtual int measureTextWidth(const std::string &utf8) = 0;
    virtual TextMetrics fontMetrics() = 0;

    // --- images ---
    virtual void drawImage(const IImage &img, int destX, int destY) = 0;
    virtual void drawImage(const IImage &img,
                           int destX, int destY, int destW, int destH) = 0;
    // Color-key style blit: pixels matching transparent are skipped.
    virtual void drawImageKeyed(const IImage &img,
                                int destX, int destY,
                                COLORREF transparent) = 0;

    // --- transform helpers (optional for TWIPS → device) ---
    // Scale logical (TWIPS) coords to device pixels. Identity if unset.
    virtual void setLogicalScale(double sx, double sy) = 0;
    virtual void setLogicalOrigin(int x, int y) = 0;
};
