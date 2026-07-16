// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "platform/types.h"

#include <string>
#include <vector>

// Placement record from .avb (matches FACEREC / BODYREC fields we need).
struct FaceRec {
    USHORT poseID = 0;
    short xCX = 0;
    short yCX = 0;
    short delta_xCX = 0;
    short delta_yCX = 0;
    UCHAR faceX = 0;
    UCHAR faceY = 0;
};

struct TorsoRec {
    USHORT poseID = 0;
    short xCX = 0;
    short yCX = 0;
};

struct BodyRec {
    USHORT poseID = 0;
    UCHAR faceX = 0;
    UCHAR faceY = 0;
};

// Minimal avatar summary for the Qt port.
struct LoadedAvatar {
    std::string name;
    int type = 0; // AT_SIMPLE / AT_COMPLEX
    USHORT iconPose = 0;
    std::vector<USHORT> bodyPoses; // legacy simple pose ids
    std::vector<USHORT> facePoses;
    std::vector<USHORT> torsoPoses;
    std::vector<FaceRec> faces;
    std::vector<TorsoRec> torsos;
    std::vector<BodyRec> bodies;
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

// avatar flags (from original avatar.h)
#define HEADMASK 1
#define TORSOMASK 2
#define TORSOFIRST 4

bool LoadAvatarInfo(const std::string &baseName, LoadedAvatar &out);
bool LoadDemoAvatar(LoadedAvatar &out);
// Load every .avb under the avatar art directory (skips failures).
std::vector<LoadedAvatar> LoadAllAvatars();
