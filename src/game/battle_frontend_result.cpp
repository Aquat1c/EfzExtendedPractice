#include "game/battle_frontend_result.h"
namespace GameFrontend {
bool BattleExitRouting::RequestCaptured(ExitRoute route,uintptr_t capturedBattle,uintptr_t liveBattle,uint8_t liveScreen){
    if(!capturedBattle || liveBattle!=capturedBattle || liveScreen!=3)return false;
    Arm(route);
    *reinterpret_cast<volatile uint32_t*>(capturedBattle+1416)=0;
    *reinterpret_cast<volatile uint8_t*>(capturedBattle+45)=1;
    return true;
}
void BattleExitRouting::Arm(ExitRoute route){
    remaining_.store(route==ExitRoute::None?0:120,std::memory_order_relaxed);
    route_.store(route,std::memory_order_release);
}
RoutedBattleResult BattleExitRouting::Consume(uint8_t result,bool cleanupHeld){
    // AL=3 from a retained destructive gate is not another native fade visit.
    if(cleanupHeld)return {result,false};
    const auto route=route_.load(std::memory_order_acquire);
    if(route==ExitRoute::None)return {result,false};
    if(result==3){
        const auto remaining=remaining_.load(std::memory_order_relaxed);
        if(remaining>0)remaining_.store(remaining-1,std::memory_order_relaxed);
        else route_.store(ExitRoute::None,std::memory_order_release);
        return {result,false};
    }
    Arm(ExitRoute::None);
    if(result!=1)return {result,false};
    return {static_cast<uint8_t>(route==ExitRoute::Loading?2:0),route==ExitRoute::Title};
}
}
