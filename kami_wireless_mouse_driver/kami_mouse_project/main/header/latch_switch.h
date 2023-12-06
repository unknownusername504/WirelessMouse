/**************** Main Mouse Buttons ****************/

#pragma once

#include "header/switch.h"
#include "header/common.h"

// Enum for a handshake between the ISR and the task.
typedef enum
{
	LATCH_EVENT_CLEAR,
	LATCH_EVENT_SET,
	LATCH_EVENT_READ,
} latch_event_t;

// Pre declarations
// Non static functions visible outside file
void mb_latch_init(void);
void mb_latch_task(void *arg);