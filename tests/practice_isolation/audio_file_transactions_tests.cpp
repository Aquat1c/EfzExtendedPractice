#include "audio_transaction_test_support.h"
#include "utils/audio_file_control.h"
#include <thread>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <cstring>
namespace {
int failures=0;
int failWriteAt=0,writeAttempts=0;
bool writeFailed=false;
void Check(bool ok,const char* label){if(!ok){++failures;std::printf("FAIL %s\n",label);}}
HANDLE interleaveStart,interleaveDone;
std::atomic<DWORD> victimThread{0};
std::atomic<bool> readInterleave{false},writeInterleave{false};
void PauseForCompetingOperation() {SetEvent(interleaveStart); WaitForSingleObject(interleaveDone,100);}
void ResetFile(const std::string& path) {
 WritePrivateProfileStringA("Shared","enabled","1",path.c_str());
 WritePrivateProfileStringA("Shared","audio","1",path.c_str());
 WritePrivateProfileStringA("Audio","writeInProgress","0",path.c_str());
 WritePrivateProfileStringA("Audio","bgmVolumePercent","20",path.c_str());
 WritePrivateProfileStringA("Audio","seVolumePercent","80",path.c_str());
 SetupTrainingFile(path);SetupCompanionFile(path);
 ResetEvent(interleaveStart);ResetEvent(interleaveDone);victimThread=GetCurrentThreadId();
}
}
UINT WINAPI InterceptIniRead(LPCSTR section,LPCSTR key,INT fallback,LPCSTR path) {
 const auto result=GetPrivateProfileIntA(section,key,fallback,path);
 if(GetCurrentThreadId()==victimThread && !strcmp(section,"Audio") && !strcmp(key,"bgmVolumePercent") && readInterleave.exchange(false)) PauseForCompetingOperation();
 return result;
}
BOOL WINAPI InterceptIniWrite(LPCSTR section,LPCSTR key,LPCSTR value,LPCSTR path) {
 ++writeAttempts;
 if(failWriteAt && writeAttempts==failWriteAt) {writeFailed=true;SetLastError(ERROR_WRITE_FAULT);return FALSE;}
 const auto result=WritePrivateProfileStringA(section,key,value,path);
 if(GetCurrentThreadId()==victimThread && !strcmp(section,"Audio") && !strcmp(key,"bgmVolumePercent") && writeInterleave.exchange(false)) PauseForCompetingOperation();
 return result;
}
void PauseAfterActivation() { PauseForCompetingOperation(); }
int main() {
 const auto path=(std::filesystem::current_path()/"f04-transaction-test.ini").string();
 interleaveStart=CreateEventA(nullptr,TRUE,FALSE,nullptr);interleaveDone=CreateEventA(nullptr,TRUE,FALSE,nullptr);
 ResetFile(path);Check(CompanionControls(),"actual controls write succeeds");
 Check(GetPrivateProfileIntA("Audio","writeInProgress",0,path.c_str())==0,"controls/schema do not strand audio marker");
 CompanionSchemaLoad();
 Check(GetPrivateProfileIntA("Audio","writeInProgress",0,path.c_str())==0,"initialization/load do not strand marker");
 for(int reader=0;reader<3;++reader) {
  const bool companionReader=reader==1;
  ResetFile(path);readInterleave=true;
  std::thread writer([&]{WaitForSingleObject(interleaveStart,INFINITE);CompanionWrite(70,30);SetEvent(interleaveDone);});
  if(companionReader)CompanionImport();else if(reader==2)TrainingImport();else TrainingRefresh();
  auto got=companionReader?CompanionTuple():TrainingTuple();
  Check((got.bgm==20 && got.se==80)||(got.bgm==70 && got.se==30),companionReader?"actual companion import excludes completed intervening writer":"actual training import excludes completed intervening writer");
  if(reader==2) {auto mirror=TrainingMirror(); Check(mirror.bgm==got.bgm&&mirror.se==got.se,"import mirror stays in accepted file transaction");}
  writer.join();
 }
 ResetFile(path);writeInterleave=true;
 std::thread competitor([&]{WaitForSingleObject(interleaveStart,INFINITE);CompanionWrite(70,30);SetEvent(interleaveDone);});
 TrainingWrite(40,60);competitor.join();
 const int b=GetPrivateProfileIntA("Audio","bgmVolumePercent",0,path.c_str()),s=GetPrivateProfileIntA("Audio","seVolumePercent",0,path.c_str());
 Check((b==40&&s==60)||(b==70&&s==30),"actual two module writers cannot overlap transactions");
 ResetFile(path);TrainingRefresh();TrainingWrite(70,30);
 WritePrivateProfileStringA("Audio","writeInProgress","1",path.c_str());
 Check(!TrainingImport(),"failed import is not replayed from cached status");
 Check(TrainingMirrorCalls()==0,"import does not use intermediate publishing setters");
 ResetFile(path); TrainingRefresh();
 Check(TrainingImport() && TrainingMirrorCalls()==0,"successful import mirrors without intermediate setters");
 auto mirrored=TrainingMirror(); Check(mirrored.bgm==20 && mirrored.se==80,"actual Config mirror receives accepted tuple");
 TrainingLaneWrite(true,45);
 Check(GetPrivateProfileIntA("Audio","bgmVolumePercent",0,path.c_str())==45 && GetPrivateProfileIntA("Audio","seVolumePercent",0,path.c_str())==80,"shipping UI lane helper persists complete snapshot");
 TrainingConfigLane(false,35); auto configured=TrainingTuple();
 Check(configured.bgm==45&&configured.se==35,"shipping Config lane helper updates one coherent pair");
 TrainingConfigLoad(10,90); configured=TrainingTuple();
 Check(configured.bgm==10&&configured.se==90,"shipping Config load helper publishes complete pair");
 for(int operation=0;operation<3;++operation) {
  HANDLE started=CreateEventA(nullptr,TRUE,FALSE,nullptr),completed=CreateEventA(nullptr,TRUE,FALSE,nullptr);
  std::thread producer;
  {
   EfzAudioFileTransaction held;
   Check(bool(held),"test holds real process-shared file transaction");
   producer=std::thread([&]{SetEvent(started);if(operation==0)TrainingConfigLane(true,55);else if(operation==1)TrainingConfigLoad(15,85);else TrainingLaneWrite(false,65);SetEvent(completed);});
   WaitForSingleObject(started,INFINITE);
   Check(WaitForSingleObject(completed,100)==WAIT_TIMEOUT,"Config SetSetting/load and UI publication serialize with file writers");
  }
  producer.join();CloseHandle(started);CloseHandle(completed);
 }
 WritePrivateProfileStringA("Audio","bgmVolumePercent","25",path.c_str());
 TrainingRefresh(); CompanionImport();
 Check(TrainingTuple().bgm==25&&CompanionTuple().bgm==25,"ordinary legacy INI edit remains effective");
 Check(TrainingMapNormalized(),"Config audio mirrors use normalized map section and keys");
 ResetFile(path);TrainingRefresh();CompanionImport();
 WritePrivateProfileStringA("Audio","writeInProgress","1",path.c_str());
 WritePrivateProfileStringA("Audio","bgmVolumePercent","70",path.c_str());
 CompanionControls();CompanionSchemaLoad();
 Check(GetPrivateProfileIntA("Audio","writeInProgress",0,path.c_str())==1,"schema and controls preserve unfinished audio marker");
 Check(CompanionLoadAvailable()==0,"LoadAudioSettings does not expose unfinished pair");
 CompanionStartupSettings();
 Check(GetPrivateProfileIntA("Audio","writeInProgress",0,path.c_str())==1,"startup never republishes unavailable defaults as repair");
 TrainingRefresh();CompanionImport();
 Check(TrainingTuple().bgm==20&&TrainingTuple().se==80&&CompanionTuple().bgm==20&&CompanionTuple().se==80,"both importers retain last accepted tuple after interrupted write and schema");
 Check(CompanionWrite(70,30),"explicit complete pair repairs unfinished write");TrainingRefresh();CompanionImport();
 Check(TrainingTuple().bgm==70&&TrainingTuple().se==30&&CompanionTuple().bgm==70&&CompanionTuple().se==30,"complete repair admits both lanes");
 for(bool companion:{false,true}) {
  ResetFile(path);writeAttempts=0;
  if(companion)CompanionWrite(70,30);else TrainingWrite(70,30);
  const int totalWrites=writeAttempts;
  std::printf("%s writer: %d injected write positions\n",companion?"companion":"training",totalWrites);
  for(int fail=1;fail<=totalWrites;++fail) {
   ResetFile(path);TrainingRefresh();CompanionImport();writeAttempts=0;writeFailed=false;failWriteAt=fail;
   const bool ok=companion?CompanionWrite(70,30):TrainingWrite(70,30);
   failWriteAt=0;
   Check(!ok&&writeFailed,"actual writer reports each injected write failure");
   if(fail==1) Check(writeAttempts==1,"marker-start failure prevents subsequent writes");
   else Check(GetPrivateProfileIntA("Audio","writeInProgress",0,path.c_str())==1,"failed payload/revision/commit retains unfinished marker");
   CompanionControls();CompanionSchemaLoad();TrainingRefresh();CompanionImport();
   Check(TrainingTuple().bgm==20&&TrainingTuple().se==80&&CompanionTuple().bgm==20&&CompanionTuple().se==80,"failed write never publishes or imports partial tuple");
   Check(companion?CompanionWrite(70,30):TrainingWrite(70,30),"writer repairs with explicit successful pair");
   TrainingRefresh();CompanionImport();
   Check(TrainingTuple().bgm==70&&TrainingTuple().se==30&&CompanionTuple().bgm==70&&CompanionTuple().se==30,"successful pair clears failed transaction");
  }
 }
 ResetFile(path);TrainingRefresh();CompanionImport();
 char mutexName[80]{};
 _snprintf_s(mutexName,sizeof(mutexName),_TRUNCATE,"Local\\EfzAudioConfigV1-%lu",GetCurrentProcessId());
 HANDLE abandoned=CreateMutexA(nullptr,FALSE,mutexName);
 std::thread interrupted([&]{
  WaitForSingleObject(abandoned,INFINITE);
  WritePrivateProfileStringA("Audio","writeInProgress","1",path.c_str());
  WritePrivateProfileStringA("Audio","bgmVolumePercent","70",path.c_str());
  // Intentionally exit owning the real process-named mutex.
 });
 interrupted.join();
 Check(!CompanionControls(),"first abandoned acquisition is declined");
 CompanionControls();CompanionSchemaLoad();TrainingRefresh();CompanionImport();
 Check(GetPrivateProfileIntA("Audio","writeInProgress",0,path.c_str())==1&&TrainingTuple().bgm==20&&CompanionTuple().bgm==20,"later acquisitions do not treat abandonment as completed recovery");
 Check(TrainingWrite(70,30),"complete pair repairs after mutex abandonment");CompanionImport();
 Check(TrainingTuple().bgm==70&&TrainingTuple().se==30&&CompanionTuple().bgm==70&&CompanionTuple().se==30,"abandoned transaction repaired by full pair only");
 CloseHandle(abandoned);
 ResetEvent(interleaveStart);ResetEvent(interleaveDone);
 std::atomic<bool> ownerMatches{false};
 std::thread registration([&]{WaitForSingleObject(interleaveStart,INFINITE);auto owned=CompanionQueryOwned();ownerMatches=(owned==CompanionTuple().owned);SetEvent(interleaveDone);});
 CompanionActivate();registration.join();
 Check(ownerMatches,"actual activation publishes callback owner before acknowledgement");
 CloseHandle(interleaveStart);CloseHandle(interleaveDone);DeleteFileA(path.c_str());
 std::printf("audio file transactions: %d failures\n",failures);return failures?1:0;
}
