#pragma once
#include "Nes.hpp"
#include <map>
#include <set>

using namespace geode::prelude;

namespace smb {

// ---------------------------------------------------------------------------
// Tile indices into the ROM's CHR data (0-255 per pattern table).
// !! These are UNVERIFIED best guesses. Press F1 in-game to open the CHR viewer,
// !! find the real tiles, and edit the numbers here. Everything else is data-driven.
// Order for each metatile: TL, TR, BL, BR.
// ---------------------------------------------------------------------------
namespace tiles {
    // background pattern table (table 1)
    constexpr std::array<uint8_t, 4> GROUND   = {0xB4, 0xB5, 0xB6, 0xB7};
    constexpr std::array<uint8_t, 4> BRICK    = {0xEA, 0xEB, 0xEC, 0xED};
    constexpr std::array<uint8_t, 4> QUESTION = {0x53, 0x55, 0x54, 0x56};
    constexpr std::array<uint8_t, 4> USED     = {0x47, 0x47, 0x47, 0x47};
    constexpr std::array<uint8_t, 4> PIPE     = {0x60, 0x61, 0x62, 0x63};
    // sprite pattern table (table 0)
    constexpr std::array<uint8_t, 4> MARIO    = {0x3A, 0x37, 0x4F, 0x4F};
    constexpr std::array<uint8_t, 4> GOOMBA   = {0x70, 0x71, 0x72, 0x73};
}

// Palettes (NES colour indices). Mario's are the well-known 0x22/0x16/0x27/0x18 set.
namespace pals {
    constexpr Pal SKY      = {0x22, 0x29, 0x1A, 0x0F};
    constexpr Pal GROUND   = {0x22, 0x27, 0x17, 0x0F};
    constexpr Pal QUESTION = {0x22, 0x37, 0x17, 0x0F};
    constexpr Pal PIPE     = {0x22, 0x29, 0x1A, 0x0F};
    constexpr Pal MARIO    = {0x22, 0x16, 0x27, 0x18};
    constexpr Pal GOOMBA   = {0x22, 0x16, 0x37, 0x18};
    constexpr Pal VIEWER   = {0x0F, 0x00, 0x10, 0x30};
}

// Original SMB1 physics constants, converted to pixels/frame at 60 Hz.
namespace phys {
    constexpr float WALK_MAX = 1.5625f, RUN_MAX = 2.5625f;
    constexpr float WALK_ACC = 0.0375f, RUN_ACC = 0.0562f;
    constexpr float DECEL = 0.0507f, SKID = 0.1f;
    constexpr float JUMP_V = -4.0f;
    constexpr float GRAV_HELD = 0.125f, GRAV_FALL = 0.4375f, TERMINAL = 4.0f;
}

struct Body {
    float x = 0, y = 0;    // x = center, y = feet; NES-style pixels, y grows downward
    float vx = 0, vy = 0;
    float hw = 6, h = 16;  // half-width, height
    bool ground = false;
};

struct Enemy {
    Body b;
    int dir = -1;
    bool alive = true;
    CCNode* node = nullptr;
};

class SMBLayer : public CCLayer {
public:
    static constexpr int W = 120, H = 15; // level size in 16px tiles (NES screen = 16 x 15)
    static constexpr float SCREEN_W = 256, SCREEN_H = 240;

    static SMBLayer* create() {
        auto* l = new SMBLayer();
        if (l && l->init()) { l->autorelease(); return l; }
        CC_SAFE_DELETE(l);
        return nullptr;
    }

    static void open() {
        auto* scene = CCScene::create();
        scene->addChild(SMBLayer::create());
        CCDirector::get()->pushScene(CCTransitionFade::create(0.4f, scene));
    }

    ~SMBLayer() {
        for (auto* t : m_textures) t->release();
    }

