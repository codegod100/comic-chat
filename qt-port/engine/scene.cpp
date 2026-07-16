// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "engine/scene.h"
#include "engine/spline.h"

#include <QFont>
#include <QFontMetrics>

#include <algorithm>
#include <cmath>
#include <sstream>

namespace {

QFont balloonFont(int point)
{
    QFont f(QStringLiteral("Sans Serif"));
    f.setPointSize(std::max(8, point));
    return f;
}

int logicalLineHeight(int fontPoint, double pxPerTwip)
{
    QFontMetrics fm(balloonFont(fontPoint));
    const int lineHpx = fm.height() + fm.leading();
    if (pxPerTwip <= 0.0) {
        return 220;
    }
    return std::max(80, static_cast<int>(std::lround(lineHpx / pxPerTwip)));
}

} // namespace

ComicScene::ComicScene() = default;

void ComicScene::setArt(const LoadedAvatar &avatar, const ComicImage &backdrop)
{
    m_avatar = avatar;
    m_backdrop = backdrop;
    m_hasArt = !backdrop.isNull();
    m_status = "Avatar: " + avatar.name;
}

void ComicScene::clear()
{
    m_panels.clear();
    m_status = "Cleared";
}

int ComicScene::measureLogical(const std::string &s) const
{
    QFontMetrics fm(balloonFont(m_fontPoint));
    const int px = fm.horizontalAdvance(QString::fromStdString(s));
    if (m_layoutPxPerTwip <= 0.0) {
        return px * 20;
    }
    return static_cast<int>(std::lround(px / m_layoutPxPerTwip));
}

std::vector<WrappedLine> ComicScene::wrapText(const std::string &text, int maxWidthLogical) const
{
    std::vector<WrappedLine> out;
    if (text.empty()) {
        return out;
    }

    std::istringstream iss(text);
    std::string word;
    std::string line;
    int lineW = 0;
    auto flush = [&]() {
        if (!line.empty()) {
            out.push_back({line, lineW});
            line.clear();
            lineW = 0;
        }
    };

    while (iss >> word) {
        const std::string candidate = line.empty() ? word : line + " " + word;
        const int w = measureLogical(candidate);
        if (w <= maxWidthLogical || line.empty()) {
            line = candidate;
            lineW = w;
        } else {
            flush();
            line = word;
            lineW = measureLogical(word);
        }
    }
    flush();
    return out;
}

void ComicScene::layoutBalloon(SceneBalloon &b, const SceneBody &body)
{
    // Panel space: y=0 at top, y=-UNIT_PANEL_H at bottom (top > bottom).
    // Size the text box from *current* font metrics (must match draw time).
    const int lineH = logicalLineHeight(m_fontPoint, m_layoutPxPerTwip);
    const int padX = std::max(180, lineH);
    const int padY = std::max(120, lineH * 2 / 3);

    // Available width for wrapping (leave padding inside cloud)
    const int maxTextW = UNIT_PANEL_W * 55 / 100;
    b.lines = wrapText(b.text, maxTextW);

    int maxW = 0;
    for (const auto &ln : b.lines) {
        maxW = std::max(maxW, ln.width);
    }
    if (!b.nick.empty()) {
        maxW = std::max(maxW, measureLogical(b.nick + ":"));
    }
    maxW = std::max(maxW, measureLogical("MM")); // minimum readable width

    const int nTextLines =
        std::max(1, static_cast<int>(b.lines.size())) + (b.nick.empty() ? 0 : 1);
    int boxW = maxW + 2 * padX;
    int boxH = nTextLines * lineH + 2 * padY;
    boxW = std::min(std::max(boxW, padX * 2 + 200), UNIT_PANEL_W * 78 / 100);
    boxH = std::min(std::max(boxH, lineH * 2 + padY), UNIT_PANEL_H * 32 / 100);

    int cx = body.arrowX;
    cx = std::max(boxW / 2 + 160, std::min(UNIT_PANEL_W - boxW / 2 - 160, cx));

    constexpr int kTopMargin = 200;
    constexpr int kCloudExtra = 140; // spline bulges past text box
    constexpr int kTailGap = 280;

    // Balloon bottom just above the head; top = bottom + height.
    int bot = body.box.top + kTailGap;
    int top = bot + boxH;

    if (top + kCloudExtra > -kTopMargin) {
        const int overshoot = (top + kCloudExtra) - (-kTopMargin);
        top -= overshoot;
        bot -= overshoot;
    }

    const int minBot = body.box.top + 100;
    if (bot < minBot) {
        bot = minBot;
        top = bot + boxH;
        if (top + kCloudExtra > -kTopMargin) {
            top = -kTopMargin - kCloudExtra;
            if (top <= bot + lineH) {
                top = bot + lineH * 2 + padY;
            }
        }
    }

    b.textBox.left = cx - boxW / 2;
    b.textBox.right = cx + boxW / 2;
    b.textBox.top = top;
    b.textBox.bottom = bot;

    b.cloudBox.left = b.textBox.left - padX / 2;
    b.cloudBox.right = b.textBox.right + padX / 2;
    b.cloudBox.top = b.textBox.top + kCloudExtra;
    b.cloudBox.bottom = b.textBox.bottom - padY / 2;
    if (b.cloudBox.top > -kTopMargin) {
        b.cloudBox.top = -kTopMargin;
    }

    b.speakerArrowX = body.arrowX;
    b.speakerTop = body.box.top;
}

