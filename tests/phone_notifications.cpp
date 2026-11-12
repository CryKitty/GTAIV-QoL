#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cassert>
#include <cstring>
#include <cstdio>
#include "../vendor/roadnav/src/phone_notifications.hpp"
#include "../vendor/scripthook/NativeContext.h"
#include "../vendor/roadnav/src/compass_controller.hpp"
namespace {
unsigned calls=0,lastUnread=0;
GameTypes::scrNativeCallContext* lastContext=nullptr;
__declspec(noinline) void __cdecl gameWaiting(GameTypes::scrNativeCallContext* context){
    ++calls;lastContext=context;
    const auto* header=reinterpret_cast<const std::uint32_t*>(context);
    lastUnread=*reinterpret_cast<const std::uint32_t*>(header[2]);
    *reinterpret_cast<std::uint32_t*>(header[0])=0x1234abcd;
}
unsigned phoneCalls=0;bool connected=true;WORD buttons=0;BYTE leftTrigger=77,rightTrigger=199;DWORD packet=12345;
__declspec(noinline) void __cdecl gamePhone(GameTypes::scrNativeCallContext* context){
    ++phoneCalls;const auto* header=reinterpret_cast<const std::uint32_t*>(context);
    *reinterpret_cast<std::uint32_t*>(header[0])=0xabcdef12;
}
__declspec(noinline) void __cdecl gameDestroy(GameTypes::scrNativeCallContext* context){
    ++phoneCalls;const auto* header=reinterpret_cast<const std::uint32_t*>(context);
    *reinterpret_cast<std::uint32_t*>(header[0])=0x12345678;
}
__declspec(noinline) DWORD WINAPI testPad(DWORD index,XINPUT_STATE* state){
    if(index||!connected)return ERROR_DEVICE_NOT_CONNECTED;
    *state={};state->dwPacketNumber=packet;state->Gamepad.wButtons=buttons;state->Gamepad.sThumbLX=123;state->Gamepad.sThumbLY=-234;
    state->Gamepad.sThumbRX=345;state->Gamepad.sThumbRY=-456;state->Gamepad.bLeftTrigger=leftTrigger;state->Gamepad.bRightTrigger=rightTrigger;return ERROR_SUCCESS;
}

__declspec(noinline) DWORD WINAPI forwardedPad(DWORD index,XINPUT_STATE* state){return testPad(index,state);}
}
int main(){
    PhoneNotifications messages;messages.start(nullptr,reinterpret_cast<void*>(&gameWaiting),reinterpret_cast<void*>(&gamePhone),reinterpret_cast<void*>(&gameDestroy));assert(messages.active());
    const auto call=[&](unsigned unread){NativeContext context;context.Push(unread);gameWaiting(&context);assert(lastContext==&context&&lastUnread==unread&&context.GetResult<unsigned>()==0x1234abcd);};
    CompassController controller(&testPad);bool raw=false;
    buttons=XINPUT_GAMEPAD_DPAD_LEFT;assert(controller.left(false,raw)&&raw);
    buttons=0;assert(!controller.left(true,raw)&&raw); // Prefer actual D-pad over mismatched native IDs.
    connected=false;assert(controller.left(true,raw)&&!raw);connected=true;
    unsigned char frontend=0;
    assert(CompassInput::attach(&testPad,0));
    CompassInput::publish(true,reinterpret_cast<std::uintptr_t>(&frontend),&messages);
    XINPUT_STATE filtered{};buttons=XINPUT_GAMEPAD_DPAD_LEFT|XINPUT_GAMEPAD_DPAD_RIGHT|XINPUT_GAMEPAD_A;
    assert(testPad(0,&filtered)==ERROR_SUCCESS);
    assert(filtered.Gamepad.wButtons==(XINPUT_GAMEPAD_DPAD_RIGHT|XINPUT_GAMEPAD_A));
    assert(filtered.dwPacketNumber==12345&&filtered.Gamepad.sThumbLX==123&&filtered.Gamepad.bRightTrigger==199);
    assert(controller.left(false,raw)&&raw); // Hold detector still sees physical Left.
    assert(controller.sample().Gamepad.bRightTrigger==199);
    assert(CompassInput::attach(&forwardedPad,3)); // Extended API forwarding to a hooked named API.
    CompassController forwarded(&forwardedPad);assert(forwarded.left(false,raw)&&raw);
    forwardedPad(0,&filtered);assert((filtered.Gamepad.wButtons&XINPUT_GAMEPAD_DPAD_LEFT)==0);
    // Across a vehicle-entry control suspension and re-enable, release of a
    // held fire/aim trigger must reach the game immediately through BOTH APIs.
    // The hook owns no fire latch: even an unchanged packet must not replay it.
    for(bool enabled:{false,true})for(BYTE trigger:{BYTE{199},BYTE{0}}){
        CompassInput::publish(enabled,reinterpret_cast<std::uintptr_t>(&frontend),&messages);
        leftTrigger=rightTrigger=trigger;
        for(auto get:{&testPad,&forwardedPad}){
            XINPUT_STATE expected{};CompassInput::pollRaw(get,0,&expected);
            assert(expected.Gamepad.bLeftTrigger==trigger&&expected.Gamepad.bRightTrigger==trigger);
            if(enabled)expected.Gamepad.wButtons&=static_cast<WORD>(~XINPUT_GAMEPAD_DPAD_LEFT);
            XINPUT_STATE actual{};assert(get(0,&actual)==ERROR_SUCCESS);
            assert(std::memcmp(&actual,&expected,sizeof(actual))==0);
        }
        assert(controller.left(false,raw)&&raw);
        assert(controller.sample().Gamepad.bLeftTrigger==trigger&&controller.sample().Gamepad.bRightTrigger==trigger);
    }
    // A new physical packet also releases every held button. Disconnect must
    // clear our diagnostic snapshot rather than reporting the last held fire.
    buttons=0;++packet;testPad(0,&filtered);assert(!filtered.Gamepad.wButtons&&!filtered.Gamepad.bRightTrigger);
    connected=false;assert(!controller.left(false,raw)&&!raw);
    assert(!controller.sample().dwPacketNumber&&!controller.sample().Gamepad.bRightTrigger);connected=true;
    buttons=XINPUT_GAMEPAD_DPAD_LEFT|XINPUT_GAMEPAD_DPAD_RIGHT|XINPUT_GAMEPAD_A;
    frontend=1;testPad(0,&filtered);assert(filtered.Gamepad.wButtons==buttons); // Pause without script tick.
    frontend=0;CompassInput::publish(false,reinterpret_cast<std::uintptr_t>(&frontend),&messages);
    testPad(0,&filtered);assert(filtered.Gamepad.wButtons==buttons); // Menu feature disabled.
    CompassInput::publish(true,0,&messages);testPad(0,&filtered);assert(filtered.Gamepad.wButtons==buttons); // Unknown frontend.
    CompassInput::publish(true,reinterpret_cast<std::uintptr_t>(&frontend),&messages);
    Sleep(270);testPad(0,&filtered);assert(filtered.Gamepad.wButtons==buttons); // Stale script.
    CompassInput::publish(true,reinterpret_cast<std::uintptr_t>(&frontend),&messages);
    const auto phone=[&](unsigned usingPhone){NativeContext context;context.Push(usingPhone);gamePhone(&context);assert(context.GetResult<unsigned>()==0xabcdef12);};
    phone(2);assert(messages.phoneInUse());
    testPad(0,&filtered);assert(filtered.Gamepad.wButtons==buttons); // Live phone opens before next HUD tick.
    NativeContext destroyed;gameDestroy(&destroyed);assert(destroyed.GetResult<unsigned>()==0x12345678&&!messages.phoneInUse()&&phoneCalls==2);
    assert(!messages.snapshot().waiting);
    call(1);assert(messages.snapshot().waiting&&messages.updates()==1&&calls==1);
    // Repeated true must retain the pending notice, including multiple texts.
    call(1);assert(messages.snapshot().waiting&&messages.updates()==2&&calls==2);
    // Invalid scalar still reaches the game unchanged but cannot alter our HUD.
    call(7);assert(messages.snapshot().waiting&&messages.updates()==2&&calls==3);
    call(0);assert(!messages.snapshot().waiting&&messages.updates()==3&&calls==4);
    call(1);assert(messages.snapshot().waiting);
    messages.stop();assert(!messages.active()&&!messages.snapshot().waiting);
    call(0);assert(calls==6&&messages.updates()==0); // Original handler restored.
    messages.start(nullptr,reinterpret_cast<void*>(&gameWaiting));assert(messages.active());call(1);assert(messages.snapshot().waiting);messages.stop();
    CompassInput::stop();testPad(0,&filtered);assert(filtered.Gamepad.wButtons==buttons);
    MH_Uninitialize();
    std::puts("Phone notifications: original context, argument and result preserved; new/multiple/read texts tracked; invalid state ignored; stop/reinstall passed");
}
