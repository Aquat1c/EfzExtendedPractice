#include "game/character_hotswap_frontend.h"
#include <cstdio>
using namespace CharacterHotswap;
namespace {int applied=0;void Apply(LoadingReturn result,const EfzTmIdentityV1&){if(result==LoadingReturn::BattleTransferred)++applied;}}
int main(){
    int failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);++failures;}}while(false)
    LoadingRequestOwner request;EfzTmIdentityV1 oldWorld{7,11,15,1},frontend{7,11,0,0};
    CHECK(request.Publish(oldWorld));const auto first=request.Capture(frontend);CHECK(first!=0);
    CHECK(request.Consume(frontend,first,2,3)==LoadingReturn::Waiting);
    CHECK(request.Consume(frontend,first,3,3,Apply)==LoadingReturn::BattleTransferred);
    CHECK(request.Consume(frontend,first,3,3,Apply)==LoadingReturn::Unowned);
    CHECK(applied==1);
    CHECK(request.Capture(frontend)==0);
    CHECK(request.CaptureInitialization(frontend)==first);
    CHECK(request.Publish(frontend));const auto second=request.Capture(frontend);CHECK(second!=first&&second!=0);
    CHECK(request.Consume(frontend,first,3,3,Apply)==LoadingReturn::Unowned);
    auto stale=frontend;stale.practiceSession=10;CHECK(request.Capture(stale)==0);
    CHECK(request.Consume(stale,second,3,3)==LoadingReturn::Unowned);
    CHECK(request.Consume(frontend,second,3,1)==LoadingReturn::RedirectedTransfer);
    CHECK(request.Consume(frontend,second,3,3)==LoadingReturn::Unowned);
    CHECK(request.CaptureInitialization(frontend)==0);
    CHECK(request.Publish(frontend));const auto canceled=request.Capture(frontend);request.Clear();
    CHECK(request.Consume(frontend,canceled,3,3)==LoadingReturn::Unowned);
    CHECK(!request.Publish({}));CHECK(request.Capture(frontend)==0);
    return failures?1:0;
}
