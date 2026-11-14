#pragma once
#include <string>
namespace qolfooter {
inline std::wstring extend(const std::wstring& source,const std::wstring& version,const std::wstring& build){
    // This marker is added only by FusionFix ExtraInfo's Game-tab branch.
    // Graphics/Audio/help text must never acquire an imitation footer.
    const auto start=source.find(L"Fusion Fix: v");
    if(source.size()>4096||start==std::wstring::npos||source.find(L"~p~")==std::wstring::npos||source.find(L"CryKitty QoL:")!=std::wstring::npos)return source;
    auto result=source;const auto nextLine=source.find(L"~n~",start);
    result.insert(nextLine==std::wstring::npos?source.size():nextLine,
        L"~n~                        ~p~CryKitty QoL: "+version+L" "+build);
    return result;
}
}
