// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "engine/image.h"
#include "platform/types.h"

#include <string>

struct AVFileRec {
    std::string filename; // avatar base name (no .avb)
    UINT fgndOffset = 0;
    UINT transOffset = 0;
    UINT auraOffset = 0;
};

class CPose {
public:
    ComicImage *m_drawing = nullptr;
    ComicImage *m_mask = nullptr;
    ComicImage *m_aura = nullptr;

    CPose() = default;
    ~CPose();

    // Composite drawing+mask into ARGB and blit.
    void drawMasked(ICanvas *canvas, int x, int y, int w, int h) const;
};

void setAvatarArtDir(const std::string &dir);
const std::string &avatarArtDir();

USHORT RegisterAVFileRec(UINT fgndOffset, UINT transOffset, UINT auraOffset,
                         const std::string &pathBase);
CPose *GetPoseFromID(unsigned short poseID, BOOL loadMask = TRUE);
void FlushAllPoses();
