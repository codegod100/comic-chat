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

void ComicScene::setArt(std::vector<LoadedAvatar> avatars, const ComicImage &backdrop)
{
    m_avatars = std::move(avatars);
    m_backdrop = backdrop;
    m_hasArt = !backdrop.isNull() && !m_avatars.empty();
    m_nickToAvatar.clear();
    m_nextAssign = 0;
    m_status = m_hasArt ? ("Cast: " + std::to_string(m_avatars.size()) + " characters")
                        : "No art";
}

void ComicScene::setArt(const LoadedAvatar &avatar, const ComicImage &backdrop)
{
    std::vector<LoadedAvatar> one;
    one.push_back(avatar);
    setArt(std::move(one), backdrop);
}

void ComicScene::setBackdrop(const ComicImage &backdrop)
{
    m_backdrop = backdrop;
    m_hasArt = !m_backdrop.isNull() && !m_avatars.empty();
    if (m_hasArt) {
        m_status = "Cast: " + std::to_string(m_avatars.size()) + " characters";
    } else if (m_backdrop.isNull()) {
        m_status = "No backdrop";
    }
}

void ComicScene::clear()
{
    m_panels.clear();
    // Keep nick→avatar mapping so users keep the same character after clear.
    m_status = m_hasArt ? ("Cast: " + std::to_string(m_avatars.size()) + " characters")
                        : "Cleared";
}

void ComicScene::trimToMaxPanels(int maxPanels)
{
    if (maxPanels < 1) {
        m_panels.clear();
        return;
    }
    if (static_cast<int>(m_panels.size()) <= maxPanels) {
        return;
    }
    const size_t drop = m_panels.size() - static_cast<size_t>(maxPanels);
    m_panels.erase(m_panels.begin(), m_panels.begin() + static_cast<std::ptrdiff_t>(drop));
    m_status = "Panels: " + std::to_string(m_panels.size()) + " (showing latest)";
}

std::string ComicScene::nickKey(const std::string &nick)
{
    std::string k = nick.empty() ? "you" : nick;
    for (char &c : k) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return k;
}

bool ComicScene::warmAvatarPoses(const LoadedAvatar &av) const
{
    if (av.type == AT_COMPLEX) {
        if (av.faces.empty() || av.torsos.empty()) {
            return false;
        }
        return GetPoseFromID(av.faces.front().poseID) != nullptr &&
               GetPoseFromID(av.torsos.front().poseID) != nullptr;
    }
    USHORT id = 0;
    if (!av.bodies.empty()) {
        id = av.bodies.front().poseID;
    } else if (!av.bodyPoses.empty()) {
        id = av.bodyPoses.front();
    } else {
        id = av.iconPose;
    }
    return GetPoseFromID(id) != nullptr;
}

int ComicScene::assignAvatarIndex(const std::string &nick)
{
    if (m_avatars.empty()) {
        return -1;
    }
    const std::string key = nickKey(nick);
    auto it = m_nickToAvatar.find(key);
    if (it != m_nickToAvatar.end()) {
        return it->second;
    }

    // First-seen order: give each new nick the next cast member (wrap around).
    const int n = static_cast<int>(m_avatars.size());
    for (int tries = 0; tries < n; ++tries) {
        const int idx = (m_nextAssign + tries) % n;
        if (warmAvatarPoses(m_avatars[static_cast<size_t>(idx)])) {
            m_nextAssign = (idx + 1) % n;
            m_nickToAvatar[key] = idx;
            return idx;
        }
    }
    // Fallback: first entry even if warm fails
    m_nickToAvatar[key] = 0;
    m_nextAssign = n > 1 ? 1 : 0;
    return 0;
}

SceneBody ComicScene::bodyFromAvatar(const LoadedAvatar &av, const std::string &nick) const
{
    SceneBody body;
    body.type = av.type;
    body.flags = av.flags;
    body.avatarName = av.name;
    body.nick = nick.empty() ? "you" : nick;
    if (av.type == AT_COMPLEX) {
        if (!av.faces.empty()) {
            const FaceRec &f = av.faces.front();
            body.facePose = f.poseID;
            body.face_xCX = f.xCX;
            body.face_yCX = f.yCX;
            body.face_dx = f.delta_xCX;
            body.face_dy = f.delta_yCX;
        }
        if (!av.torsos.empty()) {
            const TorsoRec &t = av.torsos.front();
            body.torsoPose = t.poseID;
            body.torso_xCX = t.xCX;
            body.torso_yCX = t.yCX;
        }
    } else if (!av.bodies.empty()) {
        body.bodyPose = av.bodies.front().poseID;
    } else if (!av.bodyPoses.empty()) {
        body.bodyPose = av.bodyPoses.front();
    } else {
        body.bodyPose = av.iconPose;
    }
    return body;
}

SceneBody ComicScene::bodyFromRpgSprite(const ComicImage &sprite, const std::string &nick,
                                        const std::string &label) const
{
    SceneBody body;
    body.type = AT_CUSTOM;
    body.nick = nick.empty() ? "you" : nick;
    body.avatarName = label.empty() ? ("rpg:" + body.nick) : label;
    body.customSprite = sprite;
    return body;
}

void ComicScene::setRpgSpriteForNick(const std::string &nick, const ComicImage &sprite,
                                     const std::string &label, bool isSheet, int columns,
                                     int rows)
{
    const std::string key = nickKey(nick);
    if (sprite.isNull()) {
        m_nickRpgSprites.erase(key);
        return;
    }
    RpgSpriteOverride ov;
    ov.image = sprite;
    ov.label = label;
    ov.isSheet = isSheet;
    ov.columns = std::max(1, columns);
    ov.rows = std::max(1, rows);
    m_nickRpgSprites[key] = std::move(ov);
}

