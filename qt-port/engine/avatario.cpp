// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "engine/avatario.h"
#include "engine/art_paths.h"
#include "engine/pose.h"

#include <QDir>
#include <algorithm>
#include <cstdio>
#include <cstring>

using INT16 = int16_t;
using INT32 = int32_t;

static INT16 read16(FILE *fp)
{
    INT16 val = 0;
    fread(&val, sizeof(val), 1, fp);
    return val;
}

static INT32 read32(FILE *fp)
{
    INT32 val = 0;
    fread(&val, sizeof(val), 1, fp);
    return val;
}

static UCHAR read8(FILE *fp)
{
    UCHAR val = 0;
    fread(&val, sizeof(val), 1, fp);
    return val;
}

static bool loadBasics(int key, FILE *fp, LoadedAvatar &av, const std::string &pathBase)
{
    switch (key) {
    case AK_NAME: {
        char buff[128];
        int c;
        char *bptr = buff;
        while ((c = fgetc(fp)) != EOF) {
            *bptr++ = static_cast<char>(c);
            if (!c || bptr >= buff + sizeof(buff) - 1)
                break;
        }
        *bptr = 0;
        av.name = buff;
        return true;
    }
    case AK_STYLE:
        (void)read16(fp);
        return true;
    case AK_FLAGS:
        av.flags = static_cast<UCHAR>(read16(fp));
        return true;
    case AK_ICON: {
        int fgnd = read32(fp);
        av.iconPose = RegisterAVFileRec(fgnd, 0, 0, pathBase);
        return true;
    }
    default:
        return false;
    }
}

static short read16s(FILE *fp)
{
    return static_cast<short>(read16(fp));
}

static void loadFaceRecs(FILE *fp, int nFaces, LoadedAvatar &av, const std::string &pathBase)
{
    int lastOffset = 0;
    USHORT lastPose = 0;
    for (int i = 0; i < nFaces; i++) {
        int fgndOffset = read32(fp);
        int transOffset = read32(fp);
        int auraOffset = read32(fp);
        USHORT pose;
        if (fgndOffset != lastOffset) {
            pose = RegisterAVFileRec(fgndOffset, transOffset, auraOffset, pathBase);
            lastOffset = fgndOffset;
            lastPose = pose;
        } else {
            pose = lastPose;
        }
        FaceRec rec;
        rec.poseID = pose;
        (void)read16(fp); // emotion index
        (void)read8(fp);  // intensity
        rec.xCX = read16s(fp);
        rec.yCX = read16s(fp);
        rec.delta_xCX = read16s(fp);
        rec.delta_yCX = read16s(fp);
        rec.faceX = static_cast<UCHAR>(read16(fp));
        rec.faceY = static_cast<UCHAR>(read16(fp));
        BYTE padding[16];
        fread(padding, 1, sizeof(padding), fp);
        av.facePoses.push_back(pose);
        av.faces.push_back(rec);
    }
}

static void loadTorsoRecs(FILE *fp, int nTorsos, LoadedAvatar &av, const std::string &pathBase)
{
    int lastOffset = 0;
    USHORT lastPose = 0;
    for (int i = 0; i < nTorsos; i++) {
        int fgndOffset = read32(fp);
        int transOffset = read32(fp);
        int auraOffset = read32(fp);
        USHORT pose;
        if (fgndOffset != lastOffset) {
            pose = RegisterAVFileRec(fgndOffset, transOffset, auraOffset, pathBase);
            lastOffset = fgndOffset;
            lastPose = pose;
        } else {
            pose = lastPose;
        }
        TorsoRec rec;
        rec.poseID = pose;
        (void)read16(fp);
        (void)read8(fp);
        rec.xCX = read16s(fp);
        rec.yCX = read16s(fp);
        BYTE padding[16];
        fread(padding, 1, sizeof(padding), fp);
        av.torsoPoses.push_back(pose);
        av.torsos.push_back(rec);
    }
}

