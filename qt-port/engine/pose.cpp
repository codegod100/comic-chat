// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "engine/pose.h"
#include "engine/art_paths.h"

#include <cstdio>
#include <map>
#include <memory>
#include <vector>

static std::string g_avatarDir;
static std::vector<std::unique_ptr<AVFileRec>> g_avRec;
static std::map<unsigned short, std::unique_ptr<CPose>> g_poseMap;

CPose::~CPose()
{
    delete m_drawing;
    delete m_mask;
    delete m_aura;
}

void CPose::drawMasked(ICanvas *canvas, int x, int y, int w, int h, bool flipH) const
{
    if (!m_drawing || m_drawing->isNull()) {
        return;
    }
    ComicImage tmp = *m_drawing;
    if (m_mask && !m_mask->isNull()) {
        // Masked heads: white skin fill + black ink inside silhouette
        tmp.applyMask(*m_mask);
    } else {
        // Unmasked torsos: white-fill enclosed regions, clear exterior, black ink
        tmp.fillLineArtInteriors();
    }
    if (flipH && !tmp.isNull()) {
        // Horizontal mirror — classic Comic Chat m_flip / StretchBlt negative width.
        tmp.qimage() = tmp.qimage().flipped(Qt::Horizontal);
    }
    tmp.draw(canvas, x, y, w, h);
}

void setAvatarArtDir(const std::string &dir)
{
    g_avatarDir = dir;
}

const std::string &avatarArtDir()
{
    return g_avatarDir;
}

USHORT RegisterAVFileRec(UINT fgndOffset, UINT transOffset, UINT auraOffset,
                         const std::string &pathBase)
{
    auto rec = std::make_unique<AVFileRec>();
    rec->filename = pathBase;
    rec->fgndOffset = fgndOffset;
    rec->transOffset = transOffset;
    rec->auraOffset = auraOffset;
    g_avRec.push_back(std::move(rec));
    return static_cast<USHORT>(g_avRec.size() - 1);
}

CPose *GetPoseFromID(unsigned short poseID, BOOL loadMask)
{
    auto it = g_poseMap.find(poseID);
    if (it != g_poseMap.end()) {
        return it->second.get();
    }
    if (poseID >= g_avRec.size() || !g_avRec[poseID]) {
        return nullptr;
    }

    AVFileRec *arec = g_avRec[poseID].get();
    const std::string path = joinPath(g_avatarDir, arec->filename + ".avb");
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) {
        return nullptr;
    }

    auto pose = std::make_unique<CPose>();
    pose->m_drawing = new ComicImage;
    if (fseek(fp, static_cast<long>(arec->fgndOffset), SEEK_SET) != 0 ||
        !pose->m_drawing->loadFromBmpStream(fp)) {
        fclose(fp);
        return nullptr;
    }

    if (loadMask) {
        if (arec->transOffset) {
            pose->m_mask = new ComicImage;
            if (fseek(fp, static_cast<long>(arec->transOffset), SEEK_SET) != 0 ||
                !pose->m_mask->loadFromBmpStream(fp)) {
                delete pose->m_mask;
                pose->m_mask = nullptr;
            }
        }
        if (arec->auraOffset) {
            pose->m_aura = new ComicImage;
            if (fseek(fp, static_cast<long>(arec->auraOffset), SEEK_SET) != 0 ||
                !pose->m_aura->loadFromBmpStream(fp)) {
                delete pose->m_aura;
                pose->m_aura = nullptr;
            }
        }
    }
    fclose(fp);

    CPose *raw = pose.get();
    g_poseMap[poseID] = std::move(pose);
    return raw;
}

void FlushAllPoses()
{
    g_poseMap.clear();
    g_avRec.clear();
    // Keep a null slot so pose IDs from RegisterAVFileRec still start at 0 after flush
}
