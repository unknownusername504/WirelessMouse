/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "kami_mouse.h"

// Source includes are a dangerous form of modularity but best option with compiler.
#include "source/latch_switch.c"
#include "source/eager_debounce_switch.c"
#include "source/scroll_wheel.c"
#include "source/motion_sensor.c"

/************* TinyUSB descriptors ****************/

/**
 * @brief HID report descriptor
 *
 * In this example we implement Keyboard + Mouse HID device,
 * so we must define both report descriptors
 */
static const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(HID_ITF_PROTOCOL_MOUSE))};

/**
 * @brief String descriptor
 */
static const char *hid_string_descriptor[5] = {
    // array of pointer to string descriptors
    (char[]){0x09, 0x04}, // 0: is supported language is English (0x0409)
    "Kami",               // 1: Manufacturer
    "Komplex Mouse",      // 2: Product
    "123456",             // 3: Serials, should use chip ID
    "HID interface",      // 4: HID
};

/**
 * @brief Configuration descriptor
 *
 * This is a simple configuration descriptor that defines 1 configuration and 1 HID interface
 */
static const uint8_t hid_configuration_descriptor[] = {
    // Configuration number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, MAX_POWER_MA),

    // Interface number, string index, boot protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_descriptor), HID_EP_IN_ADDR, HID_EP_IN_SIZE, HID_EP_IN_INTERVAL),
};

// Mouse Protocol 1, HID 1.11 spec, Appendix B, page 59-60, with wheel extension
/*
static const uint8_t mouse_hid_report_desc[] =
{
    0x05, 0x01,		// Usage Page (Generic Desktop)
    0x09, 0x02,		// Usage (Mouse)
    0xA1, 0x01,		// Collection (Application)
    0x05, 0x09,		//   Usage Page (Button)
    0x19, 0x01,		//   Usage Minimum (Button #1)
    0x29, 0x03,		//   Usage Maximum (Button #3)
    0x15, 0x00,		//   Logical Minimum (0)
    0x25, 0x01,		//   Logical Maximum (1)
    0x95, 0x03,		//   Report Count (3)
    0x75, 0x01,		//   Report Size (1)
    0x81, 0x02,		//   Input (Data, Variable, Absolute)
    0x95, 0x01,		//   Report Count (1)
    0x75, 0x05,		//   Report Size (5)
    0x81, 0x03,		//   Input (Constant) // Byte 1
    0x05, 0x01,		//   Usage Page (Generic Desktop)
    0x09, 0x30,		//   Usage (X)
    0x09, 0x31,		//   Usage (Y)
    0x16, 0x01, 0x80,	//   Logical Minimum (-32,767)
    0x26, 0xFF, 0x7F,	//   Logical Maximum (32,767)
    0x36, 0x01, 0x80,	//   Physical Minimum (-32,767)
    0x46, 0xFF, 0x7F,	//   Physical Maxiumum (32,767)
    0x75, 0x10,		//   Report Size (16),
    0x95, 0x02,		//   Report Count (2),
    0x81, 0x06,		//   Input (Data, Variable, Relative) // Byte 3, 5
    0x09, 0x38,		//   Usage (Wheel)
    0x15, 0x81,		//   Logical Minimum (-127)
    0x25, 0x7F,		//   Logical Maximum (127)
    0x35, 0x81,		//   Phyiscal Minimum (-127)
    0x45, 0x7F,		//   Physical Maxiumum (127)
    0x75, 0x08,		//   Report Size (8)
    0x95, 0x01,		//   Report Count (1)
    0x81, 0x06,		//   Input (Data, Variable, Relative) // Byte 6
    0xC0			// End Collection
};
*/

/********* TinyUSB HID callbacks ***************/

// Invoked when received GET HID REPORT DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    // We use only one interface and one HID report descriptor, so we can ignore parameter 'instance'
    return hid_report_descriptor;
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;

    return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{
}

/************* IO Configs ****************/

/*
// TODO: Update the mode based on if the usb is connected once wifi is implemented.
static bool wifi_report = USB_REPORT;
*/

/********* Application ***************/

void app_main(void)
{
    // Initialize the USB stack.
    ESP_LOGI(TAG, "USB initialization");
    const tinyusb_config_t tusb_cfg =
        {
            .device_descriptor = NULL,
            .string_descriptor = hid_string_descriptor,
            .string_descriptor_count = sizeof(hid_string_descriptor) / sizeof(hid_string_descriptor[0]),
            .external_phy = false,
            .configuration_descriptor = hid_configuration_descriptor,
        };

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(TAG, "USB initialization DONE");

    if (!tud_mounted())
    {
        ESP_LOGI(TAG, "USB not mounted");
        return;
    }

    // Initialize the software latches for the mouse buttons.
    mb_latch_init();
    // Initialize the software debouncing for the mouse wheel button and side buttons.
    button_debounce_init();
    // Initialize the rotary encoder for the scroll wheel.
    swheel_init();
    // Initialize the IO pins for the sensor.
    sensor_init();

    // Create the tasks for the software latches for the mouse buttons.
    xTaskCreate(mb_latch_task, "mb_latch_task", 2048, NULL, 1, NULL);
    // Create the tasks for the software debouncing for the mouse wheel button and side buttons.
    xTaskCreate(button_debounce_task, "button_debounce_task", 2048, NULL, 1, NULL);
    // Create the tasks for the scroll wheel.
    xTaskCreate(swheel_task, "swheel_task", 2048, NULL, 1, NULL);
    // Create the tasks for the Pixart PAW3395 sensor.
    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 1, NULL);

    // Main loop
    while (1)
    {
    }
}
