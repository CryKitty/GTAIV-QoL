#pragma once
// Observe faults without handling them or changing the game's exception chain.
// Fixed buffers and append-only Win32 I/O keep this independent of normal logs.
namespace passivecrash {
inline char path[MAX_PATH]{};
inline volatile LONG entries=0;
inline LONG CALLBACK Observe(EXCEPTION_POINTERS* fault){
    if(!fault||!fault->ExceptionRecord||!fault->ContextRecord||!path[0])return EXCEPTION_CONTINUE_SEARCH;
    const auto code=fault->ExceptionRecord->ExceptionCode;
    if(code!=EXCEPTION_ACCESS_VIOLATION&&code!=EXCEPTION_ILLEGAL_INSTRUCTION&&code!=EXCEPTION_INT_DIVIDE_BY_ZERO)return EXCEPTION_CONTINUE_SEARCH;
    if(InterlockedIncrement(&entries)>12)return EXCEPTION_CONTINUE_SEARCH;
    char output[4096]{};const auto* context=fault->ContextRecord;HMODULE module=nullptr;char name[MAX_PATH]{};
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCSTR>(context->Eip),&module);
    if(module)GetModuleFileNameA(module,name,MAX_PATH);
    int count=std::snprintf(output,sizeof(output),"\r\nQoL passive fault code=%08lX eip=%08lX module=%s offset=%08lX esp=%08lX eax=%08lX ecx=%08lX edx=%08lX\r\n",code,context->Eip,name,context->Eip-static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(module)),context->Esp,context->Eax,context->Ecx,context->Edx);
    if(code==EXCEPTION_ACCESS_VIOLATION&&fault->ExceptionRecord->NumberParameters>=2)
        count+=std::snprintf(output+count,sizeof(output)-count,"access=%lu target=%08lX\r\n",static_cast<unsigned long>(fault->ExceptionRecord->ExceptionInformation[0]),static_cast<unsigned long>(fault->ExceptionRecord->ExceptionInformation[1]));
    DWORD stack[32]{};SIZE_T copied=0;
    if(ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(context->Esp),stack,sizeof(stack),&copied)){
        for(unsigned i=0;i<copied/sizeof(DWORD)&&count<3000;++i)count+=std::snprintf(output+count,sizeof(output)-count,"stack[%02u]=%08lX\r\n",i,stack[i]);
    }
    HANDLE file=CreateFileA(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file!=INVALID_HANDLE_VALUE){DWORD written=0;WriteFile(file,output,static_cast<DWORD>(count),&written,nullptr);CloseHandle(file);}
    return EXCEPTION_CONTINUE_SEARCH;
}
inline void Install(const char* folder){
    static bool installed=false;if(installed)return;
    if(std::strlen(folder)+sizeof("RoadNav-arrow.log")>=MAX_PATH)return;
    strcpy_s(path,folder);strcat_s(path,"RoadNav-arrow.log");
    installed=AddVectoredExceptionHandler(0,Observe)!=nullptr;
}
}
