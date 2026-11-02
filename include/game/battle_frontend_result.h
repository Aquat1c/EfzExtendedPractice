#pragma once
#include <atomic>
#include <stdint.h>
namespace GameFrontend {
enum class ExitRoute : uint32_t { None, Title, Loading };
struct RoutedBattleResult {uint8_t screen;bool silenceTitle;};
class BattleExitRouting {
public:
    void Arm(ExitRoute route);
    bool RequestCaptured(ExitRoute route,uintptr_t capturedBattle,uintptr_t liveBattle,uint8_t liveScreen);
    RoutedBattleResult Consume(uint8_t nativeResult,bool cleanupHeld);
private:
    std::atomic<ExitRoute> route_{ExitRoute::None};
    std::atomic<int> remaining_{0};
};
}
