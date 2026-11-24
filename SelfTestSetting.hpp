#pragma once
// A "Run emulator test" button inside the mod's settings menu (mod.json type "custom:test-emulator").
#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <atomic>
#include <thread>
#include "EmuSelfTest.hpp"
#include "Rom.hpp"

using namespace geode::prelude;

namespace smb {

// Finds a validated ROM: the one already loaded this session, else the remembered path.
inline std::optional<Rom> findRomForTest(std::string& error) {
    if (romStore()) return *romStore();
    auto saved = Mod::get()->getSavedValue<std::string>("rom-path", "");
    if (saved.empty()) {
        error = "No ROM selected yet.\nClick <cy>Warp Zone</c> on the main menu once and choose your SMB1 .nes file, then run this test again.";
        return std::nullopt;
    }
    bool strict = !Mod::get()->getSettingValue<bool>("skip-crc-check");
    auto res = Rom::load(saved, strict);
    if (!res) { error = fmt::format("Saved ROM failed validation:\n{}", res.unwrapErr()); return std::nullopt; }
    return std::move(res).unwrap();
}

inline void startEmulatorTest() {
    static std::atomic<bool> running = false;
    if (running.exchange(true)) return;

    std::string err;
    auto rom = findRomForTest(err);
    if (!rom) {
        running = false;
        FLAlertLayer::create("Emulator test", err, "OK")->show();
        return;
    }
    Notification::create("Running emulator test...", NotificationIcon::Loading, 2.f)->show();

    // ~600 frames of emulation; run it off the main thread so the game doesn't freeze.
    std::thread([rom = std::move(*rom)] {
        auto result = runEmulatorSelfTest(rom.prg, rom.chr, rom.verticalMirror);
        queueInMainThread([result = std::move(result)] {
            std::string body;
            for (auto const& l : result.lines)
                body += fmt::format("{} {}\n", l.ok ? "<cg>PASS</c>" : "<cr>FAIL</c>", l.text);
            FLAlertLayer::create(
                result.passed ? "Emulator test: PASSED" : "Emulator test: FAILED",
                body, "OK")->show();
            running = false;
        });
    }).detach();
}

class SelfTestSetting : public SettingV3 {
public:
    static Result<std::shared_ptr<SettingV3>> parse(std::string const& key, std::string const& modID, matjson::Value const& json) {
        auto res = std::make_shared<SelfTestSetting>();
        auto root = checkJson(json, "SelfTestSetting");
        res->init(key, modID, root);
        res->parseNameAndDescription(root);
        res->parseEnableIf(root);
        root.checkUnknownKeys();
        return root.ok(std::static_pointer_cast<SettingV3>(res));
    }
    // A button has no value to store.
    bool load(matjson::Value const&) override { return true; }
    bool save(matjson::Value&) const override { return true; }
    bool isDefaultValue() const override { return true; }
    void reset() override {}
    SettingNodeV3* createNode(float width) override;
};

class SelfTestSettingNode : public SettingNodeV3 {
protected:
    bool init(std::shared_ptr<SelfTestSetting> setting, float width) {
        if (!SettingNodeV3::init(setting, width)) return false;
        auto* spr = ButtonSprite::create("Run Test", "goldFont.fnt", "GJ_button_01.png", .8f);
        spr->setScale(.55f);
        auto* btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(SelfTestSettingNode::onRun));
        this->getButtonMenu()->addChildAtPosition(btn, Anchor::Center);
        this->getButtonMenu()->setContentWidth(70.f);
        this->getButtonMenu()->updateLayout();
        this->updateState(nullptr);
        return true;
    }
    void onRun(CCObject*) { startEmulatorTest(); }

    void onCommit() override {}
    void onResetToDefault() override {}
public:
    bool hasUncommittedChanges() const override { return false; }
    bool hasNonDefaultValue() const override { return false; }

    static SelfTestSettingNode* create(std::shared_ptr<SelfTestSetting> setting, float width) {
        auto* ret = new SelfTestSettingNode();
        if (ret->init(setting, width)) { ret->autorelease(); return ret; }
        delete ret;
        return nullptr;
    }
};

inline SettingNodeV3* SelfTestSetting::createNode(float width) {
    return SelfTestSettingNode::create(std::static_pointer_cast<SelfTestSetting>(shared_from_this()), width);
}

} // namespace smb

$on_mod(Loaded) {
    (void)Mod::get()->registerCustomSettingType("test-emulator", &smb::SelfTestSetting::parse);
}
