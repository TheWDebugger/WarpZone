#pragma once
#include "Rom.hpp"
#include "Nes.hpp"
#include <memory>
#include <set>

using namespace geode::prelude;

namespace smb {

// Runs the real ROM in the embedded NES core and shows the 256x240 picture in a GD scene.
class EmuLayer : public CCLayer {
public:
    static EmuLayer* create() {
        auto* l = new EmuLayer();
        if (l && l->init()) { l->autorelease(); return l; }
        CC_SAFE_DELETE(l);
        return nullptr;
    }

    static void open() {
        auto* scene = CCScene::create();
        scene->addChild(EmuLayer::create());
        CCDirector::get()->pushScene(CCTransitionFade::create(0.4f, scene));
    }

    bool init() override {
        if (!CCLayer::init()) return false;
        auto const& rom = *romStore(); // caller guarantees a validated ROM is loaded
        m_nes = std::make_unique<nes::Nes>(rom.prg, rom.chr, rom.verticalMirror);
        m_buf.assign(256 * 256, 0xFF000000u);

        auto win = CCDirector::get()->getWinSize();
        this->addChild(CCLayerColor::create({0, 0, 0, 255}), -10);

        m_screen = CCSprite::create();
        m_screen->setAnchorPoint({0.5f, 0.5f});
        m_screen->setPosition(win / 2);
        m_screen->setScale(win.height / 240.f);
        this->addChild(m_screen);
        upload();

        this->setKeyboardEnabled(true);
        this->scheduleUpdate();
        return true;
    }

    void keyDown(enumKeyCodes key, double) override {
        if (key == KEY_Escape) { CCDirector::get()->popScene(); return; }
        if (key == KEY_F5) { m_nes->reset(); return; }
        m_keys.insert(key);
    }
    void keyUp(enumKeyCodes key, double) override { m_keys.erase(key); }

    void update(float dt) override {
        constexpr float FRAME = 1.f / 60.0988f; // NTSC
        m_acc = std::min(m_acc + dt, 0.1f);     // don't spiral after a hitch
        bool ran = false;
        while (m_acc >= FRAME) {
            m_nes->setButtons(buttons());
            m_nes->runFrame();
            m_acc -= FRAME;
            ran = true;
        }
        if (ran) upload();
    }

private:
    std::unique_ptr<nes::Nes> m_nes;
    std::vector<uint32_t> m_buf;
    CCSprite* m_screen = nullptr;
    std::set<enumKeyCodes> m_keys;
    float m_acc = 0.f;

    bool held(std::initializer_list<enumKeyCodes> ks) const {
        for (auto k : ks) if (m_keys.count(k)) return true;
        return false;
    }

    uint8_t buttons() const {
        uint8_t b = 0;
        if (held({KEY_X, KEY_Space}))            b |= nes::BTN_A;
        if (held({KEY_Z, KEY_Shift}))            b |= nes::BTN_B;
        if (held({KEY_Tab, KEY_Backspace}))      b |= nes::BTN_SELECT;
        if (held({KEY_Enter}))                   b |= nes::BTN_START;
        if (held({KEY_Up, KEY_W}))               b |= nes::BTN_UP;
        if (held({KEY_Down, KEY_S}))             b |= nes::BTN_DOWN;
        if (held({KEY_Left, KEY_A}))             b |= nes::BTN_LEFT;
        if (held({KEY_Right, KEY_D}))            b |= nes::BTN_RIGHT;
        return b;
    }

    // Push the latest frame to the GPU. A fresh power-of-two texture each frame avoids raw GL
    // calls; the sprite releases the previous one when the new texture is set.
    void upload() {
        std::memcpy(m_buf.data(), m_nes->framebuffer(), 256 * 240 * sizeof(uint32_t));
        auto* tex = new CCTexture2D();
        tex->initWithData(m_buf.data(), kCCTexture2DPixelFormat_RGBA8888, 256, 256, CCSize(256, 256));
        tex->setAliasTexParameters(); // crisp pixels
        m_screen->setTexture(tex);
        m_screen->setTextureRect(CCRect(0, 0, 256, 240));
        tex->release();
    }
};

} // namespace smb
