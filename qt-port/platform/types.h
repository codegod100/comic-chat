// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// Portable stand-ins for the Win32/MFC types the comic engine historically used.
// Prefer these in engine/ so files need not include Qt or Windows headers.

#pragma once

#include <cstdint>
#include <cstring>

using BOOL = int;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

using BYTE = unsigned char;
using UCHAR = unsigned char;
using SHORT = int16_t;
using USHORT = uint16_t;
using UINT = unsigned int;
using DWORD = uint32_t;
using LONG = int32_t;
using WORD = uint16_t;
using COLORREF = uint32_t;

#ifndef RGB
inline constexpr COLORREF RGB(int r, int g, int b)
{
    return static_cast<COLORREF>((r & 0xff) | ((g & 0xff) << 8) | ((b & 0xff) << 16));
}
#endif

struct POINT {
    LONG x = 0;
    LONG y = 0;
};

struct SIZE {
    LONG cx = 0;
    LONG cy = 0;
};

struct RECT {
    LONG left = 0;
    LONG top = 0;
    LONG right = 0;
    LONG bottom = 0;
};

// Short-integer rect used by balloon/layout code (historical SRECT).
// Field names match the original (PascalCase) for easier porting.
struct SRECT {
    SHORT Left = 0;
    SHORT Top = 0;
    SHORT Right = 0;
    SHORT Bottom = 0;
};

inline void SetRect(RECT *r, int l, int t, int ri, int b)
{
    r->left = l;
    r->top = t;
    r->right = ri;
    r->bottom = b;
}

inline void OffsetRect(RECT *r, int dx, int dy)
{
    r->left += dx;
    r->top += dy;
    r->right += dx;
    r->bottom += dy;
}

inline int RectWidth(const RECT &r) { return r.right - r.left; }
inline int RectHeight(const RECT &r) { return r.bottom - r.top; }

// Comic page logical units (from original common.h).
#ifndef UNITSPERINCH
#define UNITSPERINCH 1440
#endif
