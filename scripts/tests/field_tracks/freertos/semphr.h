#pragma once
#include "FreeRTOS.h"
using SemaphoreHandle_t=bool*;
inline SemaphoreHandle_t xSemaphoreCreateMutex(){return new bool(false);}
inline int xSemaphoreTake(SemaphoreHandle_t lock,TickType_t){if(*lock)return 0;*lock=true;return 1;}
inline void xSemaphoreGive(SemaphoreHandle_t lock){*lock=false;}
