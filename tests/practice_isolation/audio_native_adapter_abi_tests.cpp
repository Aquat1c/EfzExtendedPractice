#include <cstdint>
#include <cstdio>
#include "utils/audio_native_adapters.inl"
namespace {
int failures, nativeCalls, privateCalls, receipt, receiver, arg1,arg2,arg3;
uintptr_t seamEsp,returnedEsp,returnedEax,returnedEbx,returnedEsi,returnedEdi;
uintptr_t createdResult;
uintptr_t table[20]{},object;
void Check(bool b,const char* m){if(!b){++failures;std::printf("FAIL %s\n",m);}}
__declspec(naked) void NativePlay(){__asm{
 inc nativeCalls
 mov eax,[esp+4]
 mov receiver,eax
 mov eax,[esp+8]
 mov arg1,eax
 mov eax,[esp+12]
 mov arg2,eax
 mov eax,[esp+16]
 mov arg3,eax
 mov eax,012345678h
 ret 16
}}
__declspec(naked) void NativeRun(){__asm{
 inc nativeCalls
 mov eax,[esp+4]
 mov receiver,eax
 mov eax,1
 ret 4
}}
__declspec(naked) void NativeSet(){__asm{
 inc nativeCalls
 mov eax,[esp+4]
 mov receiver,eax
 mov eax,[esp+8]
 mov arg1,eax
 mov eax,080004005h
 ret 8
}}
__declspec(naked) int Finish(){__asm{
 mov returnedEsp,esp
 mov returnedEax,eax
 mov returnedEbx,ebx
 mov returnedEsi,esi
 mov returnedEdi,edi
 lea esp,[ebp-12]
 pop edi
 pop esi
 pop ebx
 pop ebp
 ret
}}
__declspec(naked) int DrivePlay(){__asm{
 push ebp
 mov ebp,esp
 push ebx
 push esi
 push edi
 sub esp,16
 mov seamEsp,esp
 mov dword ptr [esp+4],0
 mov dword ptr [esp+8],0
 mov dword ptr [esp+12],7
 mov ebx,011112222h
 mov esi,033334444h
 mov edi,055556666h
 lea eax,object
 lea edx,table
 jmp AudioDsPlayAdapter
}}
__declspec(naked) int DriveRun(){__asm{
 push ebp
 mov ebp,esp
 push ebx
 push esi
 push edi
 sub esp,4
 mov seamEsp,esp
 mov ebx,011112222h
 mov esi,033334444h
 mov edi,055556666h
 lea eax,object
 jmp AudioGraphRunAdapter
}}
__declspec(naked) int DriveSet(){__asm{
 push ebp
 mov ebp,esp
 push ebx
 push esi
 push edi
 sub esp,8
 mov seamEsp,esp
 lea eax,object
 mov [esp],eax
 lea edx,table
 mov ecx,-500
 mov ebx,011112222h
 mov esi,033334444h
 mov edi,055556666h
 jmp AudioSplitSetAdapter
}}
__declspec(naked) int DriveLoop(){__asm{
 push ebp
 mov ebp,esp
 push ebx
 push esi
 push edi
 push 7
 push 0
 push 0
 mov seamEsp,esp
 lea ecx,object
 lea edx,object
 mov ebx,011112222h
 mov esi,033334444h
 mov edi,055556666h
 jmp AudioLoopPlayAdapter
}}
__declspec(naked) int FinishCreate(){__asm{
 mov eax,[ebp-3ch]
 mov createdResult,eax
 jmp Finish
}}
__declspec(naked) int DriveCreate(){__asm{
 push ebp
 mov ebp,esp
 push ebx
 push esi
 push edi
 sub esp,64
 mov dword ptr [ebp-3ch],123
 mov eax,0
 jmp AudioDsCreateAdapter
}}
void Preserved(){
 Check(returnedEbx==0x11112222 && returnedEsi==0x33334444 && returnedEdi==0x55556666,"native nonvolatile registers preserved");
}
}
extern "C" void __cdecl AudioPrepareDsFrame(uintptr_t* f){++privateCalls;Check(f[7]==reinterpret_cast<uintptr_t>(&object),"DS live receiver frame");}
extern "C" void __cdecl AudioPrepareGraphFrame(uintptr_t* f){++privateCalls;Check(f[7]==reinterpret_cast<uintptr_t>(&object),"Run live IMediaControl frame");}
extern "C" void __cdecl AudioCreatedDsFrame(uintptr_t*){++privateCalls;}
extern "C" void __cdecl AudioAdjustSplitFrame(uintptr_t* f){++privateCalls;f[6]=uintptr_t(-1102);}
extern "C" void __cdecl AudioReceiptSplitFrame(uintptr_t* f){receipt=int(f[7]);}
extern "C" void __cdecl AudioPrepareLoopFrame(uintptr_t*){++privateCalls;}
int main(){
 object=reinterpret_cast<uintptr_t>(table);
 table[12]=reinterpret_cast<uintptr_t>(&NativePlay);table[7]=reinterpret_cast<uintptr_t>(&NativeRun);table[15]=reinterpret_cast<uintptr_t>(&NativeSet);
 audioDsPlayContinuation=audioGraphRunContinuation=audioSplitSetContinuation=reinterpret_cast<uintptr_t>(&Finish);
 DrivePlay();Check(nativeCalls==1 && privateCalls==1,"accepted Play prepares then invokes native once");
 Check(receiver==int(reinterpret_cast<uintptr_t>(&object)) && arg1==0 && arg2==0 && arg3==7,"four Play stack slots forwarded");
 Check(returnedEsp==seamEsp+16 && returnedEax==0x12345678,"Play cleanup and raw HRESULT");Preserved();
 nativeCalls=privateCalls=0;DriveRun();
 Check(nativeCalls==1 && privateCalls==1 && returnedEax==1,"Run forwards asynchronous success once");
 Check(returnedEsp==seamEsp+4,"Run one-slot native cleanup");Preserved();
 nativeCalls=privateCalls=0;DriveSet();
 Check(nativeCalls==1 && privateCalls==1 && arg1==-1102,"split effective SetVolume transforms once");
 Check(uint32_t(receipt)==0x80004005 && returnedEax==0x80004005,"actual failing COM HRESULT captured before native logging");
 Check(returnedEsp==seamEsp+8,"split two-slot cleanup");Preserved();
 audioLoopPlayContinuation=reinterpret_cast<uintptr_t>(&Finish);
 nativeCalls=privateCalls=0;DriveLoop();
 Check(nativeCalls==1 && privateCalls==1 && returnedEsp==seamEsp+12,"loop accepted Play receiver push and cleanup");
 Check(returnedEax==0x12345678 && arg3==7,"loop raw HRESULT and flags");Preserved();
 audioDsCreateContinuation=reinterpret_cast<uintptr_t>(&FinishCreate);
 privateCalls=0;DriveCreate();Check(privateCalls==1 && createdResult==0,"creation receipt replays native HRESULT store");
 std::printf("audio x86 adapter ABI: %d failures\n",failures);return failures?1:0;
}