bool ComicScene::hasRpgSpriteForNick(const std::string &nick) const
{
    auto it = m_nickRpgSprites.find(nickKey(nick));
    return it != m_nickRpgSprites.end() && !it->second.image.isNull();
}

void ComicScene::refreshBodiesForNick(const std::string &nick)
{
    const std::string key = nickKey(nick);
    if (key.empty()) {
        return;
    }
    for (auto &panel : m_panels) {
        bool dirty = false;
        for (auto &body : panel.bodies) {
            if (nickKey(body.nick) == key) {
                // Re-resolve so late rpg.actor loads replace cast placeholders.
                body = bodyForNick(body.nick);
                dirty = true;
            }
        }
        if (dirty) {
            layoutPanel(panel);
        }
    }
}

std::vector<std::string> ComicScene::nicksOnStage() const
{
    std::vector<std::string> out;
    for (const auto &panel : m_panels) {
        for (const auto &body : panel.bodies) {
            if (body.nick.empty()) {
                continue;
            }
            const std::string k = nickKey(body.nick);
            bool seen = false;
            for (const auto &e : out) {
                if (nickKey(e) == k) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                out.push_back(body.nick);
            }
        }
    }
    return out;
}

SceneBody ComicScene::bodyForNick(const std::string &nick)
{
    const std::string who = nick.empty() ? "you" : nick;
    const std::string key = nickKey(who);

    auto rpg = m_nickRpgSprites.find(key);
    if (rpg != m_nickRpgSprites.end() && !rpg->second.image.isNull()) {
        const auto &ov = rpg->second;
        SceneBody body;
        body.type = AT_CUSTOM;
        body.nick = who;
        body.avatarName = ov.label.empty() ? who : ov.label;
        body.customSprite = ov.image;
        body.rpgIsSheet = ov.isSheet;
        body.rpgColumns = ov.columns;
        body.rpgRows = ov.rows;
        body.rpgFacing = RpgFacing::Down;
        return body;
    }

    if (m_avatars.empty()) {
        SceneBody empty;
        empty.type = 0;
        empty.nick = who;
        return empty;
    }
    const int avIdx = assignAvatarIndex(who);
    if (avIdx < 0) {
        SceneBody empty;
        empty.type = 0;
        empty.nick = who;
        return empty;
    }
    return bodyFromAvatar(m_avatars[static_cast<size_t>(avIdx)], who);
}

int ComicScene::findBodyIndex(const ScenePanel &panel, const std::string &nick)
{
    const std::string key = nickKey(nick);
    for (int i = 0; i < static_cast<int>(panel.bodies.size()); ++i) {
        if (nickKey(panel.bodies[static_cast<size_t>(i)].nick) == key) {
            return i;
        }
    }
    return -1;
}

bool ComicScene::shouldStartNewPanel(const std::string &nick) const
{
    if (m_panels.empty()) {
        return true;
    }
    const ScenePanel &last = m_panels.back();
    // Classic rule: speaker already on stage → new panel (their turn again).
    if (findBodyIndex(last, nick) >= 0) {
        return true;
    }
    if (static_cast<int>(last.balloons.size()) >= MAX_BALLOONS_PER_PANEL) {
        return true;
    }
    if (static_cast<int>(last.bodies.size()) >= MAX_BODIES_PER_PANEL) {
        return true;
    }
    return false;
}

