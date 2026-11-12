// SPDX-License-Identifier: GPL-3.0-only
// CryKitty QoL menu bridge. Applied to the pinned FusionFix 5.1.2 source.
// Uses FusionFix's own registration functions; no competing game-menu hooks.
    void AddQolBundleControls()
    {
        auto module = GetModuleHandleA("GTAIV-QoL.asi");
        auto getApi = module ? reinterpret_cast<QolGetSettingsApi>(GetProcAddress(module, "QoLSettings_UiApi")) : nullptr;
        const auto api = getApi ? getApi(1) : nullptr;
        if (!api || api->version != 1 || api->size < sizeof(QolSettingsApi) || !api->Get || !api->Set)
            return;
        const char* keys[] = { "QOL_ROADNAV", "QOL_AUTOZOOM", "QOL_CSPAUSE", "QOL_TAXI", "QOL_GPSLINE", "QOL_CUTVOICE", "QOL_COMPASS", "QOL_ROUTECOL" };
        const wchar_t* labels[] = { L"RoadNav", L"Map Autozoom", L"Pause During Cutscenes", L"Taxi FOV (degrees)", L"Road Line", L"Cutscene Audio (%)", L"Compass HUD", L"RoadNav Destination Colors" };
        const auto qolScreen = AddScreen("QoL", MenuScreen::Audio);
        uint32_t rows = 0;
        for (uint32_t id = 0; id < 7; ++id) // Verified host has seven existing row slots.
        {
            if (api->Get(id) < 0) continue; // Additive settings on older core versions.
            CText::menuTexts[GetHash(keys[id])] = labels[id];
            if (id != QOL_VOCAL_SLIDERS && id != QOL_CUTSCENE_VOICE)
            {
                if (AddToggle(qolScreen, keys[id], [api, id] { return api->Get(id) != 0; },
                    [api, id](bool enabled) { api->Set(id, enabled ? 1 : 0); }))
                    rows |= 1u << id;
            }
            else
            {
                const bool taxi=id==QOL_VOCAL_SLIDERS;
                bool added = AddSlider(taxi?qolScreen:MenuScreen::Audio, keys[id], 0.0f, taxi?120.0f:1600.0f, taxi?1.0f:20.0f,
                    [api, id] { return static_cast<float>(api->Get(id)); },
                    [api, id](float value) { api->Set(id, static_cast<int32_t>(value)); });
                AddSlider(MenuScreen::TitleAudio, keys[id], 0.0f, taxi?120.0f:1600.0f, taxi?1.0f:20.0f,
                    [api, id] { return static_cast<float>(api->Get(id)); },
                    [api, id](float value) { api->Set(id, static_cast<int32_t>(value)); });
                if (added) rows |= 1u << id;
            }
        }
        if (api->MenuReady) api->MenuReady(rows);
    }
