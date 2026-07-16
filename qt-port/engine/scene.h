// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// Phase 3: multi-line comic page. TWIPS layout (unit panel), drawn via ICanvas.
// Not a full port of LayoutBalloon yet — enough for real AddLine → panels.

#pragma once

#include "engine/avatario.h"
#include "engine/chat_constants.h"
#include "engine/image.h"
#include "engine/pose.h"
#include "platform/ICanvas.h"
#include "platform/types.h"

#include <string>
#include <vector>

// Default unit panel size (historical MINUNITPANELWIDTH-ish / common 4860).
#ifndef UNIT_PANEL_W
#define UNIT_PANEL_W 4860
#define UNIT_PANEL_H 4860
#endif

struct WrappedLine {
    std::string text;
    int width = 0; // logical units
};

struct SceneBalloon {
    std::string text;
    std::string nick;
    UCHAR mode = SM_SAY;
    std::vector<WrappedLine> lines;
    RECT textBox{};   // panel-local TWIPS (y-down-negative: top=0, bottom=-H)
    RECT cloudBox{};  // inflated for spline
    int speakerArrowX = 0;
    int speakerTop = 0; // y of head top (more positive than feet)
};

struct SceneBody {
    USHORT facePose = 0;
    USHORT torsoPose = 0;
    USHORT bodyPose = 0;
    int type = AT_COMPLEX;
    UCHAR flags = 0;
    // Placement from .avb (complex only)
    short face_xCX = 0, face_yCX = 0, face_dx = 0, face_dy = 0;
    short torso_xCX = 0, torso_yCX = 0;
    RECT box{}; // full body bbox in panel TWIPS
    RECT headBox{};
    RECT torsoBox{};
    int arrowX = 0;
};

struct ScenePanel {
    SceneBody body;
    std::vector<SceneBalloon> balloons;
    unsigned seed = 1;
};

// One scrollable comic page driven by AddLine.
class ComicScene {
public:
    ComicScene();

    void setArt(const LoadedAvatar &avatar, const ComicImage &backdrop);
    void clear();

    // Add a spoken line. Creates a new panel. nick is shown in the balloon label.
    void addLine(const std::string &text, UCHAR mode = SM_SAY,
                 const std::string &nick = "you");

    int panelCount() const { return static_cast<int>(m_panels.size()); }
    int unitWidth() const { return UNIT_PANEL_W; }
    int unitHeight() const { return UNIT_PANEL_H; }

    // Pixel size of one square panel given available content width (margins excluded).
    static int panelSideForWidth(int contentWidth);
    // Total stacked height for N panels at that width (including gaps).
    int contentHeightForWidth(int contentWidth) const;

    // Draw all panels stacked vertically into dest (device pixels).
    // Each panel is square (unit W/H are equal); do not squash to fit.
    // Caller should size dest tall enough (scroll area) or panels clip.
    void draw(ICanvas *canvas, const RECT &dest) const;

    const std::string &status() const { return m_status; }

private:
    void layoutPanel(ScenePanel &panel);
    void layoutBalloon(SceneBalloon &b, const SceneBody &body);
    std::vector<WrappedLine> wrapText(const std::string &text, int maxWidthLogical) const;
    int measureLogical(const std::string &s) const;
    void drawPanel(ICanvas *canvas, const ScenePanel &panel, const RECT &pixelRect) const;
    void drawBody(ICanvas *canvas, const SceneBody &body) const;
    void drawBalloon(ICanvas *canvas, const SceneBalloon &b) const;

    LoadedAvatar m_avatar;
    ComicImage m_backdrop;
    std::vector<ScenePanel> m_panels;
    std::string m_status;
    bool m_hasArt = false;

    // Font metrics in device px at layout time (point size used for QFont).
    int m_fontPoint = 12;
    double m_layoutPxPerTwip = 0.05; // set during draw from dest size
};
