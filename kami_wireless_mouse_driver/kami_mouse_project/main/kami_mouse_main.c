/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"
#include "driver/gpio.h"

#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "hal/spi_types.h"
#include "esp_timer.h"
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

// TODO: Update the mode based on if the usb is connected once wifi is implemented.
static bool wifi_report = USB_REPORT;
*/

#define APP_BUTTON (GPIO_NUM_0) // Use BOOT signal by default
static const char *TAG = "KamiKomplexMouse";

// IDE doesn't like stdint.h, so we need to define the types here.
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef long unsigned int  uint32_t;
typedef unsigned long long uint64_t;

/************* TinyUSB descriptors ****************/

#define TUSB_DESC_TOTAL_LEN      (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)

/**
 * @brief HID report descriptor
 *
 * In this example we implement Keyboard + Mouse HID device,
 * so we must define both report descriptors
 */
const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(HID_ITF_PROTOCOL_MOUSE) )
};

/**
 * @brief String descriptor
 */
const char* hid_string_descriptor[5] = {
    // array of pointer to string descriptors
    (char[]){0x09, 0x04},    // 0: is supported language is English (0x0409)
    "Kami",                     // 1: Manufacturer
    "Komplex Mouse",            // 2: Product
    "123456",                   // 3: Serials, should use chip ID
    "HID interface",            // 4: HID
};

