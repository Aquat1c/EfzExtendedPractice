#pragma once
#include <windows.h>
#include <string>
struct AudioTuple { int bgm,se; unsigned owned; };
UINT WINAPI InterceptIniRead(LPCSTR,LPCSTR,INT,LPCSTR);
BOOL WINAPI InterceptIniWrite(LPCSTR,LPCSTR,LPCSTR,LPCSTR);
void SetupTrainingFile(const std::string&);
bool TrainingRefresh();
bool TrainingImport();
bool TrainingWrite(int,int);
AudioTuple TrainingTuple();
unsigned TrainingMirrorCalls();
void SetupCompanionFile(const std::string&);
bool CompanionControls();
bool CompanionWrite(int,int);
void CompanionImport();
AudioTuple CompanionTuple();

void PauseAfterActivation();
void CompanionActivate();
unsigned CompanionQueryOwned();

bool TrainingLaneWrite(bool,int);
void TrainingConfigLane(bool,int);
void TrainingConfigLoad(int,int);
AudioTuple TrainingMirror();
void CompanionSchemaLoad();

bool TrainingMapNormalized();
unsigned CompanionLoadAvailable();
void CompanionStartupSettings();