std::string ComicScene::avatarNameForNick(const std::string &nick) const
{
    auto it = m_nickToAvatar.find(nickKey(nick));
    if (it == m_nickToAvatar.end() || it->second < 0 ||
        it->second >= static_cast<int>(m_avatars.size())) {
        return {};
    }
    return m_avatars[static_cast<size_t>(it->second)].name;
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

void ComicScene::layoutBalloon(SceneBalloon &b, const SceneBody &body, int balloonIndex,
                               int balloonCount)
{
    // Panel space: y=0 at top, y=-UNIT_PANEL_H at bottom (top > bottom).
    const int lineH = logicalLineHeight(m_fontPoint, m_layoutPxPerTwip);
    const int padX = std::max(180, lineH);
    const int padY = std::max(120, lineH * 2 / 3);
    constexpr int kTopMargin = 160;
    constexpr int kCloudExtra = 140;
    constexpr int kTailGap = 240;

    b.speakerArrowX = body.arrowX;
    b.speakerTop = body.box.top;

    // ── Image photo frame (freeq media / bare image URL) ────────────────
    // Panel Y: top > bottom (e.g. top=-160, bot=-3000). Height = top - bot.
    // Target ~80%×55% of panel (pre-bounds “large preview”), then fit to room.
    if (b.hasImage()) {
        const int iw = std::max(1, b.image.width());
        const int ih = std::max(1, b.image.height());
        constexpr int kImgStubGap = 100;
        constexpr int kFramePad = 60;
        constexpr int kSideMargin = 50;

        // Desired large size (solo / 2-speaker / crowded).
        const int wantImgW =
            UNIT_PANEL_W * (balloonCount > 2 ? 62 : (balloonCount > 1 ? 74 : 86)) / 100;
        const int wantImgH =
            UNIT_PANEL_H * (balloonCount > 1 ? 50 : 60) / 100;

        // Room above the character. topLimit is higher on screen (larger Y).
        const int cardTopLimit = -kTopMargin;                 // e.g. -160
        const int cardBotLimit = body.box.top + kImgStubGap;  // e.g. -2200
        // CRITICAL: height = top - bot (not bot - top) in this coordinate system.
        const int roomH = std::max(1200, cardTopLimit - cardBotLimit);
        const int roomW = UNIT_PANEL_W - 2 * kSideMargin;

        b.lines = wrapText(b.text, std::max(200, wantImgW));
        const int captionLines =
            (b.nick.empty() ? 0 : 1) + static_cast<int>(b.lines.size());
        const int captionH =
            captionLines > 0 ? captionLines * lineH + padY : padY / 2;
        const int chromeH = 2 * kFramePad + captionH;

        const int maxImgW = std::max(800, std::min(wantImgW, roomW - 2 * kFramePad));
        const int maxImgH = std::max(800, std::min(wantImgH, roomH - chromeH));

        const double scale = std::min(double(maxImgW) / iw, double(maxImgH) / ih);
        int imgW = std::max(1, int(std::lround(iw * scale)));
        int imgH = std::max(1, int(std::lround(ih * scale)));
        imgW = std::min(imgW, maxImgW);
        imgH = std::min(imgH, maxImgH);
        b.lines = wrapText(b.text, imgW);

        const int totalW = imgW + 2 * kFramePad;
        const int totalH = imgH + 2 * kFramePad + captionH;

        int cx = body.arrowX;
        if (balloonCount > 1) {
            const int spread = UNIT_PANEL_W * 8 / 100;
            cx += (balloonIndex - (balloonCount - 1) / 2) *
                  (spread / std::max(1, balloonCount - 1));
        }
        cx = std::max(totalW / 2 + kSideMargin,
                      std::min(UNIT_PANEL_W - totalW / 2 - kSideMargin, cx));

        // Hang from top of panel so photos stay large.
        int top = cardTopLimit;
        int bot = top - totalH; // totalH down from top → more negative
        if (bot < cardBotLimit) {
            // Would overlap character — pin bottom, re-check top.
            bot = cardBotLimit;
            top = bot + totalH;
            if (top > cardTopLimit) {
                // Still too tall: shrink image to remaining height (keep aspect).
                top = cardTopLimit;
                const int fitH = std::max(400, top - bot - chromeH);
                if (imgH > fitH) {
                    imgW = std::max(1, imgW * fitH / imgH);
                    imgH = fitH;
                }
                bot = top - (imgH + chromeH);
            }
        }

        b.cloudBox.left = cx - totalW / 2;
        b.cloudBox.right = cx + totalW / 2;
        // Recompute totalW if imgW shrank above.
        const int finalW = imgW + 2 * kFramePad;
        b.cloudBox.left = cx - finalW / 2;
        b.cloudBox.right = cx + finalW / 2;
        b.cloudBox.top = top;
        b.cloudBox.bottom = bot;

        b.imageBox.left = b.cloudBox.left + kFramePad;
        b.imageBox.right = b.cloudBox.right - kFramePad;
        b.imageBox.top = b.cloudBox.top - kFramePad;
        b.imageBox.bottom = b.imageBox.top - imgH;

        b.textBox.left = b.imageBox.left;
        b.textBox.right = b.imageBox.right;
        b.textBox.top = b.imageBox.bottom - padY / 3;
        b.textBox.bottom = b.cloudBox.bottom + kFramePad / 2;
        return;
    }

    // ── Text speech balloon ─────────────────────────────────────────────
    const int widthCapPct = balloonCount > 2 ? 42 : (balloonCount > 1 ? 48 : 55);
    const int maxTextW = UNIT_PANEL_W * widthCapPct / 100;
    b.lines = wrapText(b.text, maxTextW);

    int maxW = 0;
    for (const auto &ln : b.lines) {
        maxW = std::max(maxW, ln.width);
    }
    if (!b.nick.empty()) {
        maxW = std::max(maxW, measureLogical(b.nick + ":"));
    }
    maxW = std::max(maxW, measureLogical("MM"));

    const int nTextLines =
        std::max(1, static_cast<int>(b.lines.size())) + (b.nick.empty() ? 0 : 1);
    int boxW = maxW + 2 * padX;
    int boxH = nTextLines * lineH + 2 * padY;
    const int maxBoxW = UNIT_PANEL_W * (balloonCount > 1 ? 48 : 78) / 100;
    const int maxBoxH = UNIT_PANEL_H * (balloonCount > 2 ? 22 : 32) / 100;
    boxW = std::min(std::max(boxW, padX * 2 + 200), maxBoxW);
    boxH = std::min(std::max(boxH, lineH * 2 + padY), maxBoxH);

    int cx = body.arrowX;
    if (balloonCount > 1) {
        const int spread = UNIT_PANEL_W * 6 / 100;
        cx += (balloonIndex - (balloonCount - 1) / 2) * (spread / std::max(1, balloonCount - 1));
    }
    cx = std::max(boxW / 2 + 120, std::min(UNIT_PANEL_W - boxW / 2 - 120, cx));

    const int stackLift = balloonIndex * (boxH / 3 + lineH / 2);
    int bot = body.box.top + kTailGap + stackLift;
    int top = bot + boxH;

    if (top + kCloudExtra > -kTopMargin) {
        const int overshoot = (top + kCloudExtra) - (-kTopMargin);
        top -= overshoot;
        bot -= overshoot;
    }

    const int minBot = body.box.top + 80;
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
    b.imageBox = {};
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

void ComicScene::layoutOneBody(SceneBody &body, const RECT &client) const
{
    if (body.type == AT_COMPLEX) {
        CPose *head = GetPoseFromID(body.facePose);
        CPose *torso = GetPoseFromID(body.torsoPose);
        if (head && head->m_drawing && torso && torso->m_drawing) {
            computeComplexBodyBoxes(body, head, torso, client, body.box, body.headBox,
                                    body.torsoBox);
            body.arrowX = (body.headBox.left + body.headBox.right) / 2;
        } else {
            body.box = client;
            body.arrowX = (client.left + client.right) / 2;
        }
        return;
    }

    // AT_SIMPLE or AT_CUSTOM (rpg.actor sprite frame / walk sheet cell)
    int iw = 0;
    int ih = 0;
    if (body.type == AT_CUSTOM && !body.customSprite.isNull()) {
        if (body.rpgIsSheet && body.rpgColumns > 0 && body.rpgRows > 0) {
            // Layout uses one cell of the walk sheet, not the full grid.
            iw = std::max(1, body.customSprite.width() / body.rpgColumns);
            ih = std::max(1, body.customSprite.height() / body.rpgRows);
        } else {
            iw = body.customSprite.width();
            ih = body.customSprite.height();
        }
    } else {
        CPose *pose = GetPoseFromID(body.bodyPose);
        if (pose && pose->m_drawing && !pose->m_drawing->isNull()) {
            iw = pose->m_drawing->width();
            ih = pose->m_drawing->height();
        }
    }

    int bw = (client.right - client.left) * 2 / 3;
    int bh = client.top - client.bottom;
    if (ih > 0 && iw > 0) {
        bh = client.top - client.bottom;
        // rpg.actor idle frames are square-ish; keep aspect, don't overfill width.
        bw = std::max(200, iw * bh / ih);
        if (body.type == AT_CUSTOM) {
            // Pixel sprites read better a bit larger relative to slot.
            bw = std::max(bw, (client.right - client.left) * 55 / 100);
            bh = iw > 0 ? ih * bw / iw : bh;
            if (bh > client.top - client.bottom) {
                bh = client.top - client.bottom;
                bw = ih > 0 ? iw * bh / ih : bw;
            }
        }
        if (bw > client.right - client.left) {
            bw = client.right - client.left;
            bh = iw > 0 ? ih * bw / iw : bh;
        }
    }
    const int slotW = client.right - client.left;
    const int margin = client.left + (slotW - bw) / 2;
    body.box.left = margin;
    body.box.right = margin + bw;
    body.box.bottom = client.bottom;
    body.box.top = client.bottom + bh;
    body.arrowX = (body.box.left + body.box.right) / 2;
}

// Port of CBodyDouble::FlipBodyBox, keeping left < right for our draw path.
// Mirrors head/torso placement within the full body box (art faces the other way).
static void flipComplexBodyBoxes(RECT &fullRect, RECT &headRect, RECT &torsoRect)
{
    auto mirrorX = [&](RECT &r) {
        const int L = fullRect.left + fullRect.right - r.right;
        const int R = fullRect.left + fullRect.right - r.left;
        r.left = L;
        r.right = R;
    };
    mirrorX(headRect);
    mirrorX(torsoRect);
}

void ComicScene::applyBodyFlip(SceneBody &body) const
{
    if (!body.flip) {
        return;
    }
    // Balloon tail still points at the (mirrored) head center.
    if (body.type == AT_COMPLEX) {
        flipComplexBodyBoxes(body.box, body.headBox, body.torsoBox);
        body.arrowX = (body.headBox.left + body.headBox.right) / 2;
    } else {
        body.arrowX = body.box.left + body.box.right - body.arrowX;
    }
}

void ComicScene::assignFacing(ScenePanel &panel) const
{
    const int n = static_cast<int>(panel.bodies.size());
    if (n <= 0) {
        return;
    }
    if (n == 1) {
        // Solo: face camera (rpg down row); comic cast unflipped.
        panel.bodies[0].flip = false;
        panel.bodies[0].rpgFacing = RpgFacing::Down;
        return;
    }

    // Face toward the group's horizontal center so speakers look at each other.
    // Comic cast: flip=false faces right, flip=true mirrors left.
    // rpg.actor sheets: use Right/Left rows (no mirror — proper art).
    long sumCx = 0;
    for (const auto &b : panel.bodies) {
        sumCx += (b.box.left + b.box.right) / 2;
    }
    const int mid = static_cast<int>(sumCx / n);

    for (int i = 0; i < n; ++i) {
        SceneBody &b = panel.bodies[static_cast<size_t>(i)];
        const int cx = (b.box.left + b.box.right) / 2;
        bool faceLeft = false;
        if (cx < mid) {
            faceLeft = false; // face right (inward)
        } else if (cx > mid) {
            faceLeft = true; // face left (inward)
        } else {
            faceLeft = (i >= n / 2);
        }
        b.flip = faceLeft;
        b.rpgFacing = faceLeft ? RpgFacing::Left : RpgFacing::Right;
    }
}

void ComicScene::layoutBalloons(ScenePanel &panel)
{
    const int nBal = static_cast<int>(panel.balloons.size());
    for (int i = 0; i < nBal; ++i) {
        SceneBalloon &bal = panel.balloons[static_cast<size_t>(i)];
        int bi = findBodyIndex(panel, bal.nick);
        if (bi < 0 && !panel.bodies.empty()) {
            bi = 0; // fallback: first body
        }
        if (bi < 0) {
            continue;
        }
        layoutBalloon(bal, panel.bodies[static_cast<size_t>(bi)], i, nBal);
    }
}

void ComicScene::layoutPanel(ScenePanel &panel)
{
    const int n = static_cast<int>(panel.bodies.size());
    if (n <= 0) {
        return;
    }

    // Bodies stand in the lower ~half (original maxBodyHeight ≈ unitH/1.9).
    // With multiple characters, give each a horizontal slot with margins between.
    const int maxBodyH = UNIT_PANEL_H * 52 / 100;
    const int bottomY = -UNIT_PANEL_H + 100;
    const int topY = bottomY + maxBodyH;

    // Scale figures down a bit when crowded so they fit side-by-side.
    const double crowd = n <= 1 ? 1.0 : (n == 2 ? 0.92 : (n == 3 ? 0.85 : 0.78));
    const int edge = std::max(80, UNIT_PANEL_W / (12 + n));
    const int gap = std::max(40, UNIT_PANEL_W / (20 + n * 4));
    const int usable = UNIT_PANEL_W - 2 * edge - (n - 1) * gap;
    const int slotW = std::max(400, usable / n);

    for (int i = 0; i < n; ++i) {
        RECT client;
        client.left = edge + i * (slotW + gap);
        client.right = client.left + slotW;
        // Slight height shrink for crowded casts (same bottom, lower top).
        const int h = static_cast<int>(std::lround((topY - bottomY) * crowd));
        client.bottom = bottomY;
        client.top = bottomY + h;
        layoutOneBody(panel.bodies[static_cast<size_t>(i)], client);
    }

    // Who faces whom (m_flip in the original), then mirror boxes + art.
    assignFacing(panel);
    for (auto &body : panel.bodies) {
        applyBodyFlip(body);
    }

    layoutBalloons(panel);
}

void ComicScene::addLine(const std::string &text, UCHAR mode, const std::string &nick)
{
    if (text.empty()) {
        return;
    }
    addImageLine(ComicImage{}, text, mode, nick);
}

void ComicScene::addImageLine(const ComicImage &image, const std::string &caption, UCHAR mode,
                              const std::string &nick)
{
    if (image.isNull() && caption.empty()) {
        return;
    }
    if (m_avatars.empty() && m_nickRpgSprites.empty()) {
        return;
    }
    if (m_layoutPxPerTwip <= 0.0) {
        m_layoutPxPerTwip = 720.0 / UNIT_PANEL_W;
    }

    const std::string who = nick.empty() ? "you" : nick;
    SceneBody speaker = bodyForNick(who);
    if (speaker.type != AT_CUSTOM && speaker.type != AT_SIMPLE && speaker.type != AT_COMPLEX) {
        return;
    }

    SceneBalloon bal;
    bal.text = caption;
    bal.nick = who;
    bal.mode = mode;
    if (!image.isNull()) {
        bal.image = image;
    }

    // Photos always get their own panel — never merge into a multi-balloon
    // frame (that made one image appear "on" several chats visually).
    const bool forceNew = !image.isNull() || shouldStartNewPanel(who);
    if (forceNew || m_panels.empty()) {
        ScenePanel panel;
        panel.seed = static_cast<unsigned>(m_panels.size() + 1);
        panel.bodies.push_back(std::move(speaker));
        panel.balloons.push_back(std::move(bal));
        layoutPanel(panel);
        m_panels.push_back(std::move(panel));
    } else {
        ScenePanel &panel = m_panels.back();
        if (findBodyIndex(panel, who) < 0) {
            panel.bodies.push_back(std::move(speaker));
        }
        panel.balloons.push_back(std::move(bal));
        layoutPanel(panel);
    }

    const ScenePanel &last = m_panels.back();
    std::string label = who;
    const int bi = findBodyIndex(last, who);
    if (bi >= 0) {
        label = last.bodies[static_cast<size_t>(bi)].avatarName;
        if (label.empty()) {
            label = who;
        }
    }
    const bool rpg = hasRpgSpriteForNick(who);
    const bool img = !image.isNull();
    m_status = "Panels: " + std::to_string(m_panels.size()) + " | frame " +
               std::to_string(last.bodies.size()) + " chars / " +
               std::to_string(last.balloons.size()) + " lines | " + who + " → " + label +
               (rpg ? " [rpg.actor]" : "") + (img ? " [img]" : "") + " (" +
               std::to_string(m_nickToAvatar.size() + m_nickRpgSprites.size()) +
               " speakers, " + std::to_string(m_avatars.size()) + " cast)";
}

void ComicScene::addReplyExchange(const std::string &origNick, const std::string &origText,
                                  const std::string &replyNick, const std::string &replyText,
                                  UCHAR replyMode)
{
    if (replyText.empty() && origText.empty()) {
        return;
    }
    if (m_avatars.empty() && m_nickRpgSprites.empty()) {
        return;
    }
    if (m_layoutPxPerTwip <= 0.0) {
        m_layoutPxPerTwip = 720.0 / UNIT_PANEL_W;
    }

    const std::string whoReply = replyNick.empty() ? "you" : replyNick;
    const std::string whoOrig = origNick.empty() ? "?" : origNick;

    // Always a dedicated panel (freeq ReplyBadge context = new comic frame).
    ScenePanel panel;
    panel.seed = static_cast<unsigned>(m_panels.size() + 1);

    auto pushBody = [&](const std::string &nick) {
        if (findBodyIndex(panel, nick) >= 0) {
            return;
        }
        SceneBody b = bodyForNick(nick);
        if (b.type == AT_CUSTOM || b.type == AT_SIMPLE || b.type == AT_COMPLEX) {
            panel.bodies.push_back(std::move(b));
        }
    };

    // Original first (left), then reply — classic two-shot conversation.
    if (!origText.empty()) {
        pushBody(whoOrig);
    }
    pushBody(whoReply);

    if (panel.bodies.empty()) {
        return;
    }

    if (!origText.empty()) {
        SceneBalloon orig;
        // Parent is plain speech context; reply balloon is marked SM_REPLY.
        orig.text = origText;
        orig.nick = whoOrig;
        orig.mode = SM_SAY;
        panel.balloons.push_back(std::move(orig));
    }

    SceneBalloon rep;
    rep.text = replyText.empty() ? "(…)" : replyText;
    rep.nick = whoReply;
    (void)replyMode;
    rep.mode = SM_REPLY; // mark reply bubble (not the original)
    panel.balloons.push_back(std::move(rep));

    layoutPanel(panel);
    m_panels.push_back(std::move(panel));

    m_status = "Panels: " + std::to_string(m_panels.size()) + " | reply frame " + whoOrig +
               " → " + whoReply;
}

void ComicScene::setMsgIdForLastBalloon(const std::string &nick, const std::string &msgid)
{
    if (msgid.empty()) {
        return;
    }
    const std::string key = nickKey(nick);
    // Walk panels newest→oldest, find the most recent balloon owned by nick
    // (or matching nick case-insensitively) and stamp it.
    for (auto it = m_panels.rbegin(); it != m_panels.rend(); ++it) {
        for (auto &bal : it->balloons) {
            if (nickKey(bal.nick) == key) {
                bal.msgid = msgid;
                return;
            }
        }
    }
}

bool ComicScene::applyReact(const std::string &targetMsgid, const std::string &emoji,
                             const std::string &reactorNick, bool remove)
{
    if (targetMsgid.empty() || emoji.empty()) {
        return false;
    }
    const std::string who = nickKey(reactorNick);
    for (auto &panel : m_panels) {
        for (auto &bal : panel.balloons) {
            if (bal.msgid == targetMsgid) {
                auto &list = bal.reacts[emoji];
                if (remove) {
                    for (auto n = list.begin(); n != list.end(); ++n) {
                        if (nickKey(*n) == who) {
                            list.erase(n);
                            break;
                        }
                    }
                } else {
                    // Toggle: if this nick already reacted with this emoji, remove it.
                    bool found = false;
                    for (auto n = list.begin(); n != list.end(); ++n) {
                        if (nickKey(*n) == who) {
                            list.erase(n);
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        list.push_back(reactorNick.empty() ? "you" : reactorNick);
                    }
                }
                if (list.empty()) {
                    bal.reacts.erase(emoji);
                }
                return true;
            }
        }
    }
    return false;
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
        // drawMasked expects dest rect with y = bottom of image in our flipped space.
        // body.flip mirrors the sprite (classic Comic Chat facing).
        pose->drawMasked(canvas, r.left, r.bottom, w, h, body.flip);
    };

    if (body.type == AT_CUSTOM) {
        if (body.customSprite.isNull()) {
            return;
        }
        const int w = body.box.right - body.box.left;
        const int h = body.box.top - body.box.bottom;
        if (w <= 0 || h <= 0) {
            return;
        }
        ComicImage frame = body.customSprite;
        if (body.rpgIsSheet && body.rpgColumns > 0 && body.rpgRows > 0 &&
            !body.customSprite.isNull()) {
            // Pick walk-sheet cell: idle column, directional row (rpg.actor standard).
            const int cols = body.rpgColumns;
            const int rows = body.rpgRows;
            const int cellW = std::max(1, body.customSprite.width() / cols);
            const int cellH = std::max(1, body.customSprite.height() / rows);
            const int col = (cols >= 3) ? 1 : (cols / 2); // idle
            int row = static_cast<int>(body.rpgFacing);
            if (row < 0 || row >= rows) {
                row = 0;
            }
            const int x = col * cellW;
            const int y = row * cellH;
            if (x + cellW <= body.customSprite.width() &&
                y + cellH <= body.customSprite.height()) {
                frame.setQImage(body.customSprite.qimage().copy(x, y, cellW, cellH));
            }
            // Sheet directions are real art — do not mirror.
        } else if (body.flip && !frame.isNull()) {
            // Single-frame custom art: mirror like classic cast.
            frame.qimage() = frame.qimage().flipped(Qt::Horizontal);
        }
        frame.draw(canvas, body.box.left, body.box.bottom, w, h);
        return;
    }

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
    const int lineH = logicalLineHeight(m_fontPoint, m_layoutPxPerTwip);
    const UCHAR mode = b.mode;

    // Style from classic Comic Chat balloon kinds (+ SM_REPLY for freeq threads).
    CanvasColor fill = CanvasColor::rgb(255, 255, 255);
    CanvasColor stroke = CanvasColor::rgb(0, 0, 0);
    int strokeW = 36;
    bool thoughtTail = false;
    bool boxShape = false;
    bool jagged = false;
    if (mode == SM_WHISPER) {
        fill = CanvasColor::rgb(248, 248, 252);
        stroke = CanvasColor::rgb(90, 90, 110);
        strokeW = 28;
    } else if (mode == SM_THINK) {
        fill = CanvasColor::rgb(255, 255, 255);
        stroke = CanvasColor::rgb(40, 40, 50);
        strokeW = 32;
        thoughtTail = true;
    } else if (mode == SM_ACTION) {
        fill = CanvasColor::rgb(255, 252, 230);
        stroke = CanvasColor::rgb(30, 30, 30);
        strokeW = 40;
        boxShape = true;
    } else if (mode == SM_SHOUT) {
        fill = CanvasColor::rgb(255, 250, 240);
        stroke = CanvasColor::rgb(0, 0, 0);
        strokeW = 56;
        jagged = true;
    } else if (mode == SM_REPLY) {
        // Soft blue “reply” speech bubble — mark the reply, not the original.
        fill = CanvasColor::rgb(232, 242, 255);
        stroke = CanvasColor::rgb(40, 90, 170);
        strokeW = 40;
    }

    if (b.hasImage()) {
        // White photo card + border; trust layout boxes (already fitted).
        RECT frame{L, T, R, Btm};
        canvas->save();
        canvas->setClipRect(frame);
        canvas->setBrush(CanvasColor::rgb(255, 255, 255));
        canvas->setPen(CanvasColor::rgb(20, 20, 20), 40);
        canvas->fillRect(frame);
        canvas->drawRect(frame);

        const int diw = std::max(1, b.imageBox.right - b.imageBox.left);
        const int dih = std::max(1, b.imageBox.top - b.imageBox.bottom);
        const int imgLeft = b.imageBox.left;
        const int imgTop = b.imageBox.top;
        const int imgBottom = b.imageBox.bottom;

        if (!b.image.isNull() && diw > 0 && dih > 0) {
            RECT ir{imgLeft - 10, imgTop + 10, imgLeft + diw + 10, imgBottom - 10};
            ir.left = std::max(ir.left, L + 16);
            ir.right = std::min(ir.right, R - 16);
            ir.top = std::min(ir.top, T - 16);
            ir.bottom = std::max(ir.bottom, Btm + 16);
            canvas->setPen(CanvasColor::rgb(40, 40, 40), 20);
            canvas->setNoBrush();
            canvas->drawRect(ir);
            b.image.draw(canvas, imgLeft, imgBottom, diw, dih);
        }

        // Caption under the image, inside the card.
        canvas->setFont("Sans Serif", m_fontPoint, false);
        canvas->setPen(CanvasColor::rgb(0, 0, 0), 1);
        int y = imgBottom - lineH;
        const int yMin = Btm + lineH;
        if (!b.nick.empty()) {
            canvas->setFont("Sans Serif", std::max(8, m_fontPoint - 1), true);
            canvas->setPen(mode == SM_REPLY ? CanvasColor::rgb(30, 80, 160)
                                            : CanvasColor::rgb(40, 40, 120),
                           1);
            const std::string label =
                (mode == SM_REPLY ? std::string("\xE2\x86\xA9 ") : std::string()) + b.nick + ":";
            const int lw = measureLogical(label);
            if (y > yMin) {
                canvas->drawText((L + R - lw) / 2, y, label);
            }
            canvas->setFont("Sans Serif", m_fontPoint, false);
            canvas->setPen(CanvasColor::rgb(0, 0, 0), 1);
            y -= lineH;
        }
        for (const auto &ln : b.lines) {
            if (y <= yMin) {
                break;
            }
            const int x = (L + R - ln.width) / 2;
            canvas->drawText(x, y, ln.text);
            y -= lineH;
        }
        canvas->restore();

        // Short stub under the card.
        const int baseHalf = std::min(240, std::max(140, (R - L) / 9));
        const int tipY = std::max(b.speakerTop + 60, Btm - 180);
        canvas->setBrush(CanvasColor::rgb(255, 255, 255));
        canvas->setPen(CanvasColor::rgb(20, 20, 20), 36);
        canvas->beginPath();
        canvas->moveTo(mx - baseHalf, Btm);
        canvas->lineTo(mx, tipY);
        canvas->lineTo(mx + baseHalf, Btm);
        canvas->closePath();
        canvas->strokeAndFill();
        return;
    }

    canvas->setBrush(fill);
    canvas->setPen(stroke, strokeW);

    if (boxShape) {
        // Action / narrative box (classic CBWoodringBox).
        RECT box{L, T, R, Btm};
        canvas->fillRect(box);
        canvas->drawRect(box);
    } else if (jagged) {
        // Shout: simple star-ish / pointed outline.
        const int midY = my;
        const int midX = mx;
        const int inset = std::max(40, (R - L) / 12);
        canvas->beginPath();
        canvas->moveTo(L + inset, T);
        canvas->lineTo(midX, T + inset / 2);
        canvas->lineTo(R - inset, T);
        canvas->lineTo(R, midY);
        canvas->lineTo(R - inset, Btm);
        canvas->lineTo(midX, Btm - inset / 2);
        canvas->lineTo(L + inset, Btm);
        canvas->lineTo(L, midY);
        canvas->closePath();
        canvas->strokeAndFill();
        // Pointy tail
        canvas->beginPath();
        canvas->moveTo(mx - 120, Btm);
        canvas->lineTo(b.speakerArrowX, b.speakerTop + 120);
        canvas->lineTo(mx + 120, Btm);
        canvas->closePath();
        canvas->strokeAndFill();
    } else {
        // Cloud speech / think / whisper / reply
        const int dx = (R - L) / 4;
        POINT cps[8] = {
            {L, my}, {L + dx / 2, T}, {mx, T}, {R - dx / 2, T},
            {R, my}, {R - dx / 2, Btm}, {mx, Btm}, {L + dx / 2, Btm},
        };
        CCardinal spline(cps, 8, TRUE);
        canvas->beginPath();
        POINT lo = spline.SegLo();
        canvas->moveTo(lo.x, lo.y);
        spline.Draw(canvas);
        canvas->closePath();
        canvas->strokeAndFill();

        if (thoughtTail) {
            // Thought: two circles stepping toward the speaker.
            const int ax = b.speakerArrowX;
            const int ay = b.speakerTop + 80;
            const int x1 = (mx * 2 + ax) / 3;
            const int y1 = (Btm * 2 + ay) / 3;
            const int x2 = (mx + ax * 2) / 3;
            const int y2 = (Btm + ay * 2) / 3;
            const int r1 = 70;
            const int r2 = 45;
            canvas->setBrush(fill);
            canvas->setPen(stroke, strokeW * 3 / 4);
            canvas->fillEllipse(RECT{x1 - r1, y1 + r1, x1 + r1, y1 - r1});
            canvas->drawEllipse(RECT{x1 - r1, y1 + r1, x1 + r1, y1 - r1});
            canvas->fillEllipse(RECT{x2 - r2, y2 + r2, x2 + r2, y2 - r2});
            canvas->drawEllipse(RECT{x2 - r2, y2 + r2, x2 + r2, y2 - r2});
        } else {
            // Speech tail
            canvas->setBrush(fill);
            canvas->setPen(stroke, strokeW);
            canvas->beginPath();
            canvas->moveTo(mx - 100, Btm);
            canvas->lineTo(b.speakerArrowX, b.speakerTop + 120);
            canvas->lineTo(mx + 100, Btm);
            canvas->closePath();
            canvas->strokeAndFill();
        }
    }

    canvas->setFont("Sans Serif", m_fontPoint, false);
    canvas->setPen(CanvasColor::rgb(0, 0, 0), 1);
    int y = b.textBox.top - lineH * 85 / 100;
    if (!b.nick.empty()) {
        canvas->setFont("Sans Serif", std::max(8, m_fontPoint - 1), true);
        if (mode == SM_REPLY) {
            canvas->setPen(CanvasColor::rgb(30, 80, 160), 1);
        } else if (mode == SM_WHISPER) {
            canvas->setPen(CanvasColor::rgb(80, 80, 100), 1);
        } else {
            canvas->setPen(CanvasColor::rgb(40, 40, 120), 1);
        }
        // ↩ marks the *reply* speaker label (classic had no reply mode).
        const std::string label =
            (mode == SM_REPLY ? std::string("\xE2\x86\xA9 ") : std::string()) + b.nick + ":";
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

    // ── React badges (freeq emoji reacts) ───────────────────────────────
    if (!b.reacts.empty()) {
        const int pillH = std::max(lineH, 150);
        const int padX = 90;
        const int gap = 60;
        const int pillGap = 40;
        const int badgeTop = Btm - gap;        // just below the balloon
        const int badgeBot = badgeTop - pillH; // flipped Y: more negative

        canvas->setFont("Sans Serif", std::max(8, m_fontPoint - 1), false);
        int x = L;
        for (const auto &pr : b.reacts) {
            const std::string &emoji = pr.first;
            const int count = static_cast<int>(pr.second.size());
            if (count <= 0) {
                continue;
            }
            std::string label = emoji;
            if (count > 1) {
                label += " " + std::to_string(count);
            }
            const int tw = measureLogical(label);
            const int pillW = tw + 2 * padX;
            const int pillL = x;
            const int pillR = x + pillW;

            canvas->setBrush(CanvasColor::rgb(255, 250, 235));
            canvas->setPen(CanvasColor::rgb(120, 90, 40), 18);
            canvas->fillEllipse(
                RECT{pillL, badgeTop, pillR, badgeBot});
            canvas->drawEllipse(RECT{pillL, badgeTop, pillR, badgeBot});
            canvas->setPen(CanvasColor::rgb(60, 40, 10), 1);
            canvas->drawText(pillL + padX, (badgeTop + badgeBot) / 2 + lineH * 35 / 100,
                             label);
            x = pillR + pillGap;
        }
    }
}

void ComicScene::drawPanel(ICanvas *canvas, const ScenePanel &panel, const RECT &pixelRect) const
{
    const double sx = double(pixelRect.right - pixelRect.left) / UNIT_PANEL_W;
    const double sy = double(pixelRect.bottom - pixelRect.top) / UNIT_PANEL_H;

    canvas->save();
    canvas->setLogicalOrigin(pixelRect.left, pixelRect.top);
    canvas->setLogicalScale(sx, -sy);
    // Keep balloons/images from bleeding into neighboring panels in the strip.
    canvas->setClipRect(RECT{0, 0, UNIT_PANEL_W, -UNIT_PANEL_H});

    RECT full{0, 0, UNIT_PANEL_W, -UNIT_PANEL_H};
    canvas->setBrush(CanvasColor::rgb(245, 240, 230));
    canvas->fillRect(full);
    if (m_hasArt && !m_backdrop.isNull()) {
        m_backdrop.draw(canvas, 0, -UNIT_PANEL_H, UNIT_PANEL_W, UNIT_PANEL_H);
    }

    canvas->setPen(CanvasColor::rgb(20, 20, 20), 48);
    canvas->setNoBrush();
    canvas->drawRect(full);

    // Draw bodies first (behind balloons), left-to-right as laid out.
    for (const auto &body : panel.bodies) {
        drawBody(canvas, body);
    }
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
        // Preview the selected room so changing the combo is immediately visible.
        RECT empty{dest.left, y0, dest.left + panelW, y0 + panelH};
        drawPanel(canvas, ScenePanel{}, empty);

        canvas->save();
        canvas->setLogicalOrigin(0, 0);
        canvas->setLogicalScale(1.0, 1.0);
        const RECT band{empty.left, empty.bottom - 56, empty.right, empty.bottom};
        canvas->setBrush(CanvasColor::rgb(255, 255, 255, 220));
        canvas->setPen(CanvasColor::rgb(40, 40, 40), 1);
        canvas->fillRect(band);
        canvas->setFont("Sans Serif", 11, false);
        canvas->setPen(CanvasColor::rgb(30, 30, 30), 1);
        canvas->drawText(band.left + 12, band.top + 22,
                         "Room preview — type below to start chatting.");
        if (!m_status.empty()) {
            canvas->drawText(band.left + 12, band.top + 42, m_status);
        }
        canvas->restore();
        return;
    }

    int x = dest.left;
    for (const auto &p : m_panels) {
        RECT pr{x, y0, x + panelW, y0 + panelH};
        drawPanel(canvas, p, pr);
        x += panelW + kGap;
    }
}