// Port of CBodyDouble::GetBodyBox into panel TWIPS (y: 0 top, -H bottom).
// Bitmap coords: origin top-left, y down. We map into clientRect with same convention
// then flip into panel space.
static void computeComplexBodyBoxes(const SceneBody &body, CPose *head, CPose *torso,
                                    const RECT &clientRect, // top=0 bottom=-H style
                                    RECT &fullRect, RECT &headRect, RECT &torsoRect)
{
    const int hw = head->m_drawing->width();
    const int hh = head->m_drawing->height();
    const int tw = torso->m_drawing->width();
    const int th = torso->m_drawing->height();

    const int xOffset = body.torso_xCX + body.face_dx - body.face_xCX;
    const int yOffset = body.torso_yCX + body.face_dy - body.face_yCX;

    // bitRect in bitmap space (y down)
    const int bitL = std::min(0, xOffset);
    const int bitR = std::max(tw, xOffset + hw);
    const int bitT = std::min(0, yOffset);
    const int bitB = std::max(th, yOffset + hh);
    const int bitW = std::max(1, bitR - bitL);
    const int bitH = std::max(1, bitB - bitT);

    // client in panel space: top=0, bottom=-H
    const int clientW = clientRect.right - clientRect.left;
    const int clientH = clientRect.top - clientRect.bottom; // positive height
    // Fit in client, then shrink so the figure isn't panel-filling.
    // Original max was ~unitHeight/1.9; we keep ~38% of panel height.
    // Fit figure in the client region (original ~ unitHeight/1.9 of full panel).
    double scale = std::min(double(clientW) / bitW, double(clientH) / bitH);
    scale *= 0.92;
    const int fullW = std::max(1, int(std::lround(scale * bitW)));
    const int fullH = std::max(1, int(std::lround(scale * bitH)));

    // Center horizontally; stand on the bottom of the client region.
    fullRect.left = clientRect.left + (clientW - fullW) / 2;
    fullRect.bottom = clientRect.bottom + 20;
    fullRect.top = fullRect.bottom + fullH;
    fullRect.right = fullRect.left + fullW;

    // Head position within full box (bitmap y-down → panel y-up via fullRect.top - ...)
    // In original MM_TWIPS, top is less negative; they used:
    // headRect.top = ROUND((yOffset - bitRect.top) * scale) + fullRect.top
    // with heightSign. We use: bitmap dy from top of bitRect maps down from fullRect.top.
    const int headBmpX = xOffset - bitL;
    const int headBmpY = yOffset - bitT;
    const int torsoBmpX = 0 - bitL;
    const int torsoBmpY = 0 - bitT;

    const int headW = std::max(1, int(std::lround(hw * scale)));
    const int headH = std::max(1, int(std::lround(hh * scale)));
    const int torsoW = std::max(1, int(std::lround(tw * scale)));
    const int torsoH = std::max(1, int(std::lround(th * scale)));

    headRect.left = fullRect.left + int(std::lround(headBmpX * scale));
    headRect.right = headRect.left + headW;
    // bitmap y increases downward; panel y increases upward from bottom
    // top of full bitmap → fullRect.top; +bmpY goes toward bottom
    headRect.top = fullRect.top - int(std::lround(headBmpY * scale));
    headRect.bottom = headRect.top - headH;

    torsoRect.left = fullRect.left + int(std::lround(torsoBmpX * scale));
    torsoRect.right = torsoRect.left + torsoW;
    torsoRect.top = fullRect.top - int(std::lround(torsoBmpY * scale));
    torsoRect.bottom = torsoRect.top - torsoH;
}

