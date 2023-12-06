#pragma once

#include "header/common.h"

#include "tinyusb.h"

/**************** HID Defs ****************/

#define CONFIG_IDF_TARGET_ESP32S3 1
#define CONFIG_FREERTOS_HZ 1000

#define MAX_POWER_MA (100)
#define HID_EP_IN_ADDR (0x81)
#define HID_EP_IN_SIZE (16)
#define HID_EP_IN_INTERVAL (10)

/*
// Enum for usb vs wifi reports
typedef enum {
	USB_REPORT,
	WIFI_REPORT,
} report_type_t;
*/

#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)

// Pre declarations
// Non static functions visible outside file
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance);
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen);
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize);
void app_main(void);