    bool init() override {
        if (!CCLayer::init()) return false;
        auto const& rom = *romStore(); // caller guarantees ROM is loaded

        auto win = CCDirector::get()->getWinSize();
        m_scale = win.height / SCREEN_H;

        auto* bg = CCLayerColor::create({0x5C, 0x94, 0xFC, 255}); // SMB sky
        this->addChild(bg, -10);

        m_world = CCNode::create();
        m_world->setScale(m_scale);
        this->addChild(m_world);

        // Build one texture per (table, palette) combination.
        m_texGround   = track(makeSheet(rom, 1, pals::GROUND,   false));
        m_texQuestion = track(makeSheet(rom, 1, pals::QUESTION, false));
        m_texPipe     = track(makeSheet(rom, 1, pals::PIPE,     false));
        m_texMario    = track(makeSheet(rom, 0, pals::MARIO,    true));
        m_texGoomba   = track(makeSheet(rom, 0, pals::GOOMBA,   true));
        m_texViewer0  = track(makeSheet(rom, 0, pals::VIEWER,   false));
        m_texViewer1  = track(makeSheet(rom, 1, pals::VIEWER,   false));

        m_hud = CCLabelBMFont::create("COINS 0", "bigFont.fnt");
        m_hud->setScale(0.45f);
        m_hud->setAnchorPoint({0, 1});
        m_hud->setPosition({10, win.height - 8});
        this->addChild(m_hud, 20);

        m_msg = CCLabelBMFont::create("", "bigFont.fnt");
        m_msg->setScale(0.8f);
        m_msg->setPosition(win / 2);
        this->addChild(m_msg, 20);

        buildViewer(win);
        buildLevel();
        resetRun();

        this->setKeyboardEnabled(true);
        this->scheduleUpdate();
        return true;
    }

    // ---- input ------------------------------------------------------------
    void keyDown(enumKeyCodes key, double) override {
        if (key == KEY_Escape) { CCDirector::get()->popScene(); return; }
        if (key == KEY_F1) { toggleViewer(); return; }
        if (m_viewerOn) { viewerKey(key); return; }
        if (key == KEY_Space || key == KEY_Up || key == KEY_Z) m_jumpPressed = true;
        m_keys.insert(key);
    }
    void keyUp(enumKeyCodes key, double) override { m_keys.erase(key); }

    // ---- main loop (fixed 60 Hz) -----------------------------------------
    void update(float dt) override {
        if (m_viewerOn) return;
        m_acc += dt;
        while (m_acc >= 1.f / 60.f) {
            m_acc -= 1.f / 60.f;
            step();
            m_jumpPressed = false;
        }
        render();
    }

private:
    // level / state
    std::vector<std::string> m_map;
    std::map<int, CCNode*> m_tileNodes;
    std::vector<Enemy> m_enemies;
    Body m_mario;
    int m_flagX = 0;
    float m_scroll = 0, m_acc = 0, m_scale = 1;
    int m_coins = 0, m_deadTimer = 0;
    bool m_won = false, m_dead = false, m_faceLeft = false, m_jumpPressed = false;
    std::set<enumKeyCodes> m_keys;

    // nodes / textures
    CCNode* m_world = nullptr;
    CCNode* m_marioNode = nullptr;
    CCLabelBMFont *m_hud = nullptr, *m_msg = nullptr;
    std::vector<CCTexture2D*> m_textures;
    CCTexture2D *m_texGround, *m_texQuestion, *m_texPipe, *m_texMario, *m_texGoomba;
    CCTexture2D *m_texViewer0, *m_texViewer1;

    // CHR viewer
    CCNode* m_viewer = nullptr;
    CCLayerColor* m_cursor = nullptr;
    CCLabelBMFont* m_viewerLabel = nullptr;
    bool m_viewerOn = false;
    int m_vcx = 0, m_vcy = 0;

    CCTexture2D* track(CCTexture2D* t) { m_textures.push_back(t); return t; }

