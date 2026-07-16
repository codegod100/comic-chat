// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "engine/spline.h"
#include "engine/vector2d.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

CSpline::CSpline(POINT cpArray[], int n, BOOL isClosed)
{
    assert(n >= 2);
    nCps = n;
    cps = (POINT *)malloc(nCps * sizeof(POINT));
    for (int i = 0; i < nCps; i++)
        cps[i] = cpArray[i];
    bezpts = NULL;
    closed = isClosed;
    matrix = nullptr;
}

CSpline::CSpline(const CSpline &s)
{
    closed = s.closed;
    matrix = s.matrix;
    nCps = s.nCps;
    if (s.cps) {
        cps = (POINT *)malloc(nCps * sizeof(POINT));
        for (int i = 0; i < nCps; i++)
            cps[i] = s.cps[i];
    } else {
        cps = nullptr;
    }
    bezpts = NULL;
}

CSpline::~CSpline()
{
    if (nCps > 0)
        free(cps);
    if (bezpts)
        free(bezpts);
}

double CCardinal::defaultTension = 0.4;

CCardinal::CCardinal(POINT cpArray[], int n, BOOL isClosed)
    : CSpline(cpArray, n, isClosed)
{
    tension = defaultTension;
    SetMatrix(tension);
    ComputeBezpts();
}

CCardinal::CCardinal(const CCardinal &c)
    : CSpline(c)
{
    tension = c.tension;

    if (c.bezpts) {
        int nBezpts = BezierCount();
        bezpts = (POINT *)malloc(nBezpts * sizeof(POINT));
        for (int i = 0; i < nBezpts; i++)
            bezpts[i] = c.bezpts[i];
    }
}

double CBeta::defaultTension = 5.0;
double CBeta::defaultBias = 1.0;

CBeta::CBeta(POINT cpArray[], int n, BOOL isClosed)
    : CSpline(cpArray, n, isClosed)
{
    tension = defaultTension;
    bias = defaultBias;
    SetMatrix(tension, bias);
    ComputeBezpts();
}

CBeta::CBeta(const CBeta &b)
    : CSpline(b)
{
    tension = b.tension;
    bias = b.bias;

    if (b.bezpts) {
        int nBezpts = BezierCount();
        bezpts = (POINT *)malloc(nBezpts * sizeof(POINT));
        for (int i = 0; i < nBezpts; i++)
            bezpts[i] = b.bezpts[i];
    }
}

static std::map<unsigned, MATRIX *> cardinalMatrixMap;
static std::map<std::string, MATRIX *> betaMatrixMap;

void CCardinal::SetMatrix(double tension)
{
    unsigned key = (unsigned)((float)tension);
    auto it = cardinalMatrixMap.find(key);
    if (it != cardinalMatrixMap.end()) {
        matrix = it->second;
        return;
    }
    matrix = (MATRIX *)malloc(sizeof(MATRIX));
    (*matrix)[0][1] = 2.0 - tension;
    (*matrix)[0][2] = tension - 2.0;
    (*matrix)[1][0] = 2.0 * tension;
    (*matrix)[1][1] = tension - 3.0;
    (*matrix)[1][2] = 3.0 - 2.0 * tension;
    (*matrix)[3][1] = 1.0;
    (*matrix)[0][3] = (*matrix)[2][2] = tension;
    (*matrix)[0][0] = (*matrix)[1][3] = (*matrix)[2][0] = -tension;
    (*matrix)[2][1] = (*matrix)[2][3] = (*matrix)[3][0] = (*matrix)[3][2] =
        (*matrix)[3][3] = 0.0;
    cardinalMatrixMap[key] = matrix;
}

void CBeta::SetMatrix(double tension, double bias)
{
    char keybuf[64];
    std::snprintf(keybuf, sizeof(keybuf), "%f*%f", tension, bias);
    std::string key(keybuf);
    auto it = betaMatrixMap.find(key);
    if (it != betaMatrixMap.end()) {
        matrix = it->second;
        return;
    }

    matrix = (MATRIX *)malloc(sizeof(MATRIX));
    double b2 = bias * bias;
    double b3 = bias * b2;
    double d = 1.0 / (tension + (2.0 * b3) + (4.0 * (b2 + bias)) + 2.0);

    (*matrix)[0][0] = -2.0 * b3;
    (*matrix)[0][1] = 2.0 * (tension + b3 + b2 + bias);
    (*matrix)[0][2] = -2.0 * (tension + b2 + bias + 1.0);
    (*matrix)[1][0] = 6.0 * b3;
    (*matrix)[1][1] = -3.0 * (tension + (2.0 * (b3 + b2)));
    (*matrix)[1][2] = 3.0 * (tension + 2.0 * b2);
    (*matrix)[2][0] = -6.0 * b3;
    (*matrix)[2][1] = 6.0 * (b3 - bias);
    (*matrix)[2][2] = 6.0 * bias;
    (*matrix)[3][0] = 2.0 * b3;
    (*matrix)[3][1] = tension + (4.0 * (b2 + bias));
    (*matrix)[0][3] = (*matrix)[3][2] = 2.0;
    (*matrix)[1][3] = (*matrix)[2][3] = (*matrix)[3][3] = 0.0;

    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            (*matrix)[i][j] *= d;

    betaMatrixMap[key] = matrix;
}

