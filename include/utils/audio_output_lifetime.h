#pragma once
#include "utils/audio_runtime_state.h"
#include <array>
namespace AudioControl {
enum class AudioOutputLane { Unknown, Bgm, Se };
enum class AudioOutputKind { DirectSound, Decoded };
struct AudioOutputBinding {
    uintptr_t owner=0, receiver=0, control=0;
    uint16_t slot=150;
    AudioOutputKind kind=AudioOutputKind::DirectSound;
    AudioOutputLane lane=AudioOutputLane::Unknown;
    uint64_t incarnation=0, rawGeneration=0, appliedRawGeneration=0;
    int rawLevel=0;
    uint32_t appliedSettings=0;
    bool rawKnown=false, applied=false;
};
struct AudioOutputOperations {
    bool (*retain)(uintptr_t) noexcept;
    void (*release)(uintptr_t) noexcept;
    bool (*current)(const AudioOutputBinding&) noexcept;
    int (*set)(const AudioOutputBinding&,int) noexcept;
};
// Native owner only: the file worker publishes settings and a wake, never bindings.
class AudioOutputRegistry {
public:
    explicit AudioOutputRegistry(AudioOutputOperations operations):operations_(operations){}
    AudioOutputBinding* Find(uintptr_t owner,uint16_t slot) noexcept {
        for(auto& b:buffers_) if(b.receiver && b.owner==owner && b.slot==slot) return &b;
        return nullptr;
    }
    AudioOutputBinding* FindReceiver(uintptr_t receiver) noexcept {
        for(auto& b:buffers_) if(b.receiver==receiver && receiver) return &b;
        return nullptr;
    }
    AudioOutputBinding* Created(uintptr_t owner,uint16_t slot,uintptr_t receiver) noexcept {
        if(!owner || slot>=150) return nullptr;
        Retire(owner,slot);
        for(auto& b:buffers_) if(!b.receiver) {
            if(!receiver || !operations_.retain(receiver)) return nullptr;
            b.owner=owner;b.slot=slot;b.receiver=receiver;b.incarnation=++incarnation_;
            return &b;
        }
        return nullptr;
    }
    AudioOutputBinding* GraphReady(uintptr_t context,uintptr_t control,uintptr_t audio) noexcept {
        if(!context || !control || !audio) return nullptr;
        for(auto& b:graphs_) if(b.owner==context && b.receiver) {
            if(b.receiver==audio && b.control==control) return &b;
            Release(b);
        }
        for(auto& b:graphs_) if(!b.receiver) {
            if(!operations_.retain(audio)) return nullptr;
            b.owner=context;b.control=control;b.receiver=audio;
            b.kind=AudioOutputKind::Decoded;b.lane=AudioOutputLane::Bgm;b.incarnation=++incarnation_;
            return &b;
        }
        return nullptr;
    }
    void Observe(AudioOutputBinding& b,AudioOutputLane lane,int raw) noexcept {
        if(!b.rawKnown || b.rawLevel!=raw || b.lane!=lane) {
            b.rawLevel=raw;b.rawKnown=true;b.lane=lane;++b.rawGeneration;
        }
    }
    int Adjust(const AudioOutputBinding& b,const AudioSettingsView& view) const noexcept {
        return ApplyAudioLaneGain(view,b.lane==AudioOutputLane::Bgm,b.rawLevel);
    }
    void Receipt(AudioOutputBinding& b,const AudioSettingsView& view,int result) noexcept {
        if(result<0) { b.applied=false;++failures_;return; }
        b.applied=true;b.appliedRawGeneration=b.rawGeneration;
        b.appliedSettings=LaneSettings(b,view);
    }
    bool Prepare(AudioOutputBinding& b,const AudioSettingsView& view) noexcept {
        if(!b.receiver || !b.rawKnown || b.lane==AudioOutputLane::Unknown) return false;
        if(!operations_.current(b)) {Release(b);return false;}
        const bool owns=b.lane==AudioOutputLane::Bgm?view.trainingOwnsBgmGain:view.trainingOwnsSeGain;
        if(!owns) {b.applied=false;return false;}
        if(b.applied && b.appliedRawGeneration==b.rawGeneration &&
           b.appliedSettings==LaneSettings(b,view)) return true;
        const int result=operations_.set(b,Adjust(b,view));
        Receipt(b,view,result);return result>=0;
    }
    void ApplyLatest(const AudioSettingsView& view) noexcept {
        for(auto& b:buffers_) Prepare(b,view);
        for(auto& b:graphs_) Prepare(b,view);
    }
    void Retire(uintptr_t owner,uint16_t slot) noexcept {if(auto* b=Find(owner,slot)) Release(*b);}
    void RetireManager(uintptr_t owner) noexcept {
        for(auto& b:buffers_) if(b.owner==owner) Release(b);
    }
    void RetireGraph(uintptr_t context) noexcept {
        for(auto& b:graphs_) if(b.owner==context) Release(b);
    }
    void RetireAll() noexcept {
        for(auto& b:buffers_) Release(b);
        for(auto& b:graphs_) Release(b);
    }
    void RebaseBgm(int raw) noexcept {
        for(auto& b:buffers_) if(b.receiver && b.lane==AudioOutputLane::Bgm) {
            Observe(b,b.lane,raw);b.applied=false;
        }
        for(auto& b:graphs_) if(b.receiver) {Observe(b,b.lane,raw);b.applied=false;}
    }
    uint32_t Failures() const noexcept {return failures_;}
private:
    static uint32_t LaneSettings(const AudioOutputBinding& b,const AudioSettingsView& v) noexcept {
        return b.lane==AudioOutputLane::Bgm ? uint32_t(v.bgmPercent)|(uint32_t(v.trainingOwnsBgmGain)<<8)
                                          : uint32_t(v.sePercent)|(uint32_t(v.trainingOwnsSeGain)<<8);
    }
    void Release(AudioOutputBinding& b) noexcept {
        const uintptr_t receiver=b.receiver;b={};
        if(receiver) operations_.release(receiver);
    }
    AudioOutputOperations operations_;
    std::array<AudioOutputBinding,150> buffers_{};
    std::array<AudioOutputBinding,8> graphs_{};
    uint64_t incarnation_=0;
    uint32_t failures_=0;
};
}
