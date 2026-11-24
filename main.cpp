#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/async.hpp>
#include "EmuLayer.hpp"
#include "SelfTestSetting.hpp"

using namespace geode::prelude;

// Validates the ROM at `path`; on success stores it and remembers the path.
static bool tryLoad(std::string const& path, bool showErrors) {
    bool strict = !Mod::get()->getSettingValue<bool>("skip-crc-check");
    auto res = smb::loadRom(path, strict);
    if (!res.rom) {
        if (showErrors) FLAlertLayer::create("Invalid ROM", res.error, "OK")->show();
        return false;
    }
    smb::romStore() = std::move(res.rom);
    Mod::get()->setSavedValue<std::string>("rom-path", path);
    return true;
}

class $modify(WarpMenu, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        auto* spr = ButtonSprite::create("Warp Zone");
        auto* btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(WarpMenu::onWarp));
        btn->setID("warp-zone-button"_spr);

        if (auto* menu = this->getChildByID("bottom-menu")) {
            spr->setScale(0.7f);
            menu->addChild(btn);
            menu->updateLayout();
        } else {
            // Texture-pack / other-mod layouts without a bottom-menu: put the button in its own menu.
            auto* fallback = CCMenu::create();
            fallback->setID("warp-zone-menu"_spr);
            auto win = CCDirector::get()->getWinSize();
            fallback->setPosition({win.width / 2, 30.f});
            fallback->addChild(btn);
            this->addChild(fallback);
        }
        return true;
    }

    void onWarp(CCObject*) {
        // 1. Already loaded this session?
        if (smb::romStore()) return smb::EmuLayer::open();

        // 2. Try the ROM path remembered from last time.
        auto saved = Mod::get()->getSavedValue<std::string>("rom-path", "");
        if (!saved.empty() && tryLoad(saved, false)) return smb::EmuLayer::open();

        // 3. Ask for a ROM. The game cannot start without one.
        FLAlertLayer::create(
            "ROM required",
            "This mod contains no Nintendo assets.\n"
            "Select your own dumped <cy>Super Mario Bros. (.nes)</c> file to continue.",
            "Choose ROM")->show();

        geode::async::spawn(
            geode::utils::file::pick(
                geode::utils::file::PickMode::OpenFile,
                geode::utils::file::FilePickOptions{
                    .filters = {geode::utils::file::FilePickOptions::Filter{
                        .description = "NES ROM",
                        .files = {"*.nes"},
                    }},
                }),
            [](geode::utils::file::PickResult result) {
                if (result.isErr()) {
                    FLAlertLayer::create("Unable to select file", fmt::format("{}", result.unwrapErr()), "OK")->show();
                    return;
                }
                auto picked = std::move(result).unwrap();
                if (!picked.has_value()) return; // user cancelled
                if (tryLoad(picked->string(), true)) smb::EmuLayer::open();
            });
    }
};
