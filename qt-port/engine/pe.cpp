// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "engine/pe.h"

// Original implementation lived inline / with panel; provide concrete helpers.

BOOL CPanelElement::SetBBox(int left, int bottom, int right, int top)
{
    m_bbox.Left = static_cast<SHORT>(left);
    m_bbox.Bottom = static_cast<SHORT>(bottom);
    m_bbox.Right = static_cast<SHORT>(right);
    m_bbox.Top = static_cast<SHORT>(top);
    return TRUE;
}

void CPanelElement::GetBBox(RECT *r)
{
    if (!r) {
        return;
    }
    r->left = m_bbox.Left;
    r->bottom = m_bbox.Bottom;
    r->right = m_bbox.Right;
    r->top = m_bbox.Top;
}
