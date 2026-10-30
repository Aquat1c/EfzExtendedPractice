#include "runtime/patch_ledger.h"

#include <algorithm>
#include <limits>

namespace Practice {
namespace {
constexpr uint32_t kExecutableWritable = 0x40; // PAGE_EXECUTE_READWRITE
// An image page reads back as EXECUTE_WRITECOPY (0x80) until its first write.
bool IsExecutableWritable(uint32_t protection) {
    return protection == kExecutableWritable || protection == 0x80;
}
bool SameModuleIdentity(const PatchModule& a, const PatchModule& b) {
    return a.base == b.base && a.incarnation == b.incarnation;
}
}

uint32_t PatchLedger::CountOwner(uint64_t owner) const {
    return static_cast<uint32_t>(std::count_if(sites_.begin(), sites_.end(),
        [owner](const Site& site) { return site.descriptor.owner == owner; }));
}
uint32_t PatchLedger::Obligations() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<uint32_t>(sites_.size());
}
uint32_t PatchLedger::ObligationsForOwner(uint64_t owner) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return CountOwner(owner);
}

bool PatchLedger::MatchesCurrent(Site& site) {
    if (!backend_.SameModule(site.descriptor.module, site.pin)) {
        site.error = PatchError::ModuleChanged;
        return false;
    }
    if (!backend_.Read(site.descriptor.address, site.readBuffer.data(), site.readBuffer.size()) ||
        site.readBuffer != site.current) {
        site.error = PatchError::ForeignBytes;
        return false;
    }
    return true;
}

bool PatchLedger::PreparePages(Site& site) {
    const uintptr_t end = site.descriptor.address + site.current.size();
    for (uintptr_t cursor = site.descriptor.address; cursor < end;) {
        PatchRegion region{};
        if (!backend_.QueryRegion(cursor, region) || !region.size ||
            region.address > cursor || region.address + region.size <= cursor ||
            region.address + region.size < region.address ||
            region.allocationBase != site.descriptor.module.base) {
            site.error = PatchError::RegionUnavailable;
            return false;
        }
        auto found = pages_.find(region.address);
        if (found != pages_.end()) {
            if (found->second.original.size != region.size ||
                found->second.original.allocationBase != region.allocationBase ||
                found->second.protectionPending ||
                (found->second.original.protection != region.protection &&
                 !IsExecutableWritable(region.protection))) {
                site.error = PatchError::PendingRestoration;
                return false;
            }
        } else {
            found = pages_.emplace(region.address, Page{region}).first;
        }
        // Reserve page references before any protection mutation.
        site.pages.push_back(region.address);
        ++found->second.references;
        cursor = (std::min)(end, region.address + region.size);
    }
    return true;
}

bool PatchLedger::MakeWritable(Site& site) {
    for (uintptr_t address : site.pages) {
        auto& page = pages_.at(address);
        PatchRegion observed{};
        if (!backend_.QueryRegion(address, observed) ||
            observed.allocationBase != page.original.allocationBase ||
            observed.size != page.original.size) {
            site.error = PatchError::RegionUnavailable;
            return false;
        }
        // A peer may leave the page executable-writable; callers already verified our bytes.
        if (!IsExecutableWritable(observed.protection) &&
            (page.protectionPending || observed.protection != page.original.protection)) {
            site.error = PatchError::ProtectionFailed;
            return false;
        }
        if (page.protectionPending) continue;
        uint32_t previous = 0;
        if (!backend_.Protect(page.original, kExecutableWritable, previous)) {
            site.error = PatchError::ProtectionFailed;
            return false;
        }
        // Account for a protection mutation before any later operation can fail.
        page.protectionPending = true;
        page.original.protection = previous;
    }
    return true;
}

bool PatchLedger::RestoreProtections(Site& site) {
    bool ok = true;
    for (uintptr_t address : site.pages) {
        auto& page = pages_.at(address);
        if (!page.protectionPending) continue;
        PatchRegion observed{};
        uint32_t previous = 0;
        if (!backend_.QueryRegion(address, observed) ||
            observed.allocationBase != page.original.allocationBase ||
            observed.size != page.original.size ||
            !IsExecutableWritable(observed.protection) ||
            !backend_.Protect(page.original, page.original.protection, previous)) {
            site.error = PatchError::ProtectionFailed;
            ok = false;
            continue;
        }
        page.protectionPending = false;
    }
    return ok;
}

bool PatchLedger::FlushPending(Site& site) {
    if (!site.flushPending) return true;
    if (!backend_.Flush(site.descriptor.address, site.current.size())) {
        site.error = PatchError::FlushFailed;
        return false;
    }
    site.flushPending = false;
    return true;
}

