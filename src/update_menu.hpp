#pragma once
#include <atomic>
#include <cwchar>
#include <string>
#include "update_status_format.hpp"
#include "qol_version.hpp"
namespace qolupdatemenu {
inline std::atomic<bool> requestFailed{false};
inline unsigned fileAge(const wchar_t* file){
    WIN32_FILE_ATTRIBUTE_DATA attributes{};FILETIME now{};GetSystemTimeAsFileTime(&now);
    if(!GetFileAttributesExW(file,GetFileExInfoStandard,&attributes))return 0;
    ULARGE_INTEGER a{},b{};a.LowPart=now.dwLowDateTime;a.HighPart=now.dwHighDateTime;
    b.LowPart=attributes.ftLastWriteTime.dwLowDateTime;b.HighPart=attributes.ftLastWriteTime.dwHighDateTime;
    return a.QuadPart>b.QuadPart?static_cast<unsigned>(std::min<ULONGLONG>((a.QuadPart-b.QuadPart)/10000000,86400)):0;
}
inline std::wstring label(bool enabled){
    wchar_t receipt[MAX_PATH]{},request[MAX_PATH]{};const auto length=GetModuleFileNameW(nullptr,receipt,MAX_PATH);
    if(!length||length>=MAX_PATH)return format(QOL_VERSION_WIDE,QOL_BUILD_WIDE,"failed",L"");
    auto end=wcsrchr(receipt,L'\\');if(!end)return format(QOL_VERSION_WIDE,QOL_BUILD_WIDE,"failed",L"");
    if(MAX_PATH-static_cast<std::size_t>(end+1-receipt)<64)return format(QOL_VERSION_WIDE,QOL_BUILD_WIDE,"failed",L"");
    wcscpy_s(end+1,MAX_PATH-static_cast<std::size_t>(end+1-receipt),L"GTAIV-QoL-update-info.ini");
    wcscpy_s(request,receipt);auto requestEnd=wcsrchr(request,L'\\');
    wcscpy_s(requestEnd+1,MAX_PATH-static_cast<std::size_t>(requestEnd+1-request),L"GTAIV-QoL-update-request.txt");
    // Bounded direct reads avoid Windows profile-cache staleness after an
    // atomic Linux-side replacement. Sharing DELETE permits that replacement.
    const auto file=CreateFileW(receipt,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    Receipt parsed;
    if(file!=INVALID_HANDLE_VALUE){char bytes[4097]{};DWORD count=0;
        const bool ok=ReadFile(file,bytes,4097,&count,nullptr)!=0;CloseHandle(file);
        if(ok)parsed=parse(std::string(bytes,count));
    }
    const std::string compiled=QOL_SOURCE_SHA;
    const bool matching=compiled.size()==40&&compiled==parsed.sha;
    std::wstring date=matching?parsed.updated:L"";
    std::string code=file==INVALID_HANDLE_VALUE?"idle":parsed.valid?parsed.state:"failed";
    if(!matching&&code=="updated")code="idle";
    const bool queued=enabled&&GetFileAttributesW(request)!=INVALID_FILE_ATTRIBUTES;
    code=pendingState(code,queued,requestFailed.load(),fileAge(request),fileAge(receipt));
    if(!enabled&&code!="failed")code="idle";
    return format(QOL_VERSION_WIDE,QOL_BUILD_WIDE,code,date);
}
}
