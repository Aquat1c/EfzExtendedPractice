#include "runtime/native_game_profile.h"

#include <windows.h>
#include <wincrypt.h>
#include <array>
#include <cstring>
#include <mutex>

namespace Practice {
namespace {
std::mutex profileMutex;
bool checked = false;
PatchModule admitted{};
// CALG_SHA_256 is hidden below XPSP3; compose the ID for the XP target.
constexpr ALG_ID kSha256Algorithm = ALG_CLASS_HASH | ALG_TYPE_ANY | 12;

constexpr std::array<uint8_t, 32> kMemorialHash{{
    0x3a,0x16,0x16,0xd6,0xc7,0x6d,0x2e,0xea,0x08,0xa9,0x5b,0xb1,0x98,0xd8,0x53,0xe9,
    0x6c,0xb6,0xb9,0xb3,0x26,0x6e,0x27,0x7c,0xa1,0xd1,0x67,0x42,0x6d,0xd6,0x64,0x81
}};

bool MappedMemorialHeaders(HMODULE image) noexcept {
    __try {
        const auto* base = reinterpret_cast<const uint8_t*>(image);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
            dos->e_lfanew > 0x100000) return false;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
        return nt->Signature == IMAGE_NT_SIGNATURE &&
            nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
            nt->FileHeader.TimeDateStamp == 0x4386998Fu &&
            nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC &&
            nt->OptionalHeader.SizeOfImage == 0x3B4000u &&
            nt->OptionalHeader.ImageBase == 0x400000u &&
            reinterpret_cast<uintptr_t>(image) == 0x400000u;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool HashMatches(HMODULE image) {
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(image, path, 32768);
    if (!length || length >= 32768) return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    bool ok = CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES,
                                   CRYPT_VERIFYCONTEXT) != FALSE;
    if (ok) ok = CryptCreateHash(provider, kSha256Algorithm, 0, 0, &hash) != FALSE;
    uint8_t buffer[16384];
    while (ok) {
        DWORD read = 0;
        if (!ReadFile(file, buffer, sizeof(buffer), &read, nullptr)) { ok = false; break; }
        if (!read) break;
        ok = CryptHashData(hash, buffer, read, 0) != FALSE;
    }
    std::array<uint8_t, 32> digest{};
    DWORD bytes = static_cast<DWORD>(digest.size());
    if (ok) ok = CryptGetHashParam(hash, HP_HASHVAL, digest.data(), &bytes, 0) &&
                 bytes == digest.size() && digest == kMemorialHash;
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    CloseHandle(file);
    return ok;
}
}

bool GetQualifiedMemorialImage(PatchModule& module) {
    std::lock_guard<std::mutex> lock(profileMutex);
    if (!checked) {
        checked = true;
        HMODULE image = GetModuleHandleW(nullptr);
        PatchModule captured{};
        if (image && CaptureNativePatchModule(reinterpret_cast<uintptr_t>(image), captured) &&
            MappedMemorialHeaders(image) && HashMatches(image))
            admitted = std::move(captured);
    }
    module = admitted;
    return module.base != 0;
}

}