void ComicScene::layoutPanel(ScenePanel &panel)
{
    // Body uses lower ~half of panel (original maxBodyHeight ≈ unitH/1.9).
    RECT client;
    client.left = UNIT_PANEL_W / 10;
    client.right = UNIT_PANEL_W - UNIT_PANEL_W / 10;
    client.top = -UNIT_PANEL_H * 48 / 100;
    client.bottom = -UNIT_PANEL_H + 100;

    if (panel.body.type == AT_COMPLEX) {
        CPose *head = GetPoseFromID(panel.body.facePose);
        CPose *torso = GetPoseFromID(panel.body.torsoPose);
        if (head && head->m_drawing && torso && torso->m_drawing) {
            computeComplexBodyBoxes(panel.body, head, torso, client, panel.body.box,
                                    panel.body.headBox, panel.body.torsoBox);
            panel.body.arrowX = (panel.body.headBox.left + panel.body.headBox.right) / 2;
        } else {
            panel.body.box = client;
            panel.body.arrowX = (client.left + client.right) / 2;
        }
    } else {
        CPose *pose = GetPoseFromID(panel.body.bodyPose);
        int bw = UNIT_PANEL_W / 3;
        int bh = UNIT_PANEL_H * 10 / 19;
        if (pose && pose->m_drawing && !pose->m_drawing->isNull()) {
            const int iw = pose->m_drawing->width();
            const int ih = pose->m_drawing->height();
            if (ih > 0) {
                bh = client.top - client.bottom;
                bw = std::max(200, iw * bh / ih);
                if (bw > client.right - client.left) {
                    bw = client.right - client.left;
                    bh = iw > 0 ? ih * bw / iw : bh;
                }
            }
        }
        const int margin = (UNIT_PANEL_W - bw) / 2;
        panel.body.box.left = margin;
        panel.body.box.right = margin + bw;
        panel.body.box.bottom = client.bottom;
        panel.body.box.top = client.bottom + bh;
        panel.body.arrowX = (panel.body.box.left + panel.body.box.right) / 2;
    }

    for (auto &bal : panel.balloons) {
        layoutBalloon(bal, panel.body);
    }
}

void ComicScene::addLine(const std::string &text, UCHAR mode, const std::string &nick)
{
    if (text.empty()) {
        return;
    }
    if (m_layoutPxPerTwip <= 0.0) {
        m_layoutPxPerTwip = 720.0 / UNIT_PANEL_W;
    }

    ScenePanel panel;
    panel.seed = static_cast<unsigned>(m_panels.size() + 1);
    panel.body.type = m_avatar.type;
    panel.body.flags = m_avatar.flags;
    if (m_avatar.type == AT_COMPLEX) {
        if (!m_avatar.faces.empty()) {
            const FaceRec &f = m_avatar.faces.front();
            panel.body.facePose = f.poseID;
            panel.body.face_xCX = f.xCX;
            panel.body.face_yCX = f.yCX;
            panel.body.face_dx = f.delta_xCX;
            panel.body.face_dy = f.delta_yCX;
        }
        if (!m_avatar.torsos.empty()) {
            const TorsoRec &t = m_avatar.torsos.front();
            panel.body.torsoPose = t.poseID;
            panel.body.torso_xCX = t.xCX;
            panel.body.torso_yCX = t.yCX;
        }
    } else if (!m_avatar.bodies.empty()) {
        panel.body.bodyPose = m_avatar.bodies.front().poseID;
    } else if (!m_avatar.bodyPoses.empty()) {
        panel.body.bodyPose = m_avatar.bodyPoses.front();
    } else {
        panel.body.bodyPose = m_avatar.iconPose;
    }

    SceneBalloon bal;
    bal.text = text;
    bal.nick = nick.empty() ? "you" : nick;
    bal.mode = mode;
    panel.balloons.push_back(std::move(bal));

    layoutPanel(panel);
    m_panels.push_back(std::move(panel));
    m_status = "Panels: " + std::to_string(m_panels.size()) + " | " + m_avatar.name;
}

