#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "class/hid/hid_device.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "KamiKomplexMouse";

// IDE doesn't like std libraries, so we need to define the types here.
#define bool _Bool
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef long int32_t;
typedef unsigned long uint32_t;

#define max(a,b) (((a) > (b)) ? (a) : (b))
#define min(a,b) (((a) < (b)) ? (a) : (b))

// Function to convert US to ticks
#define pdUS_TO_TICKS(xTimeInUs) ((TickType_t)((TickType_t)(pdMS_TO_TICKS(1) * 1000) * xTimeInUs))

// Function to convert NS to ticks
#define pdNS_TO_TICKS(xTimeInNs) ((TickType_t)((TickType_t)(pdMS_TO_TICKS(1) * 1000000) * xTimeInNs))