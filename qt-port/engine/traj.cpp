// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "engine/traj.h"
#include "engine/vector2d.h"

CTraj::~CTraj()
{
    for (CSeg *seg : m_segs) {
        delete seg;
    }
    m_segs.clear();
}

void CTraj::Draw(ICanvas *canvas)
{
    BOOL firstSeg = TRUE;

    canvas->beginPath();
    for (CSeg *seg : m_segs) {
        if (firstSeg) {
            POINT lo = seg->SegLo();
            canvas->moveTo(lo.x, lo.y);
            firstSeg = FALSE;
        }
        seg->Draw(canvas);
    }
    if (m_closed) {
        canvas->closePath();
    }
}

int dashArray[] = {100, 100};

void CTraj::Dash(ICanvas *canvas)
{
    BOOL firstSeg = TRUE;
    DASHINFO d{};

    for (CSeg *seg : m_segs) {
        if (firstSeg) {
            d.lastPoint = seg->SegLo();
            canvas->moveTo(d.lastPoint.x, d.lastPoint.y);
            firstSeg = FALSE;
            d.inDash = TRUE;
            d.partialDist = 0;
            d.arrayIndex = 0;
            d.canvas = canvas;
            d.nIndices = 2;
            d.dashArray = dashArray;
        }
        seg->Dash(d);
    }
}

void CLine::Draw(ICanvas *canvas)
{
    canvas->lineTo(m_hi.x, m_hi.y);
}

void CArc::Draw(ICanvas *canvas)
{
    DrawArc2(canvas, m_lo, m_hi, m_altitude);
}

void CArc::Dash(DASHINFO &d)
{
    void DashArc2(DASHINFO &, POINT &, POINT &, int);
    DashArc2(d, m_lo, m_hi, m_altitude);
}

// To be fast(er), uses manhattan distance rather than euclidean distance.
void DashSeg(POINT &thisPoint, DASHINFO &d)
{
    int nextDist;
    int distLimit = d.dashArray[d.arrayIndex];
    while (TRUE) {
        nextDist = manhattan_dist(d.lastPoint, thisPoint);
        if (nextDist + d.partialDist < d.dashArray[d.arrayIndex])
            break;
        POINT deltaVec = point_sub(thisPoint, d.lastPoint);
        DPOINT deltaVecN;
        deltaVecN.x = (double)deltaVec.x / nextDist;
        deltaVecN.y = (double)deltaVec.y / nextDist;
        POINT interpedPoint = point_add(
            d.lastPoint,
            dpoint_to_point(point_scalmult((double)distLimit - d.partialDist, deltaVecN)));
        if (d.inDash)
            d.canvas->lineTo(interpedPoint.x, interpedPoint.y);
        else
            d.canvas->moveTo(interpedPoint.x, interpedPoint.y);
        d.lastPoint = interpedPoint;
        d.inDash = !(d.inDash);
        d.partialDist = 0;
        d.arrayIndex = (d.arrayIndex + 1) % d.nIndices;
        distLimit = d.dashArray[d.arrayIndex];
    }
    d.partialDist += nextDist;
    if (d.inDash)
        d.canvas->lineTo(thisPoint.x, thisPoint.y);
    d.lastPoint = thisPoint;
}
