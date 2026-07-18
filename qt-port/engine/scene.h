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
    // Optional inline media (freeq media-url / bare image URL).
    ComicImage image;
    RECT imageBox{}; // dest rect for photo inside/near balloon
    bool hasImage() const { return !image.isNull(); }

    // freeq message id this balloon represents (for react targeting).
    std::string msgid;
    // Emoji reacts grouped by emoji → reactor nicks (preserves add order).
    // A nick appearing twice with the same emoji = toggle remove (ATProto semantics).
    std::map<std::string, std::vector<std::string>> reacts;
};

// Custom body type for external sprites (e.g. rpg.actor idle frames).
#ifndef AT_CUSTOM
#define AT_CUSTOM 3
#endif

// rpg.actor walk-sheet row indices (dev-guide standard).
enum class RpgFacing : int {
    Down = 0,
    Left = 1,
    Right = 2,
    Up = 3,
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
    bool flip = false; // comic-cast: true = horizontal mirror (face left)
    std::string avatarName; // art character name (anna, dan, …) or rpg handle
    std::string nick;       // speaker nick that owns this body in the panel
    // AT_CUSTOM: prefer full walk sheet + grid so we can pick left/right rows.
    ComicImage customSprite;
    bool rpgIsSheet = false; // true → customSprite is full sheet, use rpgFacing row
    int rpgColumns = 3;
    int rpgRows = 4;
    RpgFacing rpgFacing = RpgFacing::Down;
};

struct ScenePanel {
    // Multiple characters can share one panel (classic Comic Chat).
    std::vector<SceneBody> bodies;
    std::vector<SceneBalloon> balloons;
    unsigned seed = 1;
};

// Soft caps matching original layout (max ~5 balloons; keep frames readable).
#ifndef MAX_BODIES_PER_PANEL
#define MAX_BODIES_PER_PANEL 4
#endif
#ifndef MAX_BALLOONS_PER_PANEL
#define MAX_BALLOONS_PER_PANEL 4
#endif

class ComicScene {
public:
    ComicScene();

    // Backdrop + cast of characters available for nick assignment.
    void setArt(std::vector<LoadedAvatar> avatars, const ComicImage &backdrop);
    // Convenience: single avatar cast.
    void setArt(const LoadedAvatar &avatar, const ComicImage &backdrop);
    // Swap room art without clearing panels or nick→avatar mapping.
    void setBackdrop(const ComicImage &backdrop);

    void clear();
    // Drop oldest panels so at most maxPanels remain (comic strip window).
    void trimToMaxPanels(int maxPanels);

    // Add a spoken line. Nick is mapped to a stable character from the cast.
    // If setRpgSpriteForNick() was called for this nick, that sprite is used.
    void addLine(const std::string &text, UCHAR mode = SM_SAY,
                 const std::string &nick = "you");

    // Spoken line with an inline image (chat photo / freeq media upload).
    // Caption may be empty; image must be non-null.
    void addImageLine(const ComicImage &image, const std::string &caption = {},
                      UCHAR mode = SM_SAY, const std::string &nick = "you");

    // freeq-style reply: always a new panel with original line + reply (two balloons).
    // origText may be empty if the parent msgid was not in the local cache.
    void addReplyExchange(const std::string &origNick, const std::string &origText,
                          const std::string &replyNick, const std::string &replyText,
                          UCHAR replyMode = SM_SAY);

    // Stamp the server-assigned msgid (echo-message) onto the most recent balloon
    // for nick, so later +react/+reply can target it.
    void setMsgIdForLastBalloon(const std::string &nick, const std::string &msgid);
    // Apply a freeq react to the balloon whose msgid == targetMsgid.
    // remove=false → toggle (ATProto semantics: re-react removes);
    // remove=true  → force removal. Returns true if the target balloon exists.
    bool applyReact(const std::string &targetMsgid, const std::string &emoji,
                    const std::string &reactorNick, bool remove = false);

    // Prefer this image for the nick (rpg.actor sprite). Empty image clears override.
    // Pass a full walk sheet (isSheet=true) with columns/rows for directional facing.
    void setRpgSpriteForNick(const std::string &nick, const ComicImage &sprite,
                             const std::string &label = {}, bool isSheet = false,
                             int columns = 3, int rows = 4);
    bool hasRpgSpriteForNick(const std::string &nick) const;
    // After a late rpg.actor load, rebuild on-stage bodies for this nick.
    void refreshBodiesForNick(const std::string &nick);
    // Nicks currently drawn in any panel (for deferred sprite upgrades).
    std::vector<std::string> nicksOnStage() const;

    int panelCount() const { return static_cast<int>(m_panels.size()); }
    int avatarCount() const { return static_cast<int>(m_avatars.size()); }
    int unitWidth() const { return UNIT_PANEL_W; }
    int unitHeight() const { return UNIT_PANEL_H; }

    // Access laid-out panels (for hit-testing, e.g. image lightbox).
    const std::vector<ScenePanel> &panels() const { return m_panels; }

    static int panelSideForHeight(int contentHeight);
    int contentWidthForHeight(int contentHeight) const;
    int contentHeightForHeight(int contentHeight) const;

    void draw(ICanvas *canvas, const RECT &dest) const;

    const std::string &status() const { return m_status; }

    // Which art character is assigned to this nick (empty if none yet).
    std::string avatarNameForNick(const std::string &nick) const;

private:
    void layoutPanel(ScenePanel &panel);
    void layoutOneBody(SceneBody &body, const RECT &client) const;
    void assignFacing(ScenePanel &panel) const;
    void applyBodyFlip(SceneBody &body) const;
    void layoutBalloon(SceneBalloon &b, const SceneBody &body, int balloonIndex,
                       int balloonCount);
    void layoutBalloons(ScenePanel &panel);
    std::vector<WrappedLine> wrapText(const std::string &text, int maxWidthLogical) const;
    int measureLogical(const std::string &s) const;
    void drawPanel(ICanvas *canvas, const ScenePanel &panel, const RECT &pixelRect) const;
    void drawBody(ICanvas *canvas, const SceneBody &body) const;
    void drawBalloon(ICanvas *canvas, const SceneBalloon &b) const;

    // Lowercase nick → index into m_avatars (stable for session).
    int assignAvatarIndex(const std::string &nick);
    static std::string nickKey(const std::string &nick);
    SceneBody bodyFromAvatar(const LoadedAvatar &av, const std::string &nick) const;
    SceneBody bodyFromRpgSprite(const ComicImage &sprite, const std::string &nick,
                                const std::string &label) const;
    SceneBody bodyForNick(const std::string &nick);
    bool warmAvatarPoses(const LoadedAvatar &av) const;
    static int findBodyIndex(const ScenePanel &panel, const std::string &nick);
    // True if this line should start a new panel rather than join the last one.
    bool shouldStartNewPanel(const std::string &nick) const;

    std::vector<LoadedAvatar> m_avatars;
    std::map<std::string, int> m_nickToAvatar;
    struct RpgSpriteOverride {
        ComicImage image;
        std::string label;
        bool isSheet = false;
        int columns = 3;
        int rows = 4;
    };
    // Nick key → rpg.actor (or other) custom sprite override.
    std::map<std::string, RpgSpriteOverride> m_nickRpgSprites;
    int m_nextAssign = 0;
    ComicImage m_backdrop;
    std::vector<ScenePanel> m_panels;
    std::string m_status;
    bool m_hasArt = false;

    int m_fontPoint = 12;
    double m_layoutPxPerTwip = 0.05;
};
