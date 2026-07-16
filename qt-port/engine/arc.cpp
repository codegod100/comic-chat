// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "engine/traj.h"
#include "engine/vector2d.h"

#include <cmath>

static void ScanArcAux(ICanvas *canvas, DPOINT &A, DPOINT &C, POINT &center, double radius,
                       double angle)
{
    DPOINT B;
    POINT bcps[3];
    double s = cos(angle / 2);
    double tau = 4 * s / (3 * (s + 1));

    double divisor = (A.x * C.y - A.y * C.x) / (radius * radius);
    B.x = (C.y - A.y) / divisor;
    B.y = (A.x - C.x) / divisor;

    DPOINT tauTimesB = point_scalmult(tau, B);

    bcps[0].x = ROUND((1 - tau) * A.x + tauTimesB.x) + center.x;
    bcps[0].y = ROUND((1 - tau) * A.y + tauTimesB.y) + center.y;
    bcps[1].x = ROUND((1 - tau) * C.x + tauTimesB.x) + center.x;
    bcps[1].y = ROUND((1 - tau) * C.y + tauTimesB.y) + center.y;
    bcps[2] = point_add(dpoint_to_point(C), center);
    canvas->polyBezierTo(bcps, 3);
}

#define ARCSTEP (PI / 2)

static void ScanArc(ICanvas *canvas, POINT &absCenter, POINT &start, POINT &end, BOOL ccw = TRUE)
{
    DPOINT A = point_to_dpoint(point_sub(start, absCenter));
    DPOINT FinalC = point_to_dpoint(point_sub(end, absCenter));
    DPOINT C;
    double radius = point_magn(A);
    double trueAngle = angle_between_vecs(FinalC, A);
    if (ccw)
        trueAngle = -trueAngle;
    if (trueAngle <= 0)
        trueAngle += 2 * PI;
    double nextEnd = vector_to_angle(A);
    double step = ccw ? ARCSTEP : -ARCSTEP;
    BOOL doExit = FALSE;

    while (TRUE) {
        if (trueAngle > ARCSTEP) {
            nextEnd += step;
            C = point_scalmult(radius, angle_to_vector(nextEnd));
        } else {
            doExit = TRUE;
            C = FinalC;
            step = trueAngle;
        }
        ScanArcAux(canvas, A, C, absCenter, radius, step);
        if (doExit)
            break;
        A = C;
        trueAngle -= ARCSTEP;
    }
}

// Positive altitude bows the arc to the right of the vector from start to end.
void DrawArc2(ICanvas *canvas, POINT &start, POINT &end, int altitude)
{
    POINT mid, endToMid, midToCenter, absCenter;
    double endToMidDist, radius, midToCenterDist;

    if (altitude < 1 && altitude > -1) {
        canvas->lineTo(end.x, end.y);
        return;
    }

    mid = point_scalmult(0.5, point_add(start, end));
    endToMid = point_sub(mid, end);
    endToMidDist = point_magn(endToMid);
    radius = (endToMidDist * endToMidDist + altitude * altitude) / (2 * altitude);
    midToCenterDist = radius - altitude;

    midToCenter.x = endToMid.y;
    midToCenter.y = -endToMid.x;

    midToCenter = point_scalmult(midToCenterDist / point_magn(midToCenter), midToCenter);
    absCenter = point_add(point_add(end, endToMid), midToCenter);

    ScanArc(canvas, absCenter, start, end, (altitude > 0));
}

#define SAMPLESTEP .02

void DashArc2(DASHINFO &d, POINT &start, POINT &end, int altitude)
{
    POINT mid, endToMid, midToCenter, absCenter;
    double endToMidDist, radius, midToCenterDist;

    if (altitude < 1 && altitude > -1) {
        DashSeg(end, d);
        return;
    }

    mid = point_scalmult(0.5, point_add(start, end));
    endToMid = point_sub(mid, end);
    endToMidDist = point_magn(endToMid);
    radius = (endToMidDist * endToMidDist + altitude * altitude) / (2 * altitude);
    midToCenterDist = radius - altitude;

    midToCenter.x = endToMid.y;
    midToCenter.y = -endToMid.x;
    midToCenter = point_scalmult(midToCenterDist / point_magn(midToCenter), midToCenter);
    absCenter = point_add(point_add(end, endToMid), midToCenter);

    int ccw = (altitude > 0);
    DPOINT A = point_to_dpoint(point_sub(start, absCenter));
    DPOINT FinalC = point_to_dpoint(point_sub(end, absCenter));
    double r = point_magn(A);
    double trueAngle = angle_between_vecs(FinalC, A);
    if (ccw)
        trueAngle = -trueAngle;
    if (trueAngle <= 0)
        trueAngle += 2 * PI;

    double ang = vector_to_angle(A);
    double step = ccw ? SAMPLESTEP : -SAMPLESTEP;
    double remaining = trueAngle;
    while (remaining > SAMPLESTEP) {
        ang += step;
        remaining -= SAMPLESTEP;
        POINT p = point_add(absCenter, dpoint_to_point(point_scalmult(r, angle_to_vector(ang))));
        DashSeg(p, d);
    }
    DashSeg(end, d);
}
