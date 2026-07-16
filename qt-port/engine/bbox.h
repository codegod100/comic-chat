// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "platform/types.h"
#include "engine/vector2d.h"

void adjust_bbox(RECT *bbox, int delta);
void bbox_around_pt(RECT *bbox, POINT *pt, int delta = 0);
void bbox_in_bbox(RECT *source, RECT *dest);
void include_pt_in_bbox(POINT *pt, RECT *bbox);
void include_pt_in_bbox(POINT *pt, SRECT *bbox);
BOOL inside_bbox(POINT *pt, RECT *bbox);
BOOL inside_bbox(POINT *pt, SRECT *bbox);
BOOL inside_bbox_tol(POINT *pt, RECT *bbox, int tol);
BOOL bbox_overlap(RECT *bbox1, RECT *bbox2);
BOOL bbox_within_bbox(RECT *bbox1, RECT *bbox2);
BOOL is_empty(RECT *bbox);
void make_empty(RECT *bbox);
void make_empty(SRECT *bbox);
BOOL bbox_intersect(RECT *bbox1, RECT *bbox2, RECT *result);
RECT SRECTToRECT(SRECT &s);
