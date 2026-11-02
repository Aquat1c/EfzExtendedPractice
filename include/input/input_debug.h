#pragma once
#include <windows.h>
#include <string>
#include "input_core.h"
#include "input_buffer.h"

void DiagnoseInputSystem(int playerNum);
void DumpInputBuffer(int playerNum);
std::string GetInputBufferVisualization(int playerNum, int window);
void LogNextBufferValue(int playerNum);
void LogButtonPress(const char* buttonName, uintptr_t address, uint8_t value, const char* result);

bool HoldButtonA(int playerNum);
bool HoldButtonB(int playerNum);
bool HoldButtonC(int playerNum);
bool HoldButtonD(int playerNum);