    // ---- level ------------------------------------------------------------
    // An original layout in the spirit of World 1-1 (not extracted from the ROM).
    // Legend: # ground  B brick  ? question  u used  P pipe  G goomba spawn
    void buildLevel() {
        m_map.assign(H, std::string(W, ' '));
        auto set = [&](int x, int y, char c) { if (x >= 0 && x < W && y >= 0 && y < H) m_map[y][x] = c; };
        for (int x = 0; x < W; x++) {
            bool gap = (x >= 69 && x <= 70) || (x >= 86 && x <= 88);
            if (!gap) { set(x, 13, '#'); set(x, 14, '#'); }
        }
        set(16, 9, '?'); set(20, 9, 'B'); set(21, 9, '?'); set(22, 9, 'B');
        set(23, 9, '?'); set(24, 9, 'B'); set(22, 5, '?');
        set(78, 9, 'B'); set(79, 9, '?'); set(80, 9, 'B');
        int pipes[][2] = {{28, 2}, {38, 3}, {46, 4}, {57, 4}};
        for (auto& p : pipes)
            for (int dx = 0; dx < 2; dx++)
                for (int h = 0; h < p[1]; h++) set(p[0] + dx, 12 - h, 'P');
        m_flagX = (W - 8) * 16;

        // Static tile sprites
        for (int ty = 0; ty < H; ty++)
            for (int tx = 0; tx < W; tx++) refreshTile(tx, ty);
    }

    void refreshTile(int tx, int ty) {
        int key = ty * W + tx;
        if (auto it = m_tileNodes.find(key); it != m_tileNodes.end()) {
            it->second->removeFromParent();
            m_tileNodes.erase(it);
        }
        CCNode* n = nullptr;
        switch (m_map[ty][tx]) {
            case '#': n = makeMeta(m_texGround,   tiles::GROUND);   break;
            case 'B': n = makeMeta(m_texGround,   tiles::BRICK);    break;
            case '?': n = makeMeta(m_texQuestion, tiles::QUESTION); break;
            case 'u': n = makeMeta(m_texQuestion, tiles::USED);     break;
            case 'P': n = makeMeta(m_texPipe,     tiles::PIPE);     break;
            default: return;
        }
        n->setPosition({tx * 16.f + 8, SCREEN_H - (ty + 1) * 16.f});
        m_world->addChild(n, 1);
        m_tileNodes[key] = n;
    }

    bool solid(int tx, int ty) const {
        if (tx < 0) return true;           // left wall
        if (tx >= W || ty < 0 || ty >= H) return false;
        char c = m_map[ty][tx];
        return c == '#' || c == 'B' || c == '?' || c == 'u' || c == 'P';
    }

    void resetRun() {
        for (auto& e : m_enemies) if (e.node) e.node->removeFromParent();
        m_enemies.clear();
        // Reset question blocks
        for (int ty = 0; ty < H; ty++)
            for (int tx = 0; tx < W; tx++)
                if (m_map[ty][tx] == 'u') { m_map[ty][tx] = '?'; refreshTile(tx, ty); }

        int gx[] = {22, 40, 51, 53, 80, 97};
        for (int g : gx) {
            Enemy e;
            e.b = Body{g * 16.f + 8, 13 * 16.f, 0, 0, 8, 16};
            e.node = makeMeta(m_texGoomba, tiles::GOOMBA);
            m_world->addChild(e.node, 2);
            m_enemies.push_back(e);
        }
        if (m_marioNode) m_marioNode->removeFromParent();
        m_marioNode = makeMeta(m_texMario, tiles::MARIO);
        m_world->addChild(m_marioNode, 3);

        m_mario = Body{40, 13 * 16.f, 0, 0, 6, 16};
        m_scroll = 0; m_coins = 0; m_deadTimer = 0;
        m_won = m_dead = false;
        m_msg->setString("");
        m_hud->setString("COINS 0");
    }

    // ---- collision --------------------------------------------------------
    bool moveX(Body& b) {
        b.x += b.vx;
        if (b.vx == 0) return false;
        int t0 = (int)std::floor((b.y - b.h) / 16.f), t1 = (int)std::floor((b.y - 0.01f) / 16.f);
        int tx = b.vx > 0 ? (int)std::floor((b.x + b.hw - 0.01f) / 16.f)
                          : (int)std::floor((b.x - b.hw) / 16.f);
        for (int ty = t0; ty <= t1; ty++) {
            if (solid(tx, ty)) {
                b.x = b.vx > 0 ? tx * 16.f - b.hw : (tx + 1) * 16.f + b.hw;
                return true;
            }
        }
        return false;
    }