void ComicScene::drawBody(ICanvas *canvas, const SceneBody &body) const
{
    auto drawPoseBox = [&](CPose *pose, const RECT &r) {
        if (!pose || !pose->m_drawing) {
            return;
        }
        const int w = r.right - r.left;
        const int h = r.top - r.bottom;
        if (w <= 0 || h <= 0) {
            return;
        }
        // drawMasked expects dest rect with y = bottom of image in our flipped space
        pose->drawMasked(canvas, r.left, r.bottom, w, h);
    };

    if (body.type == AT_COMPLEX) {
        CPose *torso = GetPoseFromID(body.torsoPose);
        CPose *face = GetPoseFromID(body.facePose);
        const bool torsoFirst = (body.flags & TORSOFIRST) != 0;
        if (torsoFirst) {
            drawPoseBox(torso, body.torsoBox);
            drawPoseBox(face, body.headBox);
        } else {
            drawPoseBox(face, body.headBox);
            drawPoseBox(torso, body.torsoBox);
        }
    } else {
        CPose *bodyPose = GetPoseFromID(body.bodyPose);
        drawPoseBox(bodyPose, body.box);
    }
}

void ComicScene::drawBalloon(ICanvas *canvas, const SceneBalloon &b) const
{
    const int L = b.cloudBox.left;
    const int R = b.cloudBox.right;
    const int T = b.cloudBox.top;
    const int Btm = b.cloudBox.bottom;
    const int mx = (L + R) / 2;
    const int my = (T + Btm) / 2;
    const int dx = (R - L) / 4;

    POINT cps[8] = {
        {L, my}, {L + dx / 2, T}, {mx, T}, {R - dx / 2, T},
        {R, my}, {R - dx / 2, Btm}, {mx, Btm}, {L + dx / 2, Btm},
    };
    CCardinal spline(cps, 8, TRUE);

    canvas->setBrush(CanvasColor::rgb(255, 255, 255));
    canvas->setPen(CanvasColor::rgb(0, 0, 0), 36);
    canvas->beginPath();
    POINT lo = spline.SegLo();
    canvas->moveTo(lo.x, lo.y);
    spline.Draw(canvas);
    canvas->closePath();
    canvas->strokeAndFill();

    // Tail
    canvas->beginPath();
    canvas->moveTo(mx - 100, Btm);
    canvas->lineTo(b.speakerArrowX, b.speakerTop + 120);
    canvas->lineTo(mx + 100, Btm);
    canvas->closePath();
    canvas->strokeAndFill();

    // Text: use the same point size / metrics that layoutBalloon used.
    // Baseline steps downward (more negative y) each line.
    canvas->setFont("Sans Serif", m_fontPoint, false);
    canvas->setPen(CanvasColor::rgb(0, 0, 0), 1);
    const int lineH = logicalLineHeight(m_fontPoint, m_layoutPxPerTwip);
    // First baseline inset from top of text box
    int y = b.textBox.top - lineH * 85 / 100;
    if (!b.nick.empty()) {
        canvas->setFont("Sans Serif", std::max(8, m_fontPoint - 1), true);
        canvas->setPen(CanvasColor::rgb(40, 40, 120), 1);
        const std::string label = b.nick + ":";
        const int lw = measureLogical(label);
        canvas->drawText((b.textBox.left + b.textBox.right - lw) / 2, y, label);
        canvas->setFont("Sans Serif", m_fontPoint, false);
        canvas->setPen(CanvasColor::rgb(0, 0, 0), 1);
        y -= lineH;
    }
    for (const auto &ln : b.lines) {
        const int x = (b.textBox.left + b.textBox.right - ln.width) / 2;
        canvas->drawText(x, y, ln.text);
        y -= lineH;
    }
}

