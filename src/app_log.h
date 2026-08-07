#ifndef WILI8JAM_APP_LOG_H
#define WILI8JAM_APP_LOG_H

#ifdef PICO_ON_DEVICE
#include "platform/diag.h"
#define APP_LOG(...) DIAG(__VA_ARGS__)
#else
#include <stdio.h>
#define APP_LOG(...) printf(__VA_ARGS__)
#endif

#endif