    // Returns 1 if landed, 2 if head-bumped (bumpX/bumpY set), 0 otherwise.
    int moveY(Body& b, int* bumpX = nullptr, int* bumpY = nullptr) {
        b.y += b.vy;
        b.ground = false;
        int l = (int)std::floor((b.x - b.hw) / 16.f), r = (int)std::floor((b.x + b.hw - 0.01f) / 16.f);
        if (b.vy >= 0) {
            int ty = (int)std::floor(b.y / 16.f);
            for (int tx = l; tx <= r; tx++) {
                if (solid(tx, ty)) { b.y = ty * 16.f; b.vy = 0; b.ground = true; return 1; }
            }
        } else {
            int ty = (int)std::floor((b.y - b.h) / 16.f);
            for (int tx = l; tx <= r; tx++) {
                if (solid(tx, ty)) {
                    b.y = (ty + 1) * 16.f + b.h; b.vy = 0;
                    if (bumpX) *bumpX = tx;
                    if (bumpY) *bumpY = ty;
                    return 2;
                }
            }
        }
        return 0;
    }

    // ---- simulation -------------------------------------------------------
    bool held(std::initializer_list<enumKeyCodes> ks) const {
        for (auto k : ks) if (m_keys.count(k)) return true;
        return false;
    }

    void step() {
        if (m_dead) {
            if (--m_deadTimer <= 0) resetRun();
            return;
        }
        if (m_won) return;

        auto& m = m_mario;
        int dir = (held({KEY_Right, KEY_D}) ? 1 : 0) - (held({KEY_Left, KEY_A}) ? 1 : 0);
        bool run = held({KEY_X, KEY_Shift});
        bool jumpHeld = held({KEY_Space, KEY_Up, KEY_Z});

        // horizontal
        float maxV = run ? phys::RUN_MAX : phys::WALK_MAX;
        float acc = run ? phys::RUN_ACC : phys::WALK_ACC;
        if (dir != 0) {
            m_faceLeft = dir < 0;
            if (m.vx * dir < 0) m.vx += dir * phys::SKID;
            else m.vx = std::clamp(m.vx + dir * acc, -maxV, maxV);
        } else if (m.vx > 0) m.vx = std::max(0.f, m.vx - phys::DECEL);
        else if (m.vx < 0) m.vx = std::min(0.f, m.vx + phys::DECEL);

        // jump (faster = higher, like the original)
        if (m_jumpPressed && m.ground) m.vy = phys::JUMP_V - std::abs(m.vx) * 0.1f;
        float g = (m.vy < 0 && jumpHeld) ? phys::GRAV_HELD : phys::GRAV_FALL;
        m.vy = std::min(m.vy + g, phys::TERMINAL);

        if (moveX(m)) m.vx = 0;
        m.x = std::max(m.x, m_scroll + m.hw); // can't walk back off-screen
        int bx = -1, by = -1;
        if (moveY(m, &bx, &by) == 2) bumpBlock(bx, by);

        // camera: scroll right only
        m_scroll = std::max(m_scroll, std::min(m.x - SCREEN_W / 2, (float)(W * 16) - SCREEN_W));

        // enemies
        for (auto& e : m_enemies) {
            if (!e.alive || e.b.x > m_scroll + SCREEN_W + 32) continue;
            e.b.vx = e.dir * 0.5f;
            if (moveX(e.b)) e.dir = -e.dir;
            e.b.vy = std::min(e.b.vy + phys::GRAV_FALL, phys::TERMINAL);
            moveY(e.b);
            if (e.b.y > SCREEN_H + 32) { e.alive = false; continue; }

            bool overlap = std::abs(m.x - e.b.x) < (m.hw + e.b.hw) &&
                           m.y > e.b.y - e.b.h && m.y - m.h < e.b.y;
            if (!overlap) continue;
            if (m.vy > 0 && m.y - e.b.y < -4) { e.alive = false; m.vy = -3.f; }
            else die();
        }

        if (m.y > SCREEN_H + 32) die();
        if (m.x >= m_flagX) { m_won = true; m_msg->setString("COURSE CLEAR!"); }
    }

