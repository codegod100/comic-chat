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
    const int maxTextW = UNIT_PANEL_W * 65 / 100;
    b.lines = wrapText(b.text, maxTextW);

    const int lineH = logicalLineHeight(m_fontPoint, m_layoutPxPerTwip);
    int maxW = 0;
    for (const auto &ln : b.lines) {
        maxW = std::max(maxW, ln.width);
    }
    maxW = std::max(maxW, 400);

    const int textH = std::max(1, static_cast<int>(b.lines.size())) * lineH;
    const int padX = 280;
    const int padY = 180;
    const int boxW = maxW + 2 * padX;
    const int boxH = textH + 2 * padY;

    int cx = body.arrowX;
    cx = std::max(boxW / 2 + 200, std::min(UNIT_PANEL_W - boxW / 2 - 200, cx));

    // Panel coords: y=0 top, y=-UNIT_PANEL_H bottom
    b.textBox.left = cx - boxW / 2;
    b.textBox.right = cx + boxW / 2;
    b.textBox.top = -280;
    b.textBox.bottom = b.textBox.top - boxH;

    b.cloudBox = b.textBox;
    b.cloudBox.left -= 100;
    b.cloudBox.right += 100;
    b.cloudBox.top += 40;
    b.cloudBox.bottom -= 80;

    b.speakerArrowX = body.arrowX;
    b.speakerTop = body.box.top;
}

void ComicScene::layoutPanel(ScenePanel &panel)
{
    const int maxBodyH = UNIT_PANEL_H * 10 / 19;
    int bw = UNIT_PANEL_W / 3;
    int bh = maxBodyH;

    CPose *pose = nullptr;
    if (panel.body.type == AT_COMPLEX) {
        pose = GetPoseFromID(panel.body.torsoPose);
    } else {
        pose = GetPoseFromID(panel.body.bodyPose);
    }
    if (pose && pose->m_drawing && !pose->m_drawing->isNull()) {
        const int iw = pose->m_drawing->width();
        const int ih = pose->m_drawing->height();
        if (ih > 0) {
            bh = maxBodyH;
            bw = std::max(200, iw * bh / ih);
            if (bw > UNIT_PANEL_W * 4 / 5) {
                bw = UNIT_PANEL_W * 4 / 5;
                bh = iw > 0 ? ih * bw / iw : bh;
            }
        }
    }

    const int margin = (UNIT_PANEL_W - bw) / 2;
    panel.body.box.left = margin;
    panel.body.box.right = margin + bw;
    panel.body.box.bottom = -UNIT_PANEL_H + 40;
    panel.body.box.top = panel.body.box.bottom + bh;
    panel.body.arrowX = (panel.body.box.left + panel.body.box.right) / 2;

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
    if (m_avatar.type == AT_COMPLEX) {
        if (!m_avatar.facePoses.empty()) {
            panel.body.facePose = m_avatar.facePoses.front();
        }
        if (!m_avatar.torsoPoses.empty()) {
            panel.body.torsoPose = m_avatar.torsoPoses.front();
        }
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
    const int dw = body.box.right - body.box.left;
    const int dh = body.box.top - body.box.bottom;
    if (dw <= 0 || dh <= 0) {
        return;
    }

    if (body.type == AT_COMPLEX) {
        CPose *torso = GetPoseFromID(body.torsoPose);
        CPose *face = GetPoseFromID(body.facePose);
        if (torso && torso->m_drawing) {
            torso->drawMasked(canvas, body.box.left, body.box.bottom, dw, dh);
        }
        if (face && face->m_drawing && torso && torso->m_drawing) {
            const int fw = face->m_drawing->width();
            const int fh = face->m_drawing->height();
            const int tw = std::max(1, torso->m_drawing->width());
            const int th = std::max(1, torso->m_drawing->height());
            const double sx = double(dw) / tw;
            const double sy = double(dh) / th;
            const int fdw = int(fw * sx);
            const int fdh = int(fh * sy);
            const int fx = body.box.left + (dw - fdw) / 2;
            const int fy = body.box.top - fdh / 3;
            face->drawMasked(canvas, fx, fy, fdw, fdh);
        }
    } else {
        CPose *bodyPose = GetPoseFromID(body.bodyPose);
        if (bodyPose) {
            bodyPose->drawMasked(canvas, body.box.left, body.box.bottom, dw, dh);
        }
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

    // Text lines (y decreases downward in panel space)
    canvas->setFont("Sans Serif", m_fontPoint, false);
    canvas->setPen(CanvasColor::rgb(0, 0, 0), 1);
    const int lineH = logicalLineHeight(m_fontPoint, m_layoutPxPerTwip);
    int y = b.textBox.top - lineH; // first baseline
    if (!b.nick.empty()) {
        canvas->setFont("Sans Serif", std::max(8, m_fontPoint - 2), true);
        canvas->setPen(CanvasColor::rgb(40, 40, 120), 1);
        const std::string label = b.nick + ":";
        const int lw = measureLogical(label);
        canvas->drawText((b.textBox.left + b.textBox.right - lw) / 2, y + lineH / 3, label);
        canvas->setFont("Sans Serif", m_fontPoint, false);
        canvas->setPen(CanvasColor::rgb(0, 0, 0), 1);
        y -= lineH * 2 / 3;
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

void ComicScene::draw(ICanvas *canvas, const RECT &dest) const
{
    if (!canvas) {
        return;
    }

    const int n = std::max(1, static_cast<int>(m_panels.size()));
    const int gap = 10;
    const int totalGap = gap * (std::max(0, n - 1));
    const int availH = std::max(100, dest.bottom - dest.top - totalGap);
    const int panelH = m_panels.empty() ? availH : std::max(120, availH / n);
    const int panelW = dest.right - dest.left;

    const_cast<ComicScene *>(this)->m_layoutPxPerTwip = double(panelW) / UNIT_PANEL_W;
    const_cast<ComicScene *>(this)->m_fontPoint = std::max(9, std::min(18, panelH / 26));

    if (m_panels.empty()) {
        canvas->setBrush(CanvasColor::rgb(255, 255, 255));
        canvas->setPen(CanvasColor::rgb(40, 40, 40), 2);
        canvas->fillRect(dest);
        canvas->drawRect(dest);
        canvas->setFont("Sans Serif", 12, false);
        canvas->setPen(CanvasColor::rgb(80, 80, 80), 1);
        canvas->drawText(dest.left + 24, dest.top + 48,
                         "Type a line below and press Enter to add a comic panel.");
        if (!m_status.empty()) {
            canvas->drawText(dest.left + 24, dest.top + 80, m_status);
        }
        return;
    }

    int y = dest.top;
    for (const auto &p : m_panels) {
        RECT pr{dest.left, y, dest.left + panelW, y + panelH};
        drawPanel(canvas, p, pr);
        y += panelH + gap;
    }
}
