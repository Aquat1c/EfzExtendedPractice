#include "game/character_hotswap_frontend.h"
namespace CharacterHotswap {
bool LoadingRequestOwner::SameSession(const EfzTmIdentityV1& identity) const {
    return identity.providerIncarnation==identity_.providerIncarnation &&
        identity.practiceSession==identity_.practiceSession;
}
bool LoadingRequestOwner::Publish(const EfzTmIdentityV1& identity){
    std::lock_guard<std::mutex> lock(mutex_);
    owned_=false;consumed_=false;transferred_=false;
    if(!identity.providerIncarnation || !identity.practiceSession || serial_==UINT32_MAX)return false;
    identity_=identity;++serial_;owned_=true;return true;
}
uint32_t LoadingRequestOwner::Capture(const EfzTmIdentityV1& identity){
    std::lock_guard<std::mutex> lock(mutex_);
    return owned_ && !consumed_ && SameSession(identity)?serial_:0;
}
LoadingReturn LoadingRequestOwner::Consume(const EfzTmIdentityV1& identity,uint32_t ticket,
                                          uint32_t nativeResult,uint32_t acceptedResult,void (*apply)(LoadingReturn,const EfzTmIdentityV1&)){
    std::lock_guard<std::mutex> lock(mutex_);
    if(!owned_ || consumed_ || !ticket || ticket!=serial_ || !SameSession(identity))return LoadingReturn::Unowned;
    if(nativeResult!=3)return LoadingReturn::Waiting;
    consumed_=true;transferred_=acceptedResult==3;
    const auto result=acceptedResult==3?LoadingReturn::BattleTransferred:LoadingReturn::RedirectedTransfer;
    if(apply)apply(result,identity);
    return result;
}
void LoadingRequestOwner::Clear(){
    std::lock_guard<std::mutex> lock(mutex_);owned_=false;consumed_=false;transferred_=false;identity_={};
}
}

namespace CharacterHotswap { uint32_t LoadingRequestOwner::CaptureInitialization(const EfzTmIdentityV1& identity) {
    std::lock_guard<std::mutex> lock(mutex_);
    return owned_ && consumed_ && transferred_ && SameSession(identity)?serial_:0;
} }