void DestroySplineMatrixCaches()
{
    for (auto &kv : cardinalMatrixMap) {
        free(kv.second);
    }
    cardinalMatrixMap.clear();
    for (auto &kv : betaMatrixMap) {
        free(kv.second);
    }
    betaMatrixMap.clear();
}

void CSpline::ComputeBezpts()
{
    int nKnots = KnotCount();
    assert(nKnots >= 4);

    if (!bezpts)
        bezpts = (POINT *)malloc(BezierCount() * sizeof(POINT));

    int bezIndex = 1;
    POINT knot0 = GetKnot(0);
    POINT knot1 = GetKnot(1);
    POINT knot2 = GetKnot(2);
    POINT knot3 = GetKnot(3);
    POINT c0, c1, c2, c3;
    POINT b0, b1, b2, b3;
    for (int i = 0; 1; i++) {
        CvertsToCubic(knot0, knot1, knot2, knot3, c0, c1, c2, c3);
        CubicToBezier(c0, c1, c2, c3, b0, b1, b2, b3);
        if (i == 0)
            bezpts[0] = b0;
        bezpts[bezIndex] = b1;
        bezpts[bezIndex + 1] = b2;
        bezpts[bezIndex + 2] = b3;
        if (i + 4 == nKnots)
            return;
        bezIndex += 3;
        knot0 = knot1;
        knot1 = knot2;
        knot2 = knot3;
        knot3 = GetKnot(i + 4);
    }
}

void CSpline::CvertsToCubic(POINT &k0, POINT &k1, POINT &k2, POINT &k3, POINT &c0, POINT &c1,
                            POINT &c2, POINT &c3)
{
    c3.x = ROUND((*matrix)[0][0] * k0.x + (*matrix)[0][1] * k1.x + (*matrix)[0][2] * k2.x +
                 (*matrix)[0][3] * k3.x);
    c3.y = ROUND((*matrix)[0][0] * k0.y + (*matrix)[0][1] * k1.y + (*matrix)[0][2] * k2.y +
                 (*matrix)[0][3] * k3.y);
    c2.x = ROUND((*matrix)[1][0] * k0.x + (*matrix)[1][1] * k1.x + (*matrix)[1][2] * k2.x +
                 (*matrix)[1][3] * k3.x);
    c2.y = ROUND((*matrix)[1][0] * k0.y + (*matrix)[1][1] * k1.y + (*matrix)[1][2] * k2.y +
                 (*matrix)[1][3] * k3.y);
    c1.x = ROUND((*matrix)[2][0] * k0.x + (*matrix)[2][1] * k1.x + (*matrix)[2][2] * k2.x +
                 (*matrix)[2][3] * k3.x);
    c1.y = ROUND((*matrix)[2][0] * k0.y + (*matrix)[2][1] * k1.y + (*matrix)[2][2] * k2.y +
                 (*matrix)[2][3] * k3.y);
    c0.x = ROUND((*matrix)[3][0] * k0.x + (*matrix)[3][1] * k1.x + (*matrix)[3][2] * k2.x +
                 (*matrix)[3][3] * k3.x);
    c0.y = ROUND((*matrix)[3][0] * k0.y + (*matrix)[3][1] * k1.y + (*matrix)[3][2] * k2.y +
                 (*matrix)[3][3] * k3.y);
}

void CSpline::CubicToBezier(POINT &c0, POINT &c1, POINT &c2, POINT &c3, POINT &b0, POINT &b1,
                            POINT &b2, POINT &b3)
{
    b0.x = c0.x;
    b0.y = c0.y;
    b1.x = c0.x + ROUND((1.0 / 3.0) * c1.x);
    b1.y = c0.y + ROUND((1.0 / 3.0) * c1.y);
    b2.x = b1.x + ROUND((1.0 / 3.0) * (c1.x + c2.x));
    b2.y = b1.y + ROUND((1.0 / 3.0) * (c1.y + c2.y));
    b3.x = c0.x + c1.x + c2.x + c3.x;
    b3.y = c0.y + c1.y + c2.y + c3.y;
}

POINT CSpline::GetKnot(int index)
{
    if (closed) {
        if (index == 0)
            return cps[nCps - 1];
        else if (index == nCps + 1)
            return cps[0];
        else if (index == nCps + 2)
            return cps[1];
        else
            return cps[index - 1];
    } else {
        int dups = GetDups();
        if (index < dups)
            return cps[0];
        else if (index >= nCps + dups - 2)
            return cps[nCps - 1];
        else
            return cps[index - dups + 1];
    }
}

void CSpline::Draw(ICanvas *canvas)
{
    canvas->polyBezierTo(bezpts + 1, BezierCount() - 1);
}

POINT CSpline::SegLo()
{
    return bezpts[0];
}

void CSpline::Dash(DASHINFO &d)
{
    // Flatten each cubic into line samples for dashing (simple uniform sample).
    int bezCount = BezierCount();
    for (int i = 0; i < bezCount - 1; i += 3) {
        POINT p0 = bezpts[i];
        POINT p1 = bezpts[i + 1];
        POINT p2 = bezpts[i + 2];
        POINT p3 = bezpts[i + 3];
        for (int s = 1; s <= 16; ++s) {
            double t = s / 16.0;
            double u = 1.0 - t;
            double x = u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x +
                       t * t * t * p3.x;
            double y = u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y +
                       t * t * t * p3.y;
            POINT pt{ROUND(x), ROUND(y)};
            DashSeg(pt, d);
        }
    }
}
