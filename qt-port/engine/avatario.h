// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "platform/types.h"

#include <string>
#include <vector>

// Minimal avatar summary for Phase 2 (full CAvatarX hierarchy comes later).
struct LoadedAvatar {
    std::string name;
    int type = 0; // AT_SIMPLE / AT_COMPLEX
    USHORT iconPose = 0;
    // Simple: body poses. Complex: face + torso first entries for demo.
    std::vector<USHORT> bodyPoses;
    std::vector<USHORT> facePoses;
    std::vector<USHORT> torsoPoses;
    UCHAR flags = 0;
};

#define AF_MAGICNUM 0x81
#define AK_NAME 1
#define AK_FLAGS 2
#define AK_ICON 3
#define AK_NFACES 4
#define AK_NTORSOS 5
#define AK_STARTDATA 6
#define AK_ENDDATA 7
#define AK_STYLE 8
#define AK_NBODIES 9
#define AT_SIMPLE 1
#define AT_COMPLEX 2

// Load avatar by base name (e.g. "anna", "glenda") from avatarArtDir().
// Returns false on failure.
bool LoadAvatarInfo(const std::string &baseName, LoadedAvatar &out);

// Prefer a complex character if available, else any .avb.
bool LoadDemoAvatar(LoadedAvatar &out);
