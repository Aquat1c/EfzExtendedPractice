#pragma once
#include "hook_registry.h"
#ifndef EFZ_TM_LIFECYCLE_ABI
#include "practice_contract.h"
#endif
namespace Practice {
void RegisterExistingPracticeProvider();
class WorkLease;
enum class ResetReason : uint32_t;
using MeasurementResetConsumer=void (*)(const EfzTmIdentityV1&,ResetReason);
void BindMeasurementResetConsumer(MeasurementResetConsumer);
using CaptureInputBaseline=bool (*)(const EfzTmEntryV1&);
using RetireInputBaseline=uint32_t (*)(const EfzTmEntryV1&,bool);
using CancelInputWork=void (*)();
void BindInputRetirementConsumer(CaptureInputBaseline,RetireInputBaseline,CancelInputWork);
bool PrepareBattleInputs(uint32_t battle,uint32_t gameSystem,uint32_t player1,uint32_t player2);
WorkLease TryEnterMonitorWork() noexcept;
bool MonitorLifecycleOwned() noexcept;
bool RecordBattleCleanupBranch(uint32_t battle,uint32_t branch) noexcept;
bool TakeBattleCleanupResume(uint32_t battle,uint32_t gameSystem,uint32_t branch) noexcept;
bool RecordBattleCleanupReturned(uint32_t battle,uint32_t gameSystem,uint32_t acceptedScreen) noexcept;
bool LegacyMonitorStartupRequired() noexcept;
using CaptureLoadingRequest=uint32_t (*)(const EfzTmIdentityV1&);
using ConsumeLoadingRequest=void (*)(const EfzTmIdentityV1&,uint32_t,uint32_t,uint32_t);
void BindLoadingRequestConsumer(CaptureLoadingRequest capture,ConsumeLoadingRequest consume);
bool CaptureCurrentPracticeIdentity(EfzTmIdentityV1& identity);
void BindRuntimeResources(PracticeHooks::Registry& registry,PatchLedger& ledger);
}

extern "C" EFZ_TM_RUNTIME_API uint32_t __cdecl EFZ_TM_BeginLoadingV1(const EfzTmEntryV1*,uint32_t,uint32_t*);
extern "C" EFZ_TM_RUNTIME_API uint32_t __cdecl EFZ_TM_EndLoadingV1(const EfzTmEntryV1*,uint32_t,uint32_t,uint32_t,uint32_t);



