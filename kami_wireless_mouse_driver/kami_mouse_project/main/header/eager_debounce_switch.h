/**************** Side Mouse Buttons ****************/

#pragma once

#include "header/switch.h"
#include "header/common.h"

// This needs to be sufficiently long to debounce the buttons and for the report to be sent.
// But it also needs to be short enough to not cause the mouse to lag.
// These butttons are not high preformance, so 10ms should be sufficient.
#define STABLE_POLL_TIME_MS 10

// Pre declarations
// Non static functions visible outside file
void button_debounce_init(void);
void button_debounce_task(void *arg);