#include "game/battle_frontend_result.h"
#include <cstdio>
using namespace GameFrontend;
int main(){
    int failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);++failures;}}while(false)
    BattleExitRouting routing;routing.Arm(ExitRoute::Loading);
    for(int i=0;i<500;++i)CHECK(routing.Consume(3,true).screen==3);
    auto result=routing.Consume(1,false);CHECK(result.screen==2&&!result.silenceTitle);
    CHECK(routing.Consume(1,false).screen==1);
    routing.Arm(ExitRoute::Title);result=routing.Consume(1,false);
    CHECK(result.screen==0&&result.silenceTitle);
    result=routing.Consume(1,false);CHECK(result.screen==1&&!result.silenceTitle);
    routing.Arm(ExitRoute::Loading);CHECK(routing.Consume(8,false).screen==8);
    CHECK(routing.Consume(1,false).screen==1);
    routing.Arm(ExitRoute::Loading);
    for(int i=0;i<121;++i)routing.Consume(3,false);
    CHECK(routing.Consume(1,false).screen==1);
    alignas(4) unsigned char battle[1420]{};
    auto pause=reinterpret_cast<uint32_t*>(battle+1416);*pause=1;
    const auto captured=reinterpret_cast<uintptr_t>(battle);
    CHECK(!routing.RequestCaptured(ExitRoute::Loading,captured,captured+4,3));
    CHECK(!routing.RequestCaptured(ExitRoute::Loading,captured,captured,2));
    CHECK(battle[45]==0&&*pause==1);
    CHECK(routing.RequestCaptured(ExitRoute::Loading,captured,captured,3));
    CHECK(battle[45]==1&&*pause==0&&routing.Consume(1,false).screen==2);
    return failures?1:0;
}
