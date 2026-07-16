// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "platform/types.h"
#include "platform/ICanvas.h"

#ifndef UNITSPERINCH
#define UNITSPERINCH 1440
#endif

#define DEFAULTDELTA 100

inline void DrawPoint(ICanvas *canvas, POINT pt, int delta = DEFAULTDELTA)
{
    if (!canvas) {
        return;
    }
    RECT r;
    r.left = pt.x - delta;
    r.right = pt.x + delta;
    r.bottom = pt.y - delta;
    r.top = pt.y + delta;
    canvas->fillEllipse(r);
}