static void loadBodyRecs(FILE *fp, int nBodies, LoadedAvatar &av, const std::string &pathBase)
{
    int lastOffset = 0;
    USHORT lastPose = 0;
    for (int i = 0; i < nBodies; i++) {
        int fgndOffset = read32(fp);
        int transOffset = read32(fp);
        int auraOffset = read32(fp);
        USHORT pose;
        if (fgndOffset != lastOffset) {
            pose = RegisterAVFileRec(fgndOffset, transOffset, auraOffset, pathBase);
            lastOffset = fgndOffset;
            lastPose = pose;
        } else {
            pose = lastPose;
        }
        BodyRec rec;
        rec.poseID = pose;
        (void)read16(fp);
        (void)read8(fp);
        rec.faceX = static_cast<UCHAR>(read16(fp));
        rec.faceY = static_cast<UCHAR>(read16(fp));
        BYTE padding[16];
        fread(padding, 1, sizeof(padding), fp);
        av.bodyPoses.push_back(pose);
        av.bodies.push_back(rec);
    }
}

bool LoadAvatarInfo(const std::string &baseName, LoadedAvatar &out)
{
    out = LoadedAvatar{};
    out.name = baseName;
    const std::string path = joinPath(avatarArtDir(), baseName + ".avb");
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) {
        return false;
    }

    int magicNum = read16(fp);
    int avType = read16(fp);
    int version = read16(fp);
    (void)version;
    if (magicNum != AF_MAGICNUM) {
        fclose(fp);
        return false;
    }
    out.type = avType;

    bool ok = true;
    while (ok) {
        int key = read16(fp);
        if (feof(fp)) {
            break;
        }
        if (loadBasics(key, fp, out, baseName)) {
            continue;
        }
        switch (key) {
        case AK_NFACES: {
            int n = read16(fp);
            loadFaceRecs(fp, n, out, baseName);
            break;
        }
        case AK_NTORSOS: {
            int n = read16(fp);
            loadTorsoRecs(fp, n, out, baseName);
            break;
        }
        case AK_NBODIES: {
            int n = read16(fp);
            loadBodyRecs(fp, n, out, baseName);
            break;
        }
        case AK_STARTDATA:
            fclose(fp);
            if (out.name.empty()) {
                out.name = baseName;
            }
            return true;
        default:
            // Unknown key — stop to avoid desync
            ok = false;
            break;
        }
    }
    fclose(fp);
    return !out.facePoses.empty() || !out.bodyPoses.empty() || !out.torsoPoses.empty();
}

bool LoadDemoAvatar(LoadedAvatar &out)
{
    auto all = LoadAllAvatars();
    if (all.empty()) {
        return false;
    }
    out = std::move(all.front());
    return true;
}

std::vector<LoadedAvatar> LoadAllAvatars()
{
    std::vector<LoadedAvatar> result;

    // Prefer a stable, human-friendly order for the first assignments.
    static const char *prefer[] = {
        "anna",    "dan",    "denise", "connor", "glenda", "bolo",  "cro",
        "armando", "hugh",   "jordan", "lance",  "lynnea", "margaret", "mike",
        "susan",   "tiki",   "tux",    "waf",    "xeno",   "rainbow",  "pedagog",
        "tongtyed", nullptr};

    std::vector<std::string> names;
    for (int i = 0; prefer[i]; ++i) {
        names.emplace_back(prefer[i]);
    }

    QDir dir(QString::fromStdString(avatarArtDir()));
    const QStringList avbs = dir.entryList(QStringList() << "*.avb", QDir::Files, QDir::Name);
    for (const QString &f : avbs) {
        QString base = f;
        if (base.endsWith(QLatin1String(".avb"), Qt::CaseInsensitive)) {
            base.chop(4);
        }
        const std::string s = base.toStdString();
        if (std::find(names.begin(), names.end(), s) == names.end()) {
            names.push_back(s);
        }
    }

    for (const auto &name : names) {
        LoadedAvatar av;
        if (LoadAvatarInfo(name, av)) {
            // Skip empties that parse but have no drawable poses
            if (!av.faces.empty() || !av.torsos.empty() || !av.bodies.empty() ||
                !av.bodyPoses.empty()) {
                result.push_back(std::move(av));
            }
        }
    }
    return result;
}
