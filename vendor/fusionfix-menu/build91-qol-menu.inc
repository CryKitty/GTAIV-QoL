// SPDX-License-Identifier: GPL-3.0-only
// CryKitty QoL menu bridge. Applied to the pinned FusionFix 5.1.2 source.
// Uses FusionFix's own registration functions; no competing game-menu hooks.
    void AddQolBundleControls()
    {
        auto module = GetModuleHandleA("GTAIV-QoL.asi");
        auto getApi = module ? reinterpret_cast<QolGetSettingsApi>(GetProcAddress(module, "QoLSettings_GetApi")) : nullptr;
        const auto api = getApi ? getApi(1) : nullptr;
        if (!api || api->version != 1 || api->size < sizeof(QolSettingsApi) || !api->Get || !api->Set)
            return;
        const char* keys[] = { "QOL_ROADNAV", "QOL_AUTOZOOM", "QOL_CSPAUSE", "QOL_VOICES", "QOL_GAMEVOICE", "QOL_CUTVOICE", "QOL_COMPASS" };
        const wchar_t* labels[] = { L"RoadNav", L"Map Autozoom", L"Pause During Cutscenes", L"Cutscene Voice Volume", L"Gameplay Voices (%)", L"Cutscene Voices (%)", L"Compass HUD" };
        const auto qolScreen = AddScreen("QoL", MenuScreen::Audio);
        uint32_t rows = 0;
        for (uint32_t id = 0; id < QOL_SETTING_COUNT; ++id)
        {
            if (api->Get(id) < 0) continue; // Additive settings on older core versions.
            if (id == QOL_GAMEPLAY_VOICE) continue; // Reserved ABI slot; gameplay volume stays untouched.
            CText::menuTexts[GetHash(keys[id])] = labels[id];
            if (id < QOL_GAMEPLAY_VOICE || id == QOL_COMPASS)
            {
                if (AddToggle(qolScreen, keys[id], [api, id] { return api->Get(id) != 0; },
                    [api, id](bool enabled) { api->Set(id, enabled ? 1 : 0); }))
                    rows |= 1u << id;
            }
            else
            {
                bool added = AddSlider(MenuScreen::Audio, keys[id], 0.0f, 400.0f, 5.0f,
                    [api, id] { return static_cast<float>(api->Get(id)); },
                    [api, id](float value) { api->Set(id, static_cast<int32_t>(value)); });
                AddSlider(MenuScreen::TitleAudio, keys[id], 0.0f, 400.0f, 5.0f,
                    [api, id] { return static_cast<float>(api->Get(id)); },
                    [api, id](float value) { api->Set(id, static_cast<int32_t>(value)); });
                if (added) rows |= 1u << id;
            }
        }
        if (api->MenuReady) api->MenuReady(rows);
    }
