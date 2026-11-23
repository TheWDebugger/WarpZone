#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/utils/file.hpp>
#include "EmuLayer.hpp"

using namespace geode::prelude;

using PickTask = Task<Result<std::filesystem::path>>;

class $modify(SMBMenu, MenuLayer) {
    struct Fields {
        EventListener<PickTask> m_pick;
    };

    bool init() {
        if (!MenuLayer::init()) return false;

        auto* spr = ButtonSprite::create("SMB1");
        auto* btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(SMBMenu::onSMB));
        btn->setID("smb1-button"_spr);

        if (auto* menu = this->getChildByID("bottom-menu")) {
            menu->addChild(btn);
            menu->updateLayout();
        }
        return true;
    }

    void onSMB(CCObject*) {
        // 1. Already loaded this session?
        if (smb::romStore()) return smb::EmuLayer::open();

        // 2. Try the ROM path remembered from last time.
        auto saved = Mod::get()->getSavedValue<std::string>("rom-path", "");
        if (!saved.empty()) {
            if (tryLoad(std::filesystem::path(saved), false)) return smb::EmuLayer::open();
        }

        // 3. Ask for a ROM. The game cannot start without one.
        FLAlertLayer::create(
            "ROM required",
            "This mod contains no Nintendo assets.\n"
            "Select your own dumped <cy>Super Mario Bros. (.nes)</c> file to continue.",
            "Choose ROM")->show();

        file::FilePickOptions opts;
        opts.filters.push_back({"NES ROM", {"*.nes"}});
        m_fields->m_pick.bind(this, &SMBMenu::onPicked);
        m_fields->m_pick.setFilter(file::pick(file::PickMode::OpenFile, opts));
    }

    void onPicked(PickTask::Event* ev) {
        auto* res = ev->getValue();
        if (!res) return;                 // still waiting
        if (!res->isOk()) return;         // user cancelled
        if (tryLoad(res->unwrap(), true)) smb::EmuLayer::open();
    }

    bool tryLoad(std::filesystem::path const& path, bool showErrors) {
        bool strict = !Mod::get()->getSettingValue<bool>("skip-crc-check");
        auto rom = smb::Rom::load(path, strict);
        if (!rom) {
            if (showErrors)
                FLAlertLayer::create("Invalid ROM", rom.unwrapErr().c_str(), "OK")->show();
            return false;
        }
        smb::romStore() = std::move(rom.unwrap());
        Mod::get()->setSavedValue<std::string>("rom-path", geode::utils::string::pathToString(path));
        return true;
    }
};
