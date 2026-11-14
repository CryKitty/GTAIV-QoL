// SPDX-License-Identifier: GPL-3.0-only
// CryKitty QoL menu bridge. Applied to the pinned FusionFix 5.1.2 source.
// Uses FusionFix's own registration functions; no competing game-menu hooks.
    static inline const QolSettingsApi* qolApi=nullptr;
    void AddQolBundleControls()
    {
        auto module = GetModuleHandleA("GTAIV-QoL.asi");
        auto getApi = module ? reinterpret_cast<QolGetSettingsApi>(GetProcAddress(module, "QoLSettings_UiApi")) : nullptr;
        const auto api = getApi ? getApi(1) : nullptr;
        if (!api || api->version != 1 || api->size < offsetof(QolSettingsApi, MenuLabel) || !api->Get || !api->Set)
            return;
        qolApi=api;
        if(api->size>=sizeof(QolSettingsApi)&&api->SetMenuRefresh)api->SetMenuRefresh([]{ FillMenu(0); });
        const char* keys[] = { "QOL_ROADNAV", "QOL_AUTOZOOM", "QOL_CSPAUSE", "QOL_TAXI", "QOL_GPSLINE", "QOL_CUTVOICE", "QOL_COMPASS", "QOL_ESCAPE", "QOL_MISSION", "QOL_FRIEND", "QOL_ENEMY", "QOL_CUSTOM", "QOL_UPDATES", "QOL_CAMHEIGHT" };
        const wchar_t* labels[] = { L"Turn Arrows", L"Map Autozoom", L"Pause During Cutscenes", L"Taxi FOV (degrees)", L"Road Line", L"Cutscene Audio (%)", L"Compass HUD", L"Police Escape Line", L"Mission Line", L"Friend Line", L"Enemy Line", L"Custom Line", L"Check for QoL Updates", L"Driving Camera Height" };
        const auto qolScreen = AddScreen("QoL", MenuScreen::Audio);
        uint32_t rows = 0;
        const uint32_t order[]={0,1,2,3,13,4,7,8,9,10,11,5,6,12};
        for (const auto id : order) // Children immediately follow their master.
        {
            if (api->Get(id) < 0) continue; // Additive settings on older core versions.
            CText::menuTexts[GetHash(keys[id])] = labels[id];
            if (id != QOL_VOCAL_SLIDERS && id != QOL_CUTSCENE_VOICE && id != 13)
            {
                if (AddToggle(qolScreen, keys[id], [api, id] { return api->Get(id) != 0; },
                    [api, id](bool enabled) { api->Set(id, enabled ? 1 : 0); if(id==4) FillMenu(0); }))
                    rows |= 1u << id;
            }
            else
            {
                const bool taxi=id==QOL_VOCAL_SLIDERS,height=id==13;
                bool added = AddSlider(taxi||height?qolScreen:MenuScreen::Audio, keys[id], 0.0f, height?2.0f:taxi?120.0f:1600.0f, height?1.0f:taxi?1.0f:20.0f,
                    [api, id] { return static_cast<float>(api->Get(id)); },
                    [api, id](float value) { api->Set(id, static_cast<int32_t>(value)); });
                AddSlider(MenuScreen::TitleAudio, keys[id], 0.0f, height?2.0f:taxi?120.0f:1600.0f, height?1.0f:taxi?1.0f:20.0f,
                    [api, id] { return static_cast<float>(api->Get(id)); },
                    [api, id](float value) { api->Set(id, static_cast<int32_t>(value)); });
                if (added) rows |= 1u << id;
            }
        }
        if (api->MenuReady) api->MenuReady(rows);
    }
