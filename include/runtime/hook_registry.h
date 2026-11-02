#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "patch_ledger.h"

namespace PracticeHooks {
enum class State { CreatedDisabled, Enabled, DisabledAwaitingDrain, Reclaimable, ExternalInstalling };
enum class Result { Complete, Pending, Conflict, BackendFailure, ForeignBytes, MissingBackend };
// Installed: our enabled image is live. Preimage: the bytes we hooked over are
// live (a peer restored them, or the record was never enabled). Foreign: neither.
enum class EntryImage { Unknown, Installed, Preimage, Foreign };
struct Outcome {
    Result result=Result::Complete;
    size_t remaining=0;
    int backendStatus=0;
    bool Complete() const { return result==Result::Complete; }
};
// Valid only for this registry epoch and issued outside all retiring callbacks.
// Callback counts cannot prove native entry unreachability or foreign-chain drainage.
struct RetirementProof {
    bool safeContinuation=false;
    bool nativeEntriesUnreachable=false;
    bool foreignChainsDrained=false;
    uint64_t registryEpoch=0;
    uint64_t continuationReceipt=0;
};
struct TargetSpec {
    void* target=nullptr;
    void* detour=nullptr;
    std::string owner;
    std::string abi;
    uint64_t moduleIncarnation=0;
    uintptr_t rangeStart=0;
    std::vector<unsigned char> preimage;
    std::shared_ptr<void> modulePin;
};
struct Backend {
    std::function<int(void*,void*,void**)> create;
    std::function<int(void*)> enable, disable, remove, queueDisable;
    std::function<int()> applyQueued;
    std::function<bool(uintptr_t,size_t,std::vector<unsigned char>&)> read;
    int ok=0, alreadyCreated=1, alreadyEnabled=2, alreadyDisabled=3;
};
struct CallbackState {
    std::atomic<uint32_t> outstanding{0};
    std::atomic<bool> admitted{false};
};
// Statically allocated per detour; the lifecycle owner binds it before enable.
// Enter reads state before counting itself; only a provider unreachability receipt covers that gap.
struct CallbackTicket {
    std::atomic<CallbackState*> state{nullptr};
};
struct Record {
    TargetSpec spec;
    State state=State::CreatedDisabled;
    void* original=nullptr;
    std::vector<unsigned char> installed;
    bool admissionOpen=false;
    int createStatus=0, enableStatus=0, disableStatus=0, removeStatus=0;
    int queueStatus=0, applyStatus=0;
    std::shared_ptr<CallbackState> callbacks=std::make_shared<CallbackState>();
    CallbackTicket* ticket=nullptr;
    // External instruction sites use the exact bound ledger, never MinHook.
    Practice::PatchLedger* externalLedger=nullptr;
    Practice::PatchDescriptor externalPatch{};
    std::shared_ptr<void> continuationPin;
    uint64_t externalSiteId=0;
};
class ExecutionGuard {
public:
    ExecutionGuard()=default;
    ExecutionGuard(const ExecutionGuard&)=delete;
    ExecutionGuard& operator=(const ExecutionGuard&)=delete;
    ExecutionGuard(ExecutionGuard&& other) noexcept;
    ~ExecutionGuard();
    static ExecutionGuard Enter(const CallbackTicket& ticket) noexcept;
    explicit operator bool() const {return state_!=nullptr;}
    bool Admitted() const {return admitted_;}
private:
    friend class Registry;
    bool admitted_=false;
    bool depthTracked_=false;
    CallbackState* state_=nullptr;
};
class Registry {
public:
    explicit Registry(Backend backend);
    Outcome Acquire(const TargetSpec&,void** original);
    Outcome PrepareExternal(const TargetSpec&,const Practice::PatchDescriptor&,
                            Practice::PatchLedger&,CallbackTicket&,
                            std::shared_ptr<void> continuationPin);
    Outcome Enable(void* target);
    Outcome CloseAdmission(const std::string& owner);
    Outcome DisableOwnedTargets(const std::string& owner,const RetirementProof&);
    Outcome ReclaimDrainedTargets(const std::string& owner,const RetirementProof&);
    Outcome BindCallback(void* target, CallbackTicket& ticket);
    ExecutionGuard Enter(const CallbackTicket& ticket) noexcept {return ExecutionGuard::Enter(ticket);}
    std::vector<Record> Snapshot() const;
    uint64_t Epoch() const;
    EntryImage Image(void* target) const;
private:
    bool Matches(const Record&,const std::vector<unsigned char>&) const;
    bool ProofCurrent(const RetirementProof&) const;
    Backend backend_;
    mutable std::mutex mutex_;
    std::vector<Record> records_;
    uint64_t epoch_=1;
};
// Clear call-site metadata only once the registry no longer lists the target,
// never because a group retirement was requested or another target in the group was removed.
template<class Original>
bool ReleaseRetiredSlot(Registry& registry, uintptr_t& target, Original& original,
                        std::atomic<bool>& created) {
    for(const auto& record:registry.Snapshot())
        if(record.spec.target==reinterpret_cast<void*>(target)) return false;
    target=0;original=nullptr;created.store(false,std::memory_order_release);
    return true;
}

template<class Original>
bool ReleaseRetiredOriginal(Registry& registry, uintptr_t target, Original& original) {
    for(const auto& record:registry.Snapshot())
        if(record.spec.target==reinterpret_cast<void*>(target)) return false;
    original=nullptr;
    return true;
}
inline bool PublishTargetActivation(Registry& registry, uintptr_t target, std::atomic<bool>& active) {
    const bool enabled=target && registry.Enable(reinterpret_cast<void*>(target)).Complete();
    active.store(enabled,std::memory_order_release);
    return enabled;
}
inline bool RestoreRetainedTargetAdmission(Registry& registry, void* target, std::atomic<bool>& active) {
    return PublishTargetActivation(registry,reinterpret_cast<uintptr_t>(target),active);
}

}