// Mouse Protocol 1, HID 1.11 spec, Appendix B, page 59-60, with wheel extension
/*
static const uint8_t mouse_hid_report_desc[] = {
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
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
  (void) instance;
  (void) report_id;
  (void) report_type;
  (void) buffer;
  (void) reqlen;

  return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
{
}

// GPIO_NUM_4 (LMB_NO) and GPIO_NUM_5 (LMB_NC) are used for a software latch of the left mouse button.
// Likewise, GPIO_NUM_6 (RMB_NO) and GPIO_NUM_7 (RMB_NC) are used for a software latch of the right mouse button.
// The software latches are used to prevent the mouse buttons from bouncing.
static const gpio_config_t lmb_config = {
    .pin_bit_mask = BIT64(GPIO_NUM_4) | BIT64(GPIO_NUM_5),
    .mode = GPIO_MODE_INPUT,
    .intr_type = GPIO_INTR_NEGEDGE,
    .pull_up_en = true, // Not required but allows for exlcuding (no short) the external pull-up resistor.
    .pull_down_en = false,
};

static const gpio_config_t rmb_config = {
    .pin_bit_mask = BIT64(GPIO_NUM_6) | BIT64(GPIO_NUM_7),
    .mode = GPIO_MODE_INPUT,
    .intr_type = GPIO_INTR_NEGEDGE,
    .pull_up_en = true, // Not required but allows for exlcuding (no short) the external pull-up resistor.
    .pull_down_en = false,
};

// Initialize the software latches for the mouse buttons.
static void mb_latch_init(void)
{
    ESP_ERROR_CHECK(gpio_config(&lmb_config));
    ESP_ERROR_CHECK(gpio_config(&rmb_config));
    ESP_LOGI(TAG, "USB mb_latch_init");
}

// Enum for a handshake between the ISR and the task.
typedef enum {
    LATCH_EVENT_CLEAR,
    LATCH_EVENT_SET,
    LATCH_EVENT_READ,
} latch_event_t;

static latch_event_t lmb_latch_event = LATCH_EVENT_CLEAR;
static latch_event_t rmb_latch_event = LATCH_EVENT_CLEAR;

// Enum for direction as mouse_down or mouse_up.
typedef enum {
    MOUSE_BUTTON_DOWN,
    MOUSE_BUTTON_UP,
} mouse_button_dir_t;

static mouse_button_dir_t current_lmb_dir = MOUSE_BUTTON_UP;
static mouse_button_dir_t current_rmb_dir = MOUSE_BUTTON_UP;

// Get current mouse button state.
static mouse_button_dir_t calculate_lmb_dir(void)
{
    // Check if the mouse button is pressed or released.
    bool observed_lmb_no_state = gpio_get_level(GPIO_NUM_4);
    bool observed_lmb_nc_state = gpio_get_level(GPIO_NUM_5);
    // Check for a valid transition.
    if (observed_lmb_no_state == observed_lmb_nc_state) {
        return current_lmb_dir;
    }

    // Pins are active high, so the observed state should be 0 for pressed and 1 for released.
    // Change the mouse button state.
    if (observed_lmb_no_state == 1) {
        return MOUSE_BUTTON_DOWN;
    } else {
        return MOUSE_BUTTON_UP;
    }
}

static mouse_button_dir_t calculate_rmb_dir(void)
{
    // Check if the mouse button is pressed or released.
    bool observed_rmb_no_state = gpio_get_level(GPIO_NUM_6);
    bool observed_rmb_nc_state = gpio_get_level(GPIO_NUM_7);
    // Check for a valid transition.
    if (observed_rmb_no_state == observed_rmb_nc_state) {
        return current_lmb_dir;
    }

    // Pins are active high, so the observed state should be 0 for pressed and 1 for released.
    // Change the mouse button state.
    if (observed_rmb_no_state == 1) {
        return MOUSE_BUTTON_DOWN;
    } else {
        return MOUSE_BUTTON_UP;
    }
}

static void lmb_isr(void *arg)
{
    bool next_lmb_dir = calculate_lmb_dir();

    // Make sure the state is actually changing.
    if (next_lmb_dir == current_lmb_dir) {
        return;
    }

    // Set the latch event.
    lmb_latch_event = LATCH_EVENT_SET;
    current_lmb_dir = next_lmb_dir;
}

static void rmb_isr(void *arg)
{
    bool next_rmb_dir = calculate_rmb_dir();

    // Make sure the state is actually changing.
    if (next_rmb_dir == current_rmb_dir) {
        return;
    }

    // Set the latch event.
    rmb_latch_event = LATCH_EVENT_SET;
    current_rmb_dir = next_rmb_dir;
}

// Report the lmb button state.
static void lmb_latch_task_report(void)
{
    // Send a mouse report to the host when the mouse button is pressed or released.
    if (current_lmb_dir == MOUSE_BUTTON_DOWN) {
        ESP_LOGI(TAG, "LMB: DOWN");
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE, 0x01, 0, 0, 0, 0);
    } else {
        ESP_LOGI(TAG, "LMB: UP");
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE, 0x01, 0, 0, 0, 0);
    }
}

// Report the rmb button state.
static void rmb_latch_task_report(void)
{
    // Send a mouse report to the host when the mouse button is pressed or released.
    if (current_rmb_dir == MOUSE_BUTTON_DOWN) {
        ESP_LOGI(TAG, "RMB: DOWN");
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE, 0x02, 0, 0, 0, 0);
    } else {
        ESP_LOGI(TAG, "RMB: UP");
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE, 0x02, 0, 0, 0, 0);
    }
}

// Implement a software latch for the mouse buttons.
// The mouse button is latched when the mouse button is pressed.
// The mouse button is unlatched when the mouse button is released.
static void mb_latch_task(void *arg)
{
    gpio_install_isr_service(0);
    gpio_isr_handler_add(GPIO_NUM_4, lmb_isr, NULL);
    gpio_isr_handler_add(GPIO_NUM_5, lmb_isr, NULL);
    gpio_isr_handler_add(GPIO_NUM_6, rmb_isr, NULL);
    gpio_isr_handler_add(GPIO_NUM_7, rmb_isr, NULL);

    while (1) {
        if (lmb_latch_event == LATCH_EVENT_SET) {
            lmb_latch_event = LATCH_EVENT_READ;
            lmb_latch_task_report();
            // Only clear if the state is matching the current state.
            if (lmb_latch_event == LATCH_EVENT_READ) {
                lmb_latch_event = LATCH_EVENT_CLEAR;
            }
        }
        if (rmb_latch_event == LATCH_EVENT_SET) {
            rmb_latch_event = LATCH_EVENT_READ;
            rmb_latch_task_report();
            // Only clear if the state is matching the current state.
            if (rmb_latch_event == LATCH_EVENT_READ) {
                rmb_latch_event = LATCH_EVENT_CLEAR;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// There is also a mouse wheel button, and two side buttons.
// They will use only a single GPIO pin each, and will implement debouncing in software.
// The mouse wheel button is GPIO_NUM_10. The side buttons are GPIO_NUM_18 (SMB5) and GPIO_NUM_19 (SMB4).
static const gpio_config_t wheel_button_config = {
    .pin_bit_mask = BIT64(GPIO_NUM_10),
    .mode = GPIO_MODE_INPUT,
    .intr_type = GPIO_INTR_ANYEDGE,
    .pull_up_en = true, // Not required but allows for exlcuding (no short) the external pull-up resistor.
    .pull_down_en = false,
};

static const gpio_config_t side_button_config = {
    .pin_bit_mask = BIT64(GPIO_NUM_18) | BIT64(GPIO_NUM_19),
    .mode = GPIO_MODE_INPUT,
    .intr_type = GPIO_INTR_ANYEDGE,
    .pull_up_en = true, // Not required but allows for exlcuding (no short) the external pull-up resistor.
    .pull_down_en = false,
};

// Initialize the software debouncing for the mouse wheel button and side buttons.
static void button_debounce_init(void)
{
    ESP_ERROR_CHECK(gpio_config(&wheel_button_config));
    ESP_ERROR_CHECK(gpio_config(&side_button_config));
    ESP_LOGI(TAG, "USB button_debounce_init");
}

static bool mmb_event = false;
static bool smb4_event = false;
static bool smb5_event = false;

static mouse_button_dir_t mmb_dir = MOUSE_BUTTON_UP;
static mouse_button_dir_t smb4_dir = MOUSE_BUTTON_UP;
static mouse_button_dir_t smb5_dir = MOUSE_BUTTON_UP;

// This needs to be sufficiently long to debounce the buttons and for the report to be sent.
// But it also needs to be short enough to not cause the mouse to lag.
// These butttons are not high preformance, so 10ms should be sufficient.
#define STABLE_POLL_TIME_MS 10

static void mmb_isr(void *arg)
{
    // The button is debounced by waiting for it to be stable for STABLE_POLL_TIME_MS.
    for (int i = 0; i < STABLE_POLL_TIME_MS; i++) {
        bool observed_mmb_dir = gpio_get_level(GPIO_NUM_10);
        if (observed_mmb_dir == mmb_dir) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    mmb_dir = !mmb_dir;
    mmb_event = true;
}

static void smb4_isr(void *arg)
{
    // The button is debounced by waiting for it to be stable for STABLE_POLL_TIME_MS.
    for (int i = 0; i < STABLE_POLL_TIME_MS; i++) {
        bool observed_smb4_dir = gpio_get_level(GPIO_NUM_18);
        if (observed_smb4_dir == smb4_dir) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    smb4_dir = !smb4_dir;
    smb4_event = true;
}

static void smb5_isr(void *arg)
{
    // The button is debounced by waiting for it to be stable for STABLE_POLL_TIME_MS.
    for (int i = 0; i < STABLE_POLL_TIME_MS; i++) {
        bool observed_smb5_dir = gpio_get_level(GPIO_NUM_19);
        if (observed_smb5_dir == smb5_dir) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    smb5_dir = !smb5_dir;
    smb5_event = true;
}

// Report the mmb button state.
static void mmb_latch_task_report(void)
{
    // Send a mouse report to the host when the mouse button is pressed or released.
    if (mmb_event == MOUSE_BUTTON_DOWN) {
        ESP_LOGI(TAG, "MMB: DOWN");
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE, 0x01, 0, 0, 0, 0);
    } else {
        ESP_LOGI(TAG, "MMB: UP");
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE, 0x01, 0, 0, 0, 0);
    }
}

// Report the smb4 button state.
static void smb4_latch_task_report(void)
{
    // Send a mouse report to the host when the mouse button is pressed or released.
    if (smb4_event == MOUSE_BUTTON_DOWN) {
        ESP_LOGI(TAG, "SMB4: DOWN");
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE, 0x02, 0, 0, 0, 0);
    } else {
        ESP_LOGI(TAG, "SMB4: UP");
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE, 0x02, 0, 0, 0, 0);
    }
}

// Report the smb5 button state.
static void smb5_latch_task_report(void)
{
    // Send a mouse report to the host when the mouse button is pressed or released.
    if (smb5_event == MOUSE_BUTTON_DOWN) {
        ESP_LOGI(TAG, "SMB5: DOWN");
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE, 0x04, 0, 0, 0, 0);
    } else {
        ESP_LOGI(TAG, "SMB5: UP");
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE, 0x04, 0, 0, 0, 0);
    }
}

// Implement a software debounce for the mouse wheel button and side buttons.
// The mouse button is debounced when the mouse button is pressed.
// The mouse button is debounced when the mouse button is released.
static void button_debounce_task(void *arg)
{
    gpio_install_isr_service(0);
    gpio_isr_handler_add(GPIO_NUM_10, mmb_isr, NULL);
    gpio_isr_handler_add(GPIO_NUM_18, smb4_isr, NULL);
    gpio_isr_handler_add(GPIO_NUM_19, smb5_isr, NULL);

    while (1) {
        if (mmb_event) {
            mmb_event = false;
            mmb_latch_task_report();
        }
        if (smb4_event) {
            smb4_event = false;
            smb4_latch_task_report();
        }
        if (smb5_event) {
            smb5_event = false;
            smb5_latch_task_report();
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// Next is the scroll wheel which is implemented with a rotary encoder.
// The rotary encoder is GPIO_NUM_11 (SWHEEL_A) and GPIO_NUM_12 (SWHEEL_B).
// The rotary encoder is debounced in hardware, so no software debouncing is needed.
static const gpio_config_t swheel_a_config = {
    .pin_bit_mask = BIT64(GPIO_NUM_11),
    .mode = GPIO_MODE_INPUT,
    .intr_type = GPIO_INTR_ANYEDGE,
    .pull_up_en = true, // Not required but allows for exlcuding (no short) the external pull-up resistor.
    .pull_down_en = false,
};

static const gpio_config_t swheel_b_config = {
    .pin_bit_mask = BIT64(GPIO_NUM_12),
    .mode = GPIO_MODE_INPUT,
    .intr_type = GPIO_INTR_ANYEDGE,
    .pull_up_en = true, // Not required but allows for exlcuding (no short) the external pull-up resistor.
    .pull_down_en = false,
};

// Initialize the rotary encoder for the scroll wheel.
static void swheel_init(void)
{
    ESP_ERROR_CHECK(gpio_config(&swheel_a_config));
    ESP_ERROR_CHECK(gpio_config(&swheel_b_config));
    ESP_LOGI(TAG, "USB swheel_init");
}

// Enum for scroll wheel direction.
typedef enum {
    SCROLL_WHEEL_NONE,
    SCROLL_WHEEL_UP,
    SCROLL_WHEEL_DOWN,
} scroll_wheel_dir_t;

// Enum for active level of the rotary encoder.
typedef enum {
    SWHEEL_A_LOW,
    SWHEEL_B_LOW,
    SWHEEL_A_HIGH,
    SWHEEL_B_HIGH,
} swheel_active_t;

static swheel_active_t swheel_a_state = SWHEEL_A_LOW;
static swheel_active_t swheel_b_state = SWHEEL_B_LOW;

static scroll_wheel_dir_t swheel_dir = SCROLL_WHEEL_NONE;

static bool swheel_event = false;

// The rotary encoder is debounced in hardware, so no software debouncing is needed.
static void swheel_a_isr(void *arg)
{
    swheel_a_state = gpio_get_level(GPIO_NUM_11);
    // Direction is determined by the active level of the other pin.
    if (swheel_a_state == SWHEEL_A_HIGH) {
        if (swheel_b_state == SWHEEL_B_HIGH) {
            // A Low to High transition when B is High is a scroll wheel up event.
            swheel_dir = SCROLL_WHEEL_UP;
        } else {
            // A Low to High transition when B is Low is a scroll wheel down event.
            swheel_dir = SCROLL_WHEEL_DOWN;
        }
    } else {
        if (swheel_b_state == SWHEEL_B_HIGH) {
            // A High to Low transition when B is High is a scroll wheel down event.
            swheel_dir = SCROLL_WHEEL_DOWN;
        } else {
            // A High to Low transition when B is Low is a scroll wheel up event.
            swheel_dir = SCROLL_WHEEL_UP;
        }
    }
    swheel_event = true;
}

static void swheel_b_isr(void *arg)
{
    swheel_b_state = gpio_get_level(GPIO_NUM_12);
    // Direction is determined by the active level of the other pin.
    if (swheel_b_state == SWHEEL_B_HIGH) {
        if (swheel_a_state == SWHEEL_A_HIGH) {
            // B Low to High transition when A is High is a scroll wheel down event.
            swheel_dir = SCROLL_WHEEL_DOWN;
        } else {
            // B Low to High transition when A is Low is a scroll wheel up event.
            swheel_dir = SCROLL_WHEEL_UP;
        }
    } else {
        if (swheel_a_state == SWHEEL_A_HIGH) {
            // B High to Low transition when A is High is a scroll wheel up event.
            swheel_dir = SCROLL_WHEEL_UP;
        } else {
            // B High to Low transition when A is Low is a scroll wheel down event.
            swheel_dir = SCROLL_WHEEL_DOWN;
        }
    }
    swheel_event = true;
}

// Multiply the reported scroll wheel movement depending on the scroll wheel speed.
#define SCROLL_WHEEL_SPEED_MIN 1
#define SCROLL_WHEEL_SPEED_MAX 10
#define SCROLL_WHEEL_PAUSE_MS 100
static int scroll_wheel_speed = SCROLL_WHEEL_SPEED_MIN;
static int scroll_stopped_cnt = 0;

static bool scroll_wheel_speed_adjustable = false;

// Report the scroll wheel state.
static void swheel_task_report(void)
{
    // Send a mouse report to the host when the scroll wheel is scrolled.
    if (swheel_dir == SCROLL_WHEEL_UP) {
        ESP_LOGI(TAG, "SWHEEL: UP");
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE, 0x00, 0, 0, scroll_wheel_speed, 0);
    } else if (swheel_dir == SCROLL_WHEEL_DOWN) {
        ESP_LOGI(TAG, "SWHEEL: DOWN");
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE, 0x00, 0, 0, -scroll_wheel_speed, 0);
    }
}

// Function to adjust the scroll wheel speed.
static void swheel_speed_adjust(bool swheel_event)
{
    if (swheel_event) {
        scroll_stopped_cnt = 0;
        if (scroll_wheel_speed < SCROLL_WHEEL_SPEED_MAX) {
            // If there is a scroll wheel event, then increase the scroll wheel speed.
            scroll_wheel_speed++;
        }
    }
    else if (scroll_wheel_speed > SCROLL_WHEEL_SPEED_MIN) {
        if (scroll_stopped_cnt > SCROLL_WHEEL_PAUSE_MS) {
            // If there is no scroll wheel event, then decrease the scroll wheel speed.
            scroll_wheel_speed--;
            scroll_stopped_cnt = 0;
        }
        else {
            scroll_stopped_cnt++;
        }
    }
}

// Tasks for the scroll wheel.
static void swheel_task(void *arg)
{
    gpio_install_isr_service(0);
    gpio_isr_handler_add(GPIO_NUM_11, swheel_a_isr, NULL);
    gpio_isr_handler_add(GPIO_NUM_12, swheel_b_isr, NULL);
    // Initialize the scroll wheel state.
    swheel_a_state = gpio_get_level(GPIO_NUM_11);
    swheel_b_state = gpio_get_level(GPIO_NUM_12);

    while (1) {
        if (scroll_wheel_speed_adjustable) {
            swheel_speed_adjust(swheel_event);
        }
        if (swheel_event) {
            swheel_event = false;
            swheel_task_report();
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// Our mouse used a Pixart PAW3395DB-TJ3T optical sensor.
// The sensor is connected to the ESP32-S3 via SPI.
// The sensor is configured to use SPI mode 3.
// The sensor is configured to use a clock speed of 10MHz.
// The sensor is configured to use a 16 bit word size.
// The sensor is configured to use MSB first.
// The sensor is configured to use a 4 wire interface.

// The sensor is connected to the following pins:
// NCS: GPIO_NUM_27
// MOSI: GPIO_NUM_28
// SCLK: GPIO_NUM_29
// MISO: GPIO_NUM_30
// NRESET: GPIO_NUM_31
// MOTION: GPIO_NUM_38
// PWR_EN: GPIO_NUM_39

// Set up the IO pins for the sensor.
static const gpio_config_t sensor_ncs_config = {
    .pin_bit_mask = BIT64(GPIO_NUM_27),
    .mode = GPIO_MODE_OUTPUT,
    .intr_type = GPIO_INTR_DISABLE,
    .pull_up_en = false,
    .pull_down_en = false,
};

static const gpio_config_t sensor_mosi_config = {
    .pin_bit_mask = BIT64(GPIO_NUM_28),
    .mode = GPIO_MODE_OUTPUT,
    .intr_type = GPIO_INTR_DISABLE,
    .pull_up_en = false,
    .pull_down_en = false,
};

static const gpio_config_t sensor_sclk_config = {
    .pin_bit_mask = BIT64(GPIO_NUM_29),
    .mode = GPIO_MODE_OUTPUT,
    .intr_type = GPIO_INTR_DISABLE,
    .pull_up_en = false,
    .pull_down_en = false,
};

static const gpio_config_t sensor_miso_config = {
    .pin_bit_mask = BIT64(GPIO_NUM_30),
    .mode = GPIO_MODE_INPUT,
    .intr_type = GPIO_INTR_DISABLE,
    .pull_up_en = false,
    .pull_down_en = false,
};

static const gpio_config_t sensor_nreset_config = {
    .pin_bit_mask = BIT64(GPIO_NUM_31),
    .mode = GPIO_MODE_OUTPUT,
    .intr_type = GPIO_INTR_DISABLE,
    .pull_up_en = false,
    .pull_down_en = false,
};

static const gpio_config_t sensor_motion_config = {
    .pin_bit_mask = BIT64(GPIO_NUM_38),
    .mode = GPIO_MODE_INPUT,
    .intr_type = GPIO_INTR_DISABLE,
    .pull_up_en = false,
    .pull_down_en = false,
};

static const gpio_config_t sensor_pwr_en_config = {
    .pin_bit_mask = BIT64(GPIO_NUM_39),
    .mode = GPIO_MODE_OUTPUT,
    .intr_type = GPIO_INTR_DISABLE,
    .pull_up_en = false,
    .pull_down_en = false,
};

// Initialize the IO pins for the sensor.
static void sensor_init(void)
{
    ESP_ERROR_CHECK(gpio_config(&sensor_ncs_config));
    ESP_ERROR_CHECK(gpio_config(&sensor_mosi_config));
    ESP_ERROR_CHECK(gpio_config(&sensor_sclk_config));
    ESP_ERROR_CHECK(gpio_config(&sensor_miso_config));
    ESP_ERROR_CHECK(gpio_config(&sensor_nreset_config));
    ESP_ERROR_CHECK(gpio_config(&sensor_motion_config));
    ESP_ERROR_CHECK(gpio_config(&sensor_pwr_en_config));
    ESP_LOGI(TAG, "USB sensor_init");
}

// The sensor is configured to use SPI mode 3.
#define SENSOR_SPI_MODE 3
// The sensor is configured to use a clock speed of 10MHz.
#define SENSOR_SPI_CLOCK_SPEED_HZ 10000000
// The sensor is configured to use a 16 bit word size.
#define SENSOR_SPI_WORD_SIZE 16
// The sensor is configured to use MSB first.
#define SENSOR_SPI_MSB_FIRST true
// The sensor is configured to use a 4 wire interface.
#define SENSOR_SPI_DUPLEX_MODE SPI_DUPLEX_MODE_4

// Set up the SPI bus for the sensor.
static const spi_bus_config_t sensor_spi_bus_config = {
    .mosi_io_num = GPIO_NUM_28,
    .miso_io_num = GPIO_NUM_30,
    .sclk_io_num = GPIO_NUM_29,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = 0,
    .flags = 0,
    .intr_flags = 0,
};

// Set up the SPI device for the sensor.
static const spi_device_interface_config_t sensor_spi_device_config = {
    .command_bits = 0,
    .address_bits = 0,
    .dummy_bits = 0,
    .mode = SENSOR_SPI_MODE,
    .duty_cycle_pos = 0,
    .cs_ena_pretrans = 0,
    .cs_ena_posttrans = 0,
    .clock_speed_hz = SENSOR_SPI_CLOCK_SPEED_HZ,
    .input_delay_ns = 0,
    .spics_io_num = GPIO_NUM_27,
    .flags = 0,
    .queue_size = 1,
    .pre_cb = NULL,
    .post_cb = NULL,
};

spi_device_handle_t sensor_spi_device;

// Motion data struct
#define MOTION_DATA_BUFFER_SIZE 10

typedef struct {
    uint8_t motion_x;
    uint8_t motion_y;
    uint32_t timestamp;
} MotionData;

MotionData motion_data_buffer[MOTION_DATA_BUFFER_SIZE];
int motion_data_buffer_write_index = 0;
int motion_data_buffer_read_index = 0;

// Function to add motion data to the buffer
static void add_motion_data_to_buffer(uint8_t motion_x, uint8_t motion_y, uint32_t timestamp) {
    motion_data_buffer[motion_data_buffer_write_index].motion_x = motion_x;
    motion_data_buffer[motion_data_buffer_write_index].motion_y = motion_y;
    motion_data_buffer[motion_data_buffer_write_index].timestamp = timestamp;

    // Check if the buffer is full
    // Next write index
    int next_write_index = (motion_data_buffer_write_index + 1) % MOTION_DATA_BUFFER_SIZE;
    if (motion_data_buffer_write_index == motion_data_buffer_read_index) {
        int next_read_index = (motion_data_buffer_read_index + 1) % MOTION_DATA_BUFFER_SIZE;
        // If the buffer is full, then drop the oldest data by combining it with the second oldest data
        motion_data_buffer[next_read_index].motion_x += motion_data_buffer[motion_data_buffer_read_index].motion_x;
        motion_data_buffer[next_read_index].motion_y += motion_data_buffer[motion_data_buffer_read_index].motion_y;
        motion_data_buffer_read_index = next_read_index;
    }
    motion_data_buffer_write_index = next_write_index;
}

// Function to read the oldest motion data from the buffer
static MotionData read_oldest_motion_data_from_buffer(void) {
    MotionData oldest_data = motion_data_buffer[motion_data_buffer_read_index];

    motion_data_buffer_read_index = (motion_data_buffer_read_index + 1) % MOTION_DATA_BUFFER_SIZE;

    return oldest_data;
}

// Function to process motion data
static void process_motion_data(void) {
    // Catch up to the new data by processing the remaining motion data in the buffer
    while (motion_data_buffer_read_index != motion_data_buffer_write_index) {
        MotionData data = read_oldest_motion_data_from_buffer();

        // Process the motion data by moving the mouse cursor
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE, 0x00, data.motion_x, data.motion_y, 0, 0);
    }
}

// Function to read the motion data from the Pixart PAW3395 sensor.
static void sensor_read_motion_data(void)
{
    // Send the command to read the motion data.
    uint8_t command[] = {0x5A, 0x5A, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00};
    spi_transaction_t transaction;
    memset(&transaction, 0, sizeof(transaction));
    transaction.length = sizeof(command) * 8;
    transaction.tx_buffer = command;
    ESP_ERROR_CHECK(spi_device_transmit(sensor_spi_device, &transaction));

    // Wait for the sensor to acknowledge the command.
    vTaskDelay(pdMS_TO_TICKS(10));

    // Read the response from the sensor.
    uint8_t response[8];
    memset(&transaction, 0, sizeof(transaction));
    transaction.length = sizeof(response) * 8;
    transaction.rx_buffer = response;
    ESP_ERROR_CHECK(spi_device_transmit(sensor_spi_device, &transaction));

    // Check if the response is valid.
    if (response[0] == 0x5A && response[1] == 0x5A && response[2] == 0x03 && response[3] == 0x00) {
        // The motion data is a 16 bit signed integer.
        int16_t motion_x = (response[4] << 8) | response[5];
        int16_t motion_y = (response[6] << 8) | response[7];
        int32_t timestamp = esp_timer_get_time();
        ESP_LOGI(TAG, "Motion data: %d, %d", motion_x, motion_y);
        // Add motion data to the buffer
        add_motion_data_to_buffer(motion_x, motion_y, timestamp);
        // Store the motion data in the buffer with timestamp
        motion_data_buffer[motion_data_buffer_write_index].motion_x = motion_x;
        motion_data_buffer[motion_data_buffer_write_index].motion_y = motion_y;
        motion_data_buffer[motion_data_buffer_write_index].timestamp = esp_timer_get_time();

        // Update the buffer index
        motion_data_buffer_write_index = (motion_data_buffer_write_index + 1) % MOTION_DATA_BUFFER_SIZE;
    } else {
        ESP_LOGE(TAG, "Failed to read motion data");
    }
}

// Function to write a register on the Pixart PAW3395 sensor.
static void sensor_write_register(uint8_t address, uint8_t value)
{
    // Send the command to write the register.
    uint8_t command[] = {0x5A, 0x5A, 0x01, address, value, 0x00, 0x00, 0x00};
    spi_transaction_t transaction;
    memset(&transaction, 0, sizeof(transaction));
    transaction.length = sizeof(command) * 8;
    transaction.tx_buffer = command;
    ESP_ERROR_CHECK(spi_device_transmit(sensor_spi_device, &transaction));

    // Wait for the sensor to acknowledge the command.
    vTaskDelay(pdMS_TO_TICKS(10));

    // Read the response from the sensor.
    uint8_t response[8];
    memset(&transaction, 0, sizeof(transaction));
    transaction.length = sizeof(response) * 8;
    transaction.rx_buffer = response;
    ESP_ERROR_CHECK(spi_device_transmit(sensor_spi_device, &transaction));

    // Check if the response is valid.
    if (response[0] == 0x5A && response[1] == 0x5A && response[2] == 0x01 && response[3] == address) {
        ESP_LOGI(TAG, "Register 0x%02X set to 0x%02X", address, value);
    } else {
        ESP_LOGE(TAG, "Failed to set register 0x%02X to 0x%02X", address, value);
    }
}

// Initialize the SPI device for the sensor.
static void sensor_spi_init(void)
{
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &sensor_spi_bus_config, 1));
    ESP_ERROR_CHECK(spi_bus_add_device(SPI3_HOST, &sensor_spi_device_config, &sensor_spi_device));
    ESP_LOGI(TAG, "SPI device initialized");
}

// Define the configuration for the Pixart PAW3395 sensor.
static const uint8_t sensor_register_programming_sequence [][2] = {
    {0x00, 0x00}, // Reset
    {0x01, 0x00}, // Power down
    {0x02, 0x00}, // Motion wake up
    {0x0A, 0x01}, // Enable motion events
    {0x0B, 0x01}, // Enable motion events
};

// Function to configure the Pixart PAW3395 sensor.
static void sensor_configure(void)
{
    // Configure the sensor's registers.
    for (int i = 0; i < sizeof(sensor_register_programming_sequence) / sizeof(sensor_register_programming_sequence[0]); i++) {
        sensor_write_register(sensor_register_programming_sequence[i][0], sensor_register_programming_sequence[i][1]);
        // Wait for the sensor to acknowledge the command.
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "SPI device configured");
}

// Max 4000Hz
#define REPORT_RATE_US 250

// Function to convert NS to ticks
#define US_TO_TICKS(us) ((us * 1000) / pdMS_TO_TICKS(1))

// Sensor task
static void sensor_task(void *arg)
{
    while (1) {
        // Time stamp to ensure we do not exceed REPORT_RATE_MS
        uint32_t start_us = esp_timer_get_time();
        // Process motion data
        process_motion_data();
        // Read the motion data from the Pixart PAW3395 sensor.
        sensor_read_motion_data();
        // Time stamp to ensure we do not exceed REPORT_RATE_MS
        // Wait until REPORT_RATE_MS has elapsed
        uint32_t diff_us = (esp_timer_get_time() - start_us) / 1000;
        if (diff_us < REPORT_RATE_US) {
            vTaskDelay(US_TO_TICKS(REPORT_RATE_US - diff_us));
        }
    }
}

/********* Application ***************/

void app_main(void)
{
    // Initialize the USB stack.
    ESP_LOGI(TAG, "USB initialization");
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = hid_string_descriptor,
        .string_descriptor_count = sizeof(hid_string_descriptor) / sizeof(hid_string_descriptor[0]),
        .external_phy = false,
        .configuration_descriptor = hid_configuration_descriptor,
    };

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(TAG, "USB initialization DONE");

    if (!tud_mounted()) {
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
    // Initialize the SPI device for the sensor.
    sensor_spi_init();

    // Start the Pixart PAW3395 sensor.
    sensor_configure();

    // Create the tasks for the software latches for the mouse buttons.
    xTaskCreate(mb_latch_task, "mb_latch_task", 2048, NULL, 1, NULL);
    // Create the tasks for the software debouncing for the mouse wheel button and side buttons.
    xTaskCreate(button_debounce_task, "button_debounce_task", 2048, NULL, 1, NULL);
    // Create the tasks for the scroll wheel.
    xTaskCreate(swheel_task, "swheel_task", 2048, NULL, 1, NULL);
    // Create the tasks for the Pixart PAW3395 sensor.
    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 1, NULL);

    // Main loop
    while (1) {
    }
}
