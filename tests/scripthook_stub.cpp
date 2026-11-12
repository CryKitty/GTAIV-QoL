// Loader test only; never packaged with the gameplay build.
#define SCRIPTHOOK_EXPORTS
#include "../vendor/scripthook/ScriptThread.h"
ScriptThread::ScriptThread():m_pBaseThread(nullptr){}
ScriptThread::~ScriptThread(){}
void ScriptThread::RunTick(){}
void ScriptThread::RunScript(){}
void ScriptThread::OnStart(){}
void ScriptThread::OnKill(){}

#include "../vendor/scripthook/Services/D3DHook.h"
class StubRenderHook final:public ID3DDeviceHook {
public:
 void OnCreateDevice(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*) override{}
 void OnResetDevice(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*) override{}
 void OnLostDevice(IDirect3DDevice9*) override{}
 void OnDestroyDevice(IDirect3DDevice9*) override{}
 void OnRender(IDirect3DDevice9*) override{}
};
extern "C" __declspec(dllexport) ID3DDeviceHook* StubCreateRenderHook(){return new StubRenderHook;}

IService::~IService(){} // SDK service stub for standalone tests only.

// Export the real private-static C++ ABI rather than a linker alias.
class NativeContext;
class __declspec(dllexport) NativeInvoke {
    static void __cdecl SpeedyInvoke(NativeContext*,unsigned);
};
void __cdecl NativeInvoke::SpeedyInvoke(NativeContext*,unsigned){}
