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
    RECT box{}; // placed bbox in panel TWIPS
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

    // Add a spoken line (local nick "you" for now). Creates/extends panels.
    void addLine(const std::string &text, UCHAR mode = SM_SAY);

    int panelCount() const { return static_cast<int>(m_panels.size()); }
    int unitWidth() const { return UNIT_PANEL_W; }
    int unitHeight() const { return UNIT_PANEL_H; }

    // Draw all panels stacked vertically into dest (device pixels).
    // dest is the full comic area; panels share width and stack downward.
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