void ComicScene::drawPanel(ICanvas *canvas, const ScenePanel &panel, const RECT &pixelRect) const
{
    const double sx = double(pixelRect.right - pixelRect.left) / UNIT_PANEL_W;
    const double sy = double(pixelRect.bottom - pixelRect.top) / UNIT_PANEL_H;

    canvas->save();
    canvas->setLogicalOrigin(pixelRect.left, pixelRect.top);
    canvas->setLogicalScale(sx, -sy);

    RECT full{0, 0, UNIT_PANEL_W, -UNIT_PANEL_H};
    canvas->setBrush(CanvasColor::rgb(245, 240, 230));
    canvas->fillRect(full);
    if (m_hasArt && !m_backdrop.isNull()) {
        m_backdrop.draw(canvas, 0, -UNIT_PANEL_H, UNIT_PANEL_W, UNIT_PANEL_H);
    }

    canvas->setPen(CanvasColor::rgb(20, 20, 20), 48);
    canvas->setNoBrush();
    canvas->drawRect(full);

    drawBody(canvas, panel.body);
    for (const auto &bal : panel.balloons) {
        drawBalloon(canvas, bal);
    }
    canvas->restore();
}

int ComicScene::panelSideForHeight(int contentHeight)
{
    // Square panels sized to the viewport height (side scroll strip).
    const int side = std::min(contentHeight, 560);
    return std::max(220, side);
}

int ComicScene::contentWidthForHeight(int contentHeight) const
{
    constexpr int kGap = 14;
    const int side = panelSideForHeight(contentHeight);
    if (m_panels.empty()) {
        return side;
    }
    const int n = static_cast<int>(m_panels.size());
    return n * side + (n - 1) * kGap;
}

int ComicScene::contentHeightForHeight(int contentHeight) const
{
    return panelSideForHeight(contentHeight);
}

void ComicScene::draw(ICanvas *canvas, const RECT &dest) const
{
    if (!canvas) {
        return;
    }

    constexpr int kGap = 14;
    const int contentH = std::max(1, dest.bottom - dest.top);
    const int side = panelSideForHeight(contentH);
    const int panelW = side;
    const int panelH = side;
    // Vertically center the strip in dest if dest is taller than the panel.
    const int y0 = dest.top + std::max(0, (contentH - side) / 2);

    auto *self = const_cast<ComicScene *>(this);
    self->m_layoutPxPerTwip = double(panelW) / UNIT_PANEL_W;
    self->m_fontPoint = std::max(9, std::min(14, panelH / 36));

    for (auto &p : self->m_panels) {
        self->layoutPanel(p);
    }

    if (m_panels.empty()) {
        RECT empty{dest.left, y0, dest.left + panelW, y0 + panelH};
        canvas->setBrush(CanvasColor::rgb(255, 255, 255));
        canvas->setPen(CanvasColor::rgb(40, 40, 40), 2);
        canvas->fillRect(empty);
        canvas->drawRect(empty);
        canvas->setFont("Sans Serif", 12, false);
        canvas->setPen(CanvasColor::rgb(80, 80, 80), 1);
        canvas->drawText(empty.left + 24, empty.top + 48,
                         "Type a line below — panels scroll sideways.");
        if (!m_status.empty()) {
            canvas->drawText(empty.left + 24, empty.top + 80, m_status);
        }
        return;
    }

    int x = dest.left;
    for (const auto &p : m_panels) {
        RECT pr{x, y0, x + panelW, y0 + panelH};
        drawPanel(canvas, p, pr);
        x += panelW + kGap;
    }
}
