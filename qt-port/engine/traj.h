// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "platform/types.h"
#include "platform/ICanvas.h"

#include <vector>

class CGraphicalObj {
public:
    virtual ~CGraphicalObj() = default;
    virtual void Draw(ICanvas *) = 0;
};

typedef struct {
    BOOL inDash;
    int partialDist;
    int arrayIndex;
    int *dashArray;
    int nIndices;
    POINT lastPoint;
    ICanvas *canvas;
} DASHINFO;

class CSeg : public CGraphicalObj {
public:
    virtual POINT SegLo() = 0;
    virtual void Dash(DASHINFO &) {}
    ~CSeg() override = default;
};

class CLine : public CSeg {
public:
    POINT m_lo;
    POINT m_hi;
    CLine(POINT &lo, POINT &hi)
    {
        m_lo = lo;
        m_hi = hi;
    }
    void Draw(ICanvas *) override;
    POINT SegLo() override { return (m_lo); }
};

class CArc : public CSeg {
public:
    POINT m_lo;
    POINT m_hi;
    int m_altitude;
    CArc(POINT &lo, POINT &hi, int alt)
    {
        m_lo = lo;
        m_hi = hi;
        m_altitude = alt;
    }
    void Draw(ICanvas *) override;
    POINT SegLo() override { return (m_lo); }
    void Dash(DASHINFO &) override;
};

class CTraj : public CGraphicalObj {
public:
    std::vector<CSeg *> m_segs;
    BOOL m_closed;

    CTraj() { m_closed = FALSE; }
    ~CTraj() override;
    void AddSeg(CSeg *seg) { m_segs.push_back(seg); }
    void Draw(ICanvas *) override;
    virtual void Dash(ICanvas *);
};

void DrawArc2(ICanvas *canvas, POINT &start, POINT &end, int altitude);
void DashSeg(POINT &thisPoint, DASHINFO &d);
