#include "../../../include/game/mission/mission_state_dump.h"
#include "../../../include/game/mission/savestate_entity_layout.h"

#include "../../../include/game/savestate_hook.h"
#include "../../../include/utils/pause_integration.h"
#include "../../../include/utils/network.h"
#include "../../../include/utils/utilities.h"
#include "../../../include/core/constants.h"
#include "../../../include/core/logger.h"
#include "../../../include/core/memory.h"

#include <cstdint>
#include <cstring>
#include <sstream>
#include <vector>

// Ranges mirror custom_savestate.cpp.

namespace Mission::StateDump {

namespace {

constexpr uintptr_t kWrapperOffset = 52;      // session+52 on ALL versions
constexpr uint32_t  kBufferCapacity = 500000; // malloc(0x7A120) on ALL versions

struct DescLayout {
    uintptr_t baseOff;   // dword: buffer base pointer
    uintptr_t writeOff;  // dword: write pointer
    uintptr_t capOff;    // dword: capacity constant (sanity anchor)
};

bool GetDescLayout(DescLayout& out) {
    switch (GetEfzRevivalVersion()) {
        case EfzRevivalVersion::Revival102e:
        case EfzRevivalVersion::Revival102f:
        case EfzRevivalVersion::Revival102g:
        case EfzRevivalVersion::Revival102h:
        case EfzRevivalVersion::Revival102i:
            out = { 520, 524, 528 };   // dwords [130]/[131]/[132]
            return true;
        case EfzRevivalVersion::Revival102j:
            out = { 524, 528, 532 };   // j shifted one dword (list-based records)
            return true;
        default:
            return false;
    }
}

// Battle-context tail region size: e stops at ctx+1424, f..j include +1432.
uint32_t Region4Size() {
    return GetEfzRevivalVersion() == EfzRevivalVersion::Revival102e ? 0x150u : 0x158u;
}

bool ResolveBuffer(uintptr_t& outBase, uintptr_t& outWrite, std::string& err) {
    void* session = PauseIntegration::GetPracticeControllerPtr();
    if (!session) {
        session = PauseIntegration::ResolvePracticeControllerPtrNow("state dump");
    }
    if (!session) { err = "practice controller unavailable"; return false; }

    DescLayout dl;
    if (!GetDescLayout(dl)) { err = "unsupported EfzRevival version"; return false; }

    uintptr_t desc = 0;
    if (!SafeReadMemory(reinterpret_cast<uintptr_t>(session) + kWrapperOffset,
                        &desc, sizeof(desc)) || !desc) {
        err = "snapshot descriptor missing";
        return false;
    }
    uintptr_t base = 0, write = 0;
    uint32_t cap = 0;
    if (!SafeReadMemory(desc + dl.baseOff, &base, sizeof(base)) ||
        !SafeReadMemory(desc + dl.writeOff, &write, sizeof(write)) ||
        !SafeReadMemory(desc + dl.capOff, &cap, sizeof(cap))) {
        err = "descriptor read failed";
        return false;
    }
    // The capacity constant is a layout sanity anchor: anything but 500000
    // means wrong field indices - do NOT touch.
    if (!base || cap != kBufferCapacity || write < base || write - base > cap) {
        err = "descriptor sanity check failed";
        return false;
    }
    outBase = base;
    outWrite = write;
    return true;
}

// Character state struct sizes, indexed by charPtr[141] (Revival's Size[]
// table; identical across versions - the structs are efz.exe-side).
constexpr uint32_t kCharSizes[25] = {
    13424, 13392, 13400, 13392, 13392, 13384, 13488, 13384, 13400, 13400,
    13408, 13400, 13416, 13392, 13432, 13432, 13416, 13400, 13408, 13400,
    13432, 13400, 13408, 13448, 13408,
};

bool ReadLiveCharIds(uint8_t& p1, uint8_t& p2) {
    const uintptr_t b1 = GetPlayerBase(1);
    const uintptr_t b2 = GetPlayerBase(2);
    if (!b1 || !b2) return false;
    return SafeReadMemory(b1 + 141, &p1, sizeof(p1)) &&
           SafeReadMemory(b2 + 141, &p2, sizeof(p2));
}

constexpr uint32_t kMagic = 0x44535A45;  // 'EZSD'
constexpr uint16_t kFormat = 1;

#pragma pack(push, 1)
struct DumpHeader {
    uint32_t magic;
    uint16_t format;
    uint8_t  revival;      // EfzRevivalVersion numeric value (strict match)
    uint8_t  p1Char, p2Char;
    uint8_t  reserved[3];
    uint32_t payloadLen;   // buffer image bytes following this header
    uint32_t p1Size, p2Size;
    uint32_t region4;      // battle-ctx tail size used (0x150/0x158)
};
#pragma pack(pop)

const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string B64Encode(const std::vector<uint8_t>& in) {
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const uint32_t v = (in[i] << 16) | (in[i + 1] << 8) | in[i + 2];
        out.push_back(kB64[(v >> 18) & 63]);
        out.push_back(kB64[(v >> 12) & 63]);
        out.push_back(kB64[(v >> 6) & 63]);
        out.push_back(kB64[v & 63]);
    }
    const size_t rem = in.size() - i;
    if (rem == 1) {
        const uint32_t v = in[i] << 16;
        out.push_back(kB64[(v >> 18) & 63]);
        out.push_back(kB64[(v >> 12) & 63]);
        out.push_back('='); out.push_back('=');
    } else if (rem == 2) {
        const uint32_t v = (in[i] << 16) | (in[i + 1] << 8);
        out.push_back(kB64[(v >> 18) & 63]);
        out.push_back(kB64[(v >> 12) & 63]);
        out.push_back(kB64[(v >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

bool B64Decode(const std::string& in, std::vector<uint8_t>& out) {
    int8_t rev[256];
    memset(rev, -1, sizeof(rev));
    for (int i = 0; i < 64; ++i) rev[static_cast<uint8_t>(kB64[i])] = static_cast<int8_t>(i);
    out.clear();
    out.reserve((in.size() / 4) * 3);
    uint32_t acc = 0;
    int bits = 0;
    for (char c : in) {
        if (c == '=' || c == '\r' || c == '\n' || c == ' ') continue;
        const int8_t d = rev[static_cast<uint8_t>(c)];
        if (d < 0) return false;
        acc = (acc << 6) | static_cast<uint32_t>(d);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
        }
    }
    return true;
}

// Buffer offsets of every region, in the deterministic save order.
struct RegionLayout {
    size_t latch, ctx, ctxTail, gameState, p1, p2, palette, tail;
    uint32_t r4, sz1, sz2;
};

bool ComputeLayout(RegionLayout& L, uint8_t p1Char, uint8_t p2Char, size_t len,
                   std::string& err) {
    if (p1Char >= 25 || p2Char >= 25) { err = "char id out of range"; return false; }
    L.r4 = Region4Size();
    L.sz1 = kCharSizes[p1Char];
    L.sz2 = kCharSizes[p2Char];
    L.latch = 0;
    L.ctx = 4;
    L.ctxTail = L.ctx + 0x434;
    L.gameState = L.ctxTail + L.r4;
    L.p1 = L.gameState + 0x142F0;
    L.p2 = L.p1 + L.sz1;
    L.palette = L.p2 + L.sz2;
    L.tail = L.palette + 0x400;      // surface pixels + optional practice object
    if (L.tail > len) { err = "payload shorter than fixed regions"; return false; }
    return true;
}

// Overwrite pointer-bearing/runtime-owned ranges with the FRESH session's
// bytes; restoring another session's heap pointers would crash.
void Reconcile(std::vector<uint8_t>& mix, uintptr_t freshBase, const RegionLayout& L) {
    const size_t len = mix.size();
    auto keepFresh = [&](size_t off, size_t size) {
        if (off + size <= len) {
            SafeReadMemory(freshBase + off, mix.data() + off, size);
        }
    };

    // F-key latch is always restored as ZERO; a stale latch eats the next
    // F5-F8 press.
    memset(mix.data() + L.latch, 0, 4);

    // Battle context: runtime control header +4..12, runtime pointer block
    // +12..36 (P1/P2/game/input pointers), init/cleanup flags +44/45.
    keepFresh(L.ctx + 4, 32);
    keepFresh(L.ctx + 44, 2);

    // Battle-context tail (ctx bytes 1088..): game-speed byte ctx+1400 and
    // pause dword ctx+1416 stay CURRENT.
    keepFresh(L.ctxTail + (1400 - 1088), 1);
    keepFresh(L.ctxTail + (1416 - 1088), 4);

    // Runtime-owned prefix: manager pointers +0..12, resource table +56..3748; palette flags +4920/24;
    // heap object +4988, resource object +82440, speed trigger +82556, replay IO +82563.
    keepFresh(L.gameState + 0, 3748);
    // 65 system SE ids (words @+3748) and the BGM buffer id (+3878 = 0xF26). DirectSound ids are
    // handed out in session load order, so another session's ids play the wrong sounds.
    keepFresh(L.gameState + 3748, 132);
    keepFresh(L.gameState + 4920, 8);
    keepFresh(L.gameState + 4988, 4);
    keepFresh(L.gameState + 82440, 4);
    keepFresh(L.gameState + 82556, 5);
    keepFresh(L.gameState + 82563, 5);

    // Animation table +16, surface array +20, pointer dwords 30..34 (+120..140), collision table +356.
    // Rumi/Nanase stance swaps the +16/+356 pairs, so these must keep the fresh bytes.
    for (const size_t off : { L.p1, L.p2 }) {
        keepFresh(off + 16, 8);
        keepFresh(off + 120, 20);
        keepFresh(off + 356, 4);
        // Character SE bank: 50 DirectSound ids (char+612..712), same load-order rule as the system table.
        keepFresh(off + 612, 100);
        // Entity ring: 64 slots (base +1232, stride 152). Slot+8 caches the animation-table pointer, set
        // once at load and never on spawn, so a stale pointer crashes calculateEntityHitbox.
        for (std::size_t slot = 0;
             slot < ::Mission::SavestateEntityLayout::kSlotCount; ++slot) {
            keepFresh(off +
                      ::Mission::SavestateEntityLayout::SlotAnimPointerField(slot),
                      sizeof(uint32_t));
        }
    }

    uintptr_t heapObj = 0;
    memcpy(&heapObj, mix.data() + L.gameState + 4988, sizeof(heapObj));
    if (heapObj) {
        uint8_t typeByte = 0;
        if (SafeReadMemory(heapObj + 4, &typeByte, sizeof(typeByte))) {
            const size_t objSize = (typeByte == 16) ? 0x1C0 : 0xE0;
            if (objSize <= len - L.tail) {
                keepFresh(len - objSize, objSize);
            }
        }
    }
}

std::string DescribeEntityInventory(
    const ::Mission::SavestateEntityLayout::Inventory& inventory) {
    if (!inventory.valid) return "invalid";
    std::ostringstream out;
    out << "alive=" << inventory.aliveCount
        << " mask=0x" << std::hex << inventory.aliveMask
        << " checksum=0x" << inventory.checksum << std::dec
        << " cursors=" << inventory.tail << '/' << inventory.next;
    return out.str();
}

::Mission::SavestateEntityLayout::Inventory PayloadEntityInventory(
    const std::vector<uint8_t>& payload, size_t playerOffset, uint32_t playerSize) {
    if (playerOffset > payload.size() || playerSize > payload.size() - playerOffset) {
        return {};
    }
    return ::Mission::SavestateEntityLayout::Inspect(
        payload.data() + playerOffset, playerSize);
}

bool ReadLiveEntityInventory(int playerIndex, uint32_t playerSize,
                             ::Mission::SavestateEntityLayout::Inventory& out) {
    const uintptr_t base = GetPlayerBase(playerIndex);
    if (!base) return false;
    std::vector<uint8_t> state(playerSize);
    if (!SafeReadMemory(base, state.data(), state.size())) return false;
    out = ::Mission::SavestateEntityLayout::Inspect(state.data(), state.size());
    return out.valid;
}

bool VerifyOrRepairLiveEntityState(const std::vector<uint8_t>& payload,
                                   const RegionLayout& L, std::string& err) {
    struct PlayerRegion { int index; size_t offset; uint32_t size; };
    const PlayerRegion players[] = {
        { 1, L.p1, L.sz1 },
        { 2, L.p2, L.sz2 },
    };
    for (const PlayerRegion& player : players) {
        const auto expected = PayloadEntityInventory(payload, player.offset, player.size);
        ::Mission::SavestateEntityLayout::Inventory live;
        if (!expected.valid || !ReadLiveEntityInventory(player.index, player.size, live)) {
            err = "entity ring verification unavailable for P" +
                  std::to_string(player.index);
            return false;
        }
        if (::Mission::SavestateEntityLayout::Equivalent(expected, live)) {
            LogOut("[MISSION][STATE][ENTITY] verified P" +
                   std::to_string(player.index) + " " +
                   DescribeEntityInventory(live), true);
            continue;
        }

        LogOut("[MISSION][STATE][ENTITY] P" + std::to_string(player.index) +
               " post-load mismatch expected{" + DescribeEntityInventory(expected) +
               "} live{" + DescribeEntityInventory(live) + "}; repairing exact ring", true);
        const uintptr_t liveBase = GetPlayerBase(player.index);
        constexpr size_t stateOffset =
            ::Mission::SavestateEntityLayout::kEntityStateOffset;
        constexpr size_t stateSize =
            ::Mission::SavestateEntityLayout::kEntityStateEnd - stateOffset;
        if (!liveBase || player.offset + stateOffset > payload.size() ||
            stateSize > payload.size() - player.offset - stateOffset ||
            !SafeWriteMemory(liveBase + stateOffset,
                             payload.data() + player.offset + stateOffset,
                             stateSize) ||
            !ReadLiveEntityInventory(player.index, player.size, live) ||
            !::Mission::SavestateEntityLayout::Equivalent(expected, live)) {
            err = "entity ring restore mismatch for P" +
                  std::to_string(player.index);
            return false;
        }
        LogOut("[MISSION][STATE][ENTITY] repaired P" +
               std::to_string(player.index) + " " +
               DescribeEntityInventory(live), true);
    }
    return true;
}

}

bool Available() {
    DescLayout dl;
    return SavestateHook::IsInstalled() && GetDescLayout(dl);
}

bool Capture(std::string& outB64, std::string& outErr) {
    outB64.clear();
    if (!SavestateHook::IsInstalled()) { outErr = "savestate hooks not installed"; return false; }
    DescLayout dl;
    if (!GetDescLayout(dl)) { outErr = "unsupported EfzRevival version"; return false; }

    uint8_t p1 = 0, p2 = 0;
    if (!ReadLiveCharIds(p1, p2)) { outErr = "player bases unavailable"; return false; }

    if (!SavestateHook::TriggerSave()) { outErr = "TriggerSave failed"; return false; }

    uintptr_t base = 0, write = 0;
    if (!ResolveBuffer(base, write, outErr)) return false;
    const size_t len = write - base;
    if (len == 0) { outErr = "empty snapshot buffer"; return false; }

    RegionLayout L;
    if (!ComputeLayout(L, p1, p2, len, outErr)) return false;

    DumpHeader hdr = {};
    hdr.magic = kMagic;
    hdr.format = kFormat;
    hdr.revival = static_cast<uint8_t>(GetEfzRevivalVersion());
    hdr.p1Char = p1;
    hdr.p2Char = p2;
    hdr.payloadLen = static_cast<uint32_t>(len);
    hdr.p1Size = L.sz1;
    hdr.p2Size = L.sz2;
    hdr.region4 = L.r4;

    std::vector<uint8_t> blob(sizeof(hdr) + len);
    memcpy(blob.data(), &hdr, sizeof(hdr));
    if (!SafeReadMemory(base, blob.data() + sizeof(hdr), len)) {
        outErr = "buffer read failed";
        return false;
    }
    const std::vector<uint8_t> payload(blob.begin() + sizeof(hdr), blob.end());
    const auto p1Entities = PayloadEntityInventory(payload, L.p1, L.sz1);
    const auto p2Entities = PayloadEntityInventory(payload, L.p2, L.sz2);
    if (!p1Entities.valid || !p2Entities.valid) {
        outErr = "snapshot does not contain complete entity rings";
        return false;
    }
    outB64 = B64Encode(blob);
    LogOut("[MISSION][STATE] dumped " + std::to_string(len) + " bytes (chars " +
           std::to_string(p1) + "/" + std::to_string(p2) + ")", true);
    LogOut("[MISSION][STATE][ENTITY] captured P1{" +
           DescribeEntityInventory(p1Entities) + "} P2{" +
           DescribeEntityInventory(p2Entities) + "}", true);
    return true;
}

bool Restore(const std::string& b64, std::string& outErr) {
    if (!SavestateHook::IsInstalled()) { outErr = "savestate hooks not installed"; return false; }

    std::vector<uint8_t> blob;
    if (!B64Decode(b64, blob) || blob.size() < sizeof(DumpHeader)) {
        outErr = "corrupt savestate blob";
        return false;
    }
    DumpHeader hdr;
    memcpy(&hdr, blob.data(), sizeof(hdr));
    if (hdr.magic != kMagic || hdr.format != kFormat) { outErr = "bad blob header"; return false; }
    if (blob.size() - sizeof(hdr) != hdr.payloadLen) { outErr = "blob length mismatch"; return false; }
    if (hdr.revival != static_cast<uint8_t>(GetEfzRevivalVersion())) {
        outErr = "dump was made on a different EfzRevival version";
        return false;
    }

    uint8_t p1 = 0, p2 = 0;
    if (!ReadLiveCharIds(p1, p2)) { outErr = "player bases unavailable"; return false; }
    if (p1 != hdr.p1Char || p2 != hdr.p2Char) {
        outErr = "characters do not match the dump";
        return false;
    }

    if (!SavestateHook::TriggerSave()) { outErr = "TriggerSave failed"; return false; }

    uintptr_t base = 0, write = 0;
    if (!ResolveBuffer(base, write, outErr)) return false;
    const size_t len = write - base;
    if (len != hdr.payloadLen) {
        outErr = "buffer length mismatch (settings/stage differ from the dump)";
        return false;
    }

    RegionLayout L;
    if (!ComputeLayout(L, p1, p2, len, outErr)) return false;
    if (L.r4 != hdr.region4 || L.sz1 != hdr.p1Size || L.sz2 != hdr.p2Size) {
        outErr = "region layout mismatch";
        return false;
    }

    std::vector<uint8_t> mix(blob.begin() + sizeof(hdr), blob.end());
    Reconcile(mix, base, L);

    if (!SafeWriteMemory(base, mix.data(), len)) {
        outErr = "buffer write failed";
        return false;
    }
    if (!SavestateHook::TriggerLoad()) { outErr = "TriggerLoad failed"; return false; }
    if (!VerifyOrRepairLiveEntityState(mix, L, outErr)) return false;

    LogOut("[MISSION][STATE] restored " + std::to_string(len) + " bytes into live session", true);
    return true;
}

}