    void bumpBlock(int tx, int ty) {
        if (ty < 0 || ty >= H || tx < 0 || tx >= W) return;
        if (m_map[ty][tx] == '?') {
            m_map[ty][tx] = 'u';
            refreshTile(tx, ty);
            m_coins++;
            m_hud->setString(fmt::format("COINS {}", m_coins).c_str());
        }
    }

    void die() {
        m_dead = true;
        m_deadTimer = 90;
        m_msg->setString("TRY AGAIN");
    }

    // ---- rendering --------------------------------------------------------
    void render() {
        m_world->setPositionX(-m_scroll * m_scale);
        m_marioNode->setPosition({m_mario.x, SCREEN_H - m_mario.y});
        m_marioNode->setScaleX(m_faceLeft ? -1.f : 1.f);
        for (auto& e : m_enemies) {
            e.node->setVisible(e.alive);
            e.node->setPosition({e.b.x, SCREEN_H - e.b.y});
        }
    }

    // ---- CHR viewer (F1) --------------------------------------------------
    void buildViewer(CCSize win) {
        m_viewer = CCNode::create();
        m_viewer->setVisible(false);
        this->addChild(m_viewer, 100);
        m_viewer->addChild(CCLayerColor::create({0, 0, 0, 255}));

        float s = std::min(win.width / 2.2f, win.height * 0.8f) / 128.f;
        m_vscale = s;
        float y0 = (win.height - 128 * s) / 2 - 8;
        float x0 = (win.width - 256 * s) / 2;
        for (int t = 0; t < 2; t++) {
            auto* spr = CCSprite::createWithTexture(t ? m_texViewer1 : m_texViewer0);
            spr->setAnchorPoint({0, 0});
            spr->setScale(s);
            spr->setPosition({x0 + t * 128 * s, y0});
            m_viewer->addChild(spr);
        }
        m_vx0 = x0; m_vy0 = y0;
        m_cursor = CCLayerColor::create({255, 0, 0, 120}, 8 * s, 8 * s);
        m_viewer->addChild(m_cursor, 2);
        m_viewerLabel = CCLabelBMFont::create("", "bigFont.fnt");
        m_viewerLabel->setScale(0.5f);
        m_viewerLabel->setPosition({win.width / 2, win.height - 14});
        m_viewer->addChild(m_viewerLabel, 3);
        moveCursor(0, 0);
    }
    float m_vscale = 1, m_vx0 = 0, m_vy0 = 0;

    void toggleViewer() {
        m_viewerOn = !m_viewerOn;
        m_viewer->setVisible(m_viewerOn);
    }

    void viewerKey(enumKeyCodes k) {
        if (k == KEY_Left) moveCursor(-1, 0);
        else if (k == KEY_Right) moveCursor(1, 0);
        else if (k == KEY_Up) moveCursor(0, -1);
        else if (k == KEY_Down) moveCursor(0, 1);
    }

    void moveCursor(int dx, int dy) {
        m_vcx = (m_vcx + dx + 32) % 32;
        m_vcy = (m_vcy + dy + 16) % 16;
        float s = m_vscale * 8;
        m_cursor->setPosition({m_vx0 + m_vcx * s, m_vy0 + (15 - m_vcy) * s});
        int table = m_vcx / 16, idx = m_vcy * 16 + (m_vcx % 16);
        m_viewerLabel->setString(
            fmt::format("{} TILE ${:02X}  (ARROWS MOVE, F1 CLOSES)", table ? "BG" : "SPRITE", idx).c_str());
    }
};

} // namespace smb
