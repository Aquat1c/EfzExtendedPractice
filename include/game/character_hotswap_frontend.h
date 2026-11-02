#pragma once
#include "runtime/practice_contract.h"
#include <mutex>
namespace CharacterHotswap {
enum class LoadingReturn { Unowned, Waiting, BattleTransferred, RedirectedTransfer };
class LoadingRequestOwner {
public:
    bool Publish(const EfzTmIdentityV1& identity);
    uint32_t Capture(const EfzTmIdentityV1& identity);
    uint32_t CaptureInitialization(const EfzTmIdentityV1& identity);
    LoadingReturn Consume(const EfzTmIdentityV1& identity,uint32_t ticket,
                          uint32_t nativeResult,uint32_t acceptedResult,
                          void (*apply)(LoadingReturn,const EfzTmIdentityV1&)=nullptr);
    void Clear();
private:
    bool SameSession(const EfzTmIdentityV1& identity) const;
    std::mutex mutex_;
    EfzTmIdentityV1 identity_{};
    uint32_t serial_=0;
    bool owned_=false,consumed_=false,transferred_=false;
};
}