bool PatchLedger::RestoreSite(Site& site) {
    site.retiring = true;
    if (!MatchesCurrent(site)) return false;
    bool bytesOk = true;
    if (site.current != site.descriptor.expected) {
        if (!MakeWritable(site)) {
            RestoreProtections(site);
            return false;
        }
        const size_t written = backend_.Write(site.descriptor.address,
            site.descriptor.expected.data(), site.current.size());
        const size_t completed = (std::min)(written, site.current.size());
        std::copy_n(site.descriptor.expected.begin(), completed, site.current.begin());
        if (completed) site.flushPending = true;
        bytesOk = site.current == site.descriptor.expected;
        if (!bytesOk) site.error = PatchError::WriteFailed;
    }
    const bool flushed = FlushPending(site);
    const bool protectedAgain = RestoreProtections(site);
    if (bytesOk && flushed && protectedAgain) {
        site.error = PatchError::None;
        return true;
    }
    return false;
}

void PatchLedger::ReleaseSite(Site& site) {
    for (uintptr_t address : site.pages) {
        auto found = pages_.find(address);
        if (--found->second.references == 0 && !found->second.protectionPending)
            pages_.erase(found);
    }
    if (site.pin) backend_.Unpin(site.pin);
    site.pin = 0;
}

PatchResult PatchLedger::Acquire(const PatchDescriptor& descriptor, uint64_t& siteId) {
    siteId = 0;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto count = [&] { return static_cast<uint32_t>(sites_.size()); };
    if (!descriptor.module.base || !descriptor.module.incarnation || !descriptor.owner ||
        !descriptor.address || descriptor.expected.empty() ||
        descriptor.expected.size() != descriptor.replacement.size() ||
        descriptor.address + descriptor.expected.size() < descriptor.address ||
        nextId_ == (std::numeric_limits<uint64_t>::max)())
        return {false, count(), PatchError::InvalidDescriptor};
    for (auto& existing : sites_) {
        const auto& other = existing.descriptor;
        const bool overlaps = descriptor.address < other.address + other.expected.size() &&
            other.address < descriptor.address + descriptor.expected.size();
        if (!overlaps) continue;
        if (other.address == descriptor.address && other.owner == descriptor.owner &&
            SameModuleIdentity(other.module, descriptor.module) &&
            other.expected == descriptor.expected && other.replacement == descriptor.replacement &&
            !existing.retiring && existing.current == descriptor.replacement &&
            existing.error == PatchError::None && MatchesCurrent(existing)) {
            siteId = existing.id;
            return {true, count(), PatchError::None};
        }
        return {false, count(), PatchError::OwnerConflict};
    }

    // The site must be durable before any protection/byte mutation, even on failed acquisition.
    Site prepared{};
    prepared.id = ++nextId_;
    prepared.descriptor = descriptor;
    prepared.current = descriptor.expected;
    prepared.readBuffer.resize(descriptor.expected.size());
    prepared.pages.reserve(descriptor.expected.size());
    sites_.push_back(std::move(prepared));
    Site& site = sites_.back();
    if (!backend_.Pin(descriptor.module, site.pin)) {
        sites_.pop_back();
        return {false, count(), PatchError::PinFailed};
    }
    if (!MatchesCurrent(site)) {
        const auto error = site.error;
        ReleaseSite(site);
        sites_.pop_back();
        return {false, count(), error};
    }
    siteId = site.id;
    if (PreparePages(site) && MakeWritable(site)) {
        const size_t written = backend_.Write(descriptor.address,
            descriptor.replacement.data(), descriptor.replacement.size());
        const size_t completed = (std::min)(written, site.current.size());
        std::copy_n(descriptor.replacement.begin(), completed, site.current.begin());
        if (completed) site.flushPending = true;
        const bool bytesOk = written == descriptor.replacement.size();
        if (!bytesOk) site.error = PatchError::WriteFailed;
        const bool flushed = FlushPending(site);
        const bool protectedAgain = RestoreProtections(site);
        if (bytesOk && flushed && protectedAgain)
            return {true, count(), PatchError::None};
    }
    const auto failure = site.error;
    if (RestoreSite(site)) {
        ReleaseSite(site);
        sites_.pop_back();
    }
    return {false, count(), failure};
}

PatchResult PatchLedger::RestoreOwner(uint64_t owner) {
    std::lock_guard<std::mutex> lock(mutex_);
    PatchError error = PatchError::None;
    for (auto it = sites_.begin(); it != sites_.end();) {
        if (it->descriptor.owner != owner) { ++it; continue; }
        if (RestoreSite(*it)) {
            ReleaseSite(*it);
            it = sites_.erase(it);
        } else {
            error = it->error;
            ++it;
        }
    }
    const auto pending = CountOwner(owner);
    return {pending == 0, pending, error};
}

} // namespace Practice
