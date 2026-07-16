// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// Comic strip scene: panels, balloons, multi-avatar nick mapping.

#pragma once

#include "engine/avatario.h"
#include "engine/chat_constants.h"
#include "engine/image.h"
#include "engine/pose.h"
#include "platform/ICanvas.h"
#include "platform/types.h"

#include <map>
#include <string>
#include <vector>

#ifndef UNIT_PANEL_W
#define UNIT_PANEL_W 4860
#define UNIT_PANEL_H 4860
#endif

struct WrappedLine {
    std::string text;
    int width = 0;
};

struct SceneBalloon {
    std::string text;
    std::string nick;
    UCHAR mode = SM_SAY;
    std::vector<WrappedLine> lines;
    RECT textBox{};
    RECT cloudBox{};
    int speakerArrowX = 0;
    int speakerTop = 0;
};

struct SceneBody {
    USHORT facePose = 0;
    USHORT torsoPose = 0;
    USHORT bodyPose = 0;
    int type = AT_COMPLEX;
    UCHAR flags = 0;
    short face_xCX = 0, face_yCX = 0, face_dx = 0, face_dy = 0;
    short torso_xCX = 0, torso_yCX = 0;
    RECT box{};
    RECT headBox{};
    RECT torsoBox{};
    int arrowX = 0;
    std::string avatarName; // art character name (anna, dan, …)
};

struct ScenePanel {
    SceneBody body;
    std::vector<SceneBalloon> balloons;
    unsigned seed = 1;
};

class ComicScene {
public:
    ComicScene();

    // Backdrop + cast of characters available for nick assignment.
    void setArt(std::vector<LoadedAvatar> avatars, const ComicImage &backdrop);
    // Convenience: single avatar cast.
    void setArt(const LoadedAvatar &avatar, const ComicImage &backdrop);

    void clear();

    // Add a spoken line. Nick is mapped to a stable character from the cast.
    void addLine(const std::string &text, UCHAR mode = SM_SAY,
                 const std::string &nick = "you");

    int panelCount() const { return static_cast<int>(m_panels.size()); }
    int avatarCount() const { return static_cast<int>(m_avatars.size()); }
    int unitWidth() const { return UNIT_PANEL_W; }
    int unitHeight() const { return UNIT_PANEL_H; }

    static int panelSideForHeight(int contentHeight);
    int contentWidthForHeight(int contentHeight) const;
    int contentHeightForHeight(int contentHeight) const;

    void draw(ICanvas *canvas, const RECT &dest) const;

    const std::string &status() const { return m_status; }

    // Which art character is assigned to this nick (empty if none yet).
    std::string avatarNameForNick(const std::string &nick) const;

private:
    void layoutPanel(ScenePanel &panel);
    void layoutBalloon(SceneBalloon &b, const SceneBody &body);
    std::vector<WrappedLine> wrapText(const std::string &text, int maxWidthLogical) const;
    int measureLogical(const std::string &s) const;
    void drawPanel(ICanvas *canvas, const ScenePanel &panel, const RECT &pixelRect) const;
    void drawBody(ICanvas *canvas, const SceneBody &body) const;
    void drawBalloon(ICanvas *canvas, const SceneBalloon &b) const;

    // Lowercase nick → index into m_avatars (stable for session).
    int assignAvatarIndex(const std::string &nick);
    static std::string nickKey(const std::string &nick);
    SceneBody bodyFromAvatar(const LoadedAvatar &av) const;
    bool warmAvatarPoses(const LoadedAvatar &av) const;

    std::vector<LoadedAvatar> m_avatars;
    std::map<std::string, int> m_nickToAvatar;
    int m_nextAssign = 0;
    ComicImage m_backdrop;
    std::vector<ScenePanel> m_panels;
    std::string m_status;
    bool m_hasArt = false;

    int m_fontPoint = 12;
    double m_layoutPxPerTwip = 0.05;
};
