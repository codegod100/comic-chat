// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "engine/traj.h"

typedef double MATRIX[4][4];

class CSpline : public CSeg {
public:
    BOOL closed;
    MATRIX *matrix;
    POINT *bezpts;
    int nCps;
    POINT *cps;

    CSpline(POINT cpArray[], int n, BOOL isClosed);
    CSpline(const CSpline &);
    ~CSpline() override;
    virtual int GetDups() = 0;
    void ComputeBezpts();
    virtual int KnotCount() = 0;
    int BezierCount() { return ((3 * KnotCount()) - 8); }
    POINT GetKnot(int index);
    void CvertsToCubic(POINT &, POINT &, POINT &, POINT &, POINT &, POINT &, POINT &, POINT &);
    void CubicToBezier(POINT &, POINT &, POINT &, POINT &, POINT &, POINT &, POINT &, POINT &);
    void Draw(ICanvas *) override;
    void Dash(DASHINFO &) override;
    POINT SegLo() override;
    virtual CSpline *Clone() = 0;
};

class CCardinal : public CSpline {
    static double defaultTension;

public:
    double tension;

    CCardinal(const CCardinal &);
    CCardinal(POINT cpArray[], int n, BOOL isClosed);
    void SetMatrix(double tension);
    int GetDups() override { return 2; }
    int KnotCount() override { return (closed ? nCps + 3 : nCps + 2); }
    CSpline *Clone() override { return (new CCardinal(*this)); }
};

class CBeta : public CSpline {
    static double defaultTension;
    static double defaultBias;

public:
    double tension;
    double bias;

    CBeta(const CBeta &);
    CBeta(POINT cpArray[], int n, BOOL isClosed);
    void SetMatrix(double tension, double bias);
    int GetDups() override { return 3; }
    int KnotCount() override { return (closed ? nCps + 3 : nCps + 4); }
    CSpline *Clone() override { return (new CBeta(*this)); }
};

void DestroySplineMatrixCaches();
