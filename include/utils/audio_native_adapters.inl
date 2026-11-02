// x86 inline JMP adapters; the caller owns each qualified whole-instruction window.
// Replays the original COM stdcall cleanup; preserves nonvolatile registers, flags and the HRESULT.
extern "C" {
uintptr_t audioDsPlayContinuation=0, audioGraphRunContinuation=0;
uintptr_t audioDsCreateContinuation=0, audioSplitSetContinuation=0;
uintptr_t audioLoopPlayContinuation=0;
void __cdecl AudioPrepareDsFrame(uintptr_t*);
void __cdecl AudioPrepareGraphFrame(uintptr_t*);
void __cdecl AudioCreatedDsFrame(uintptr_t*);
void __cdecl AudioAdjustSplitFrame(uintptr_t*);
void __cdecl AudioReceiptSplitFrame(uintptr_t*);
void __cdecl AudioPrepareLoopFrame(uintptr_t*);

__declspec(naked) void AudioDsPlayAdapter() {
    __asm {
        pushfd
        pushad
        push esp
        call AudioPrepareDsFrame
        add esp,4
        popad
        popfd
        mov [esp],eax
        call dword ptr [edx+30h]
        jmp dword ptr [audioDsPlayContinuation]
    }
}
__declspec(naked) void AudioGraphRunAdapter() {
    __asm {
        pushfd
        pushad
        push esp
        call AudioPrepareGraphFrame
        add esp,4
        popad
        popfd
        mov edx,[eax]
        mov [esp],eax
        call dword ptr [edx+1ch]
        jmp dword ptr [audioGraphRunContinuation]
    }
}
__declspec(naked) void AudioDsCreateAdapter() {
    __asm {
        pushfd
        pushad
        push esp
        call AudioCreatedDsFrame
        add esp,4
        popad
        popfd
        mov [ebp-3ch],eax
        cmp dword ptr [ebp-3ch],0
        jmp dword ptr [audioDsCreateContinuation]
    }
}
__declspec(naked) void AudioSplitSetAdapter() {
    __asm {
        pushfd
        pushad
        push esp
        call AudioAdjustSplitFrame
        add esp,4
        popad
        popfd
        mov [esp+4],ecx
        call dword ptr [edx+3ch]
        pushfd
        pushad
        push esp
        call AudioReceiptSplitFrame
        add esp,4
        popad
        popfd
        jmp dword ptr [audioSplitSetContinuation]
    }
}
__declspec(naked) void AudioLoopPlayAdapter() {
    __asm {
        pushfd
        pushad
        push esp
        call AudioPrepareLoopFrame
        add esp,4
        popad
        popfd
        mov eax,[ecx]
        push edx
        call dword ptr [eax+30h]
        jmp dword ptr [audioLoopPlayContinuation]
    }
}
}
