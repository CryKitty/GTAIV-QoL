"""Apply the minimal QoL menu bridge to the pinned GPL-3.0 FusionFix source."""
from pathlib import Path
import shutil
import sys

PIN = 'cad0970e2c82712d86c849974b62d078331ddd88'  # v5.1.2


def patch(root):
    own = Path(__file__).resolve().parents[1]
    path = root/'source/settings.ixx'
    source = path.read_text(encoding='utf-8-sig')
    call = '        InitializeMenuAPI();\n'
    declaration = 'public:\n    // A custom screen is shown in place of the Display screen'
    if source.count(call) != 1 or source.count(declaration) != 1:
        raise RuntimeError('FusionFix source layout differs from the pinned menu bridge.')
    source = source.replace('#include <common.hxx>', '#include <common.hxx>\n#include "qol_settings_api.h"', 1)
    source = source.replace(call, call+'        AddQolBundleControls();\n', 1)
    bridge=(own/'tools/fusionfix-qol-menu.inc').read_text()
    source = source.replace(declaration, bridge+'\n'+declaration, 1)
    copy_row='    static const char* __cdecl CopyRowLabel(const char* label, wchar_t* text)\n    {\n'
    # Retained build91 inputs deliberately omit the optional label API.
    if 'static inline const QolSettingsApi* qolApi=' in bridge:
        if source.count(copy_row)!=1:raise RuntimeError('FusionFix row-label bridge differs from pinned source')
        source=source.replace(copy_row,copy_row+"        if (qolApi && qolApi->size >= sizeof(QolSettingsApi) && qolApi->MenuLabel && qolApi->MenuLabel(label,text,60)) return nullptr;\n",1)
    source += '\nextern \"C\" __declspec(dllexport) uint32_t QoLFusionFixBridgeVersion() { return 1; }\n'
    path.write_text(source, encoding='utf-8')
    shutil.copyfile(own/'src/qol_settings_api.h', root/'source/qol_settings_api.h')

    # DllMain must not depend on stack walking to recognize UAL. On Proton,
    # or a loader stack without usable unwind data, the old check can miss UAL
    # and initialize game hooks while its LoadLibrary call is still in progress.
    common = root/'source/common.ixx'
    common_source = common.read_text(encoding='utf-8-sig')
    begin = common_source.index('export bool IsUALPresent()\n')
    end = common_source.index('\nexport class CallbackHandler', begin)
    common_source = common_source[:begin]+'''export bool IsSupportedGameProcess()
{
    wchar_t path[MAX_PATH]{};
    const auto size = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!size || size >= MAX_PATH) return false;
    const auto slash = wcsrchr(path, L'\\\\');
    const auto name = slash ? slash + 1 : path;
    return _wcsicmp(name, L"GTAIV.exe") == 0 || _wcsicmp(name, L"EFLC.exe") == 0;
}

export bool IsUALPresent()
{
    // Ultimate ASI Loader identifies itself through this export. Check the
    // executable (loader smoke host) and supported proxy names directly;
    // no stack capture, symbol lookup or additional DLL loading under DllMain.
    const wchar_t* proxies[] = { nullptr, L"dinput8.dll", L"dsound.dll", L"xlive.dll",
        L"version.dll", L"winmm.dll", L"wininet.dll", L"d3d8.dll", L"d3d9.dll",
        L"d3d11.dll", L"dinput.dll", L"xinput1_3.dll", L"xinput1_4.dll",
        L"msacm32.dll", L"binkw32.dll", L"vorbisFile.dll" };
    for (const auto* name : proxies)
    {
        const auto module = GetModuleHandleW(name);
        if (module && IsModuleUAL(module)) return true;
    }
    return false;
}
'''+common_source[end:]
    common.write_text(common_source, encoding='utf-8')

    # The pinned Common constructor scans the host executable before DllMain.
    # Never scan a launcher or another program that happens to load the ASI.
    variables = root/'source/comvars.ixx'
    variable_source = variables.read_text(encoding='utf-8-sig')
    constructor = '    Common()\n    {\n'
    if variable_source.count(constructor) != 1:
        raise RuntimeError('FusionFix common constructor differs from pinned source.')
    variable_source = variable_source.replace(constructor, constructor+'        if (!IsSupportedGameProcess()) return;\n', 1)
    variables.write_text(variable_source, encoding='utf-8')
    dllmain = root/'source/dllmain.cpp'
    dll_source = dllmain.read_text(encoding='utf-8-sig')
    startup = '    void __declspec(dllexport) InitializeASI()\n    {\n'
    if dll_source.count(startup) != 1:
        raise RuntimeError('FusionFix ASI startup differs from pinned source.')
    dll_source = dll_source.replace(startup, startup+'        if (!IsSupportedGameProcess()) return;\n', 1)
    dllmain.write_text(dll_source, encoding='utf-8')

    # Expose a read-only range pointer already resolved by the pinned host.
    fps = root/'source/frameratevigilante.ixx'
    fps_source = fps.read_text(encoding='utf-8-sig')
    anchor = 'namespace CPhysics\n'
    range_line = 'auto pattern = find_pattern("0F 86 ? ? ? ? F3 0F 10 15 ? ? ? ? 0F 2E CA", "0F 86 ? ? ? ? F3 0F 10 0D ? ? ? ? 0F 2E C1");' 
    if fps_source.count(anchor) != 1 or fps_source.count(range_line) != 1:
        raise RuntimeError('FusionFix radar range layout differs from the pinned source.')
    fps_source = fps_source.replace(anchor, (own/'tools/fusionfix-compass-range.inc').read_text()+'\n'+anchor, 1)
    fps_source = fps_source.replace(range_line, range_line+'\n                    QolCompassRadarRangeAddress = *pattern.get_first<float*>(10);')
    fps.write_text(fps_source, encoding='utf-8')


if __name__ == '__main__': patch(Path(sys.argv[1]))
