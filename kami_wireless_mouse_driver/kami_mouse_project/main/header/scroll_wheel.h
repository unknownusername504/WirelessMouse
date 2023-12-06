/**************** Scroll Wheel ****************/

#pragma once

#include "header/common.h"

// Enum for scroll wheel direction.
typedef enum
{
	SCROLL_WHEEL_NONE,
	SCROLL_WHEEL_UP,
	SCROLL_WHEEL_DOWN,
} scroll_wheel_dir_t;

// Enum for active level of the rotary encoder.
typedef enum
{
	SWHEEL_A_LOW,
	SWHEEL_B_LOW,
	SWHEEL_A_HIGH,
	SWHEEL_B_HIGH,
} swheel_active_t;

// Multiply the reported scroll wheel movement depending on the scroll wheel speed.
#define SCROLL_WHEEL_SPEED_MIN 1
#define SCROLL_WHEEL_SPEED_MAX 10
#define SCROLL_WHEEL_PAUSE_MS 100

// Pre declarations
// Non static functions visible outside file
void swheel_init(void);
void swheel_task(void *arg);