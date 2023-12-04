/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "kami_mouse.h"

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

/************* IO Configs ****************/

/*
// TODO: Update the mode based on if the usb is connected once wifi is implemented.
static bool wifi_report = USB_REPORT;
*/

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

// Our mouse used a Pixart PAW3395DM-T6QU optical sensor.
// The sensor is connected to the ESP32-S3 via SPI.
// The sensor is configured to use SPI mode 3.
// The sensor is configured to use a clock speed of 10MHz.
// The sensor is configured to use a 16 bit word size. (default)
// The sensor is configured to use MSB first. (default)
// The sensor is configured to use a 4 wire interface. (default)

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
// 1 bit for direction, 7 bits for address, and dummy bits will be 0 for writes or overriden for reads.
static const spi_device_interface_config_t sensor_spi_device_config = {
    .command_bits = 1,
    .address_bits = 7,
    .dummy_bits = 0,
    .mode = SENSOR_SPI_MODE,
    .duty_cycle_pos = 0,
    .cs_ena_pretrans = 0,
    .cs_ena_posttrans = 0,
    .clock_speed_hz = SENSOR_SPI_CLOCK_SPEED_HZ,
    .input_delay_ns = SENSOR_INPUT_DELAY_NS,
    .spics_io_num = GPIO_NUM_27,
    .flags = 0,
    .queue_size = 1,
    .pre_cb = NULL,
    .post_cb = NULL,
};

/************* Programming Sequences *************/

// Define the configuration for the Pixart PAW3395 sensor.
/*
6.2 Power-Up Initialization Register Setting
*/
static const uint8_t sensor_prog_seq_first[][2] = {
	{0x7F, 0x07}, // 0x7F with value 0x07
	{0x40, 0x41}, // 0x40 with value 0x41
	{0x7F, 0x00}, // 0x7F with value 0x00
	{0x40, 0x80}, // 0x40 with value 0x80
	{0x7F, 0x0E}, // 0x7F with value 0x0E
	{0x55, 0x0D}, // 0x55 with value 0x0D
	{0x56, 0x1B}, // 0x56 with value 0x1B
	{0x57, 0xE8}, // 0x57 with value 0xE8
	{0x58, 0xD5}, // 0x58 with value 0xD5
	{0x7F, 0x14}, // 0x7F with value 0x14
	{0x42, 0xBC}, // 0x42 with value 0xBC
	{0x43, 0x74}, // 0x43 with value 0x74
	{0x4B, 0x20}, // 0x4B with value 0x20
	{0x4D, 0x00}, // 0x4D with value 0x00
	{0x53, 0x0E}, // 0x53 with value 0x0E
	{0x7F, 0x05}, // 0x7F with value 0x05
	{0x44, 0x04}, // 0x44 with value 0x04
	{0x4D, 0x06}, // 0x4D with value 0x06
	{0x51, 0x40}, // 0x51 with value 0x40
	{0x53, 0x40}, // 0x53 with value 0x40
	{0x55, 0xCA}, // 0x55 with value 0xCA
	{0x5A, 0xE8}, // 0x5A with value 0xE8
	{0x5B, 0xEA}, // 0x5B with value 0xEA
	{0x61, 0x31}, // 0x61 with value 0x31
	{0x62, 0x64}, // 0x62 with value 0x64
	{0x6D, 0xB8}, // 0x6D with value 0xB8
	{0x6E, 0x0F}, // 0x6E with value 0x0F
	{0x70, 0x02}, // 0x70 with value 0x02
	{0x4A, 0x2A}, // 0x4A with value 0x2A
	{0x60, 0x26}, // 0x60 with value 0x26
	{0x7F, 0x06}, // 0x7F with value 0x06
	{0x6D, 0x70}, // 0x6D with value 0x70
	{0x6E, 0x60}, // 0x6E with value 0x60
	{0x6F, 0x04}, // 0x6F with value 0x04
	{0x53, 0x02}, // 0x53 with value 0x02
	{0x55, 0x11}, // 0x55 with value 0x11
	{0x7A, 0x01}, // 0x7A with value 0x01
	{0x7D, 0x51}, // 0x7D with value 0x51
	{0x7F, 0x07}, // 0x7F with value 0x07
	{0x41, 0x10}, // 0x41 with value 0x10
	{0x42, 0x32}, // 0x42 with value 0x32
	{0x43, 0x00}, // 0x43 with value 0x00
	{0x7F, 0x08}, // 0x7F with value 0x08
	{0x71, 0x4F}, // 0x71 with value 0x4F
	{0x7F, 0x09}, // 0x7F with value 0x09
	{0x62, 0x1F}, // 0x62 with value 0x1F
	{0x63, 0x1F}, // 0x63 with value 0x1F
	{0x65, 0x03}, // 0x65 with value 0x03
	{0x66, 0x03}, // 0x66 with value 0x03
	{0x67, 0x1F}, // 0x67 with value 0x1F
	{0x68, 0x1F}, // 0x68 with value 0x1F
	{0x69, 0x03}, // 0x69 with value 0x03
	{0x6A, 0x03}, // 0x6A with value 0x03
	{0x6C, 0x1F}, // 0x6C with value 0x1F
	{0x6D, 0x1F}, // 0x6D with value 0x1F
	{0x51, 0x04}, // 0x51 with value 0x04
	{0x53, 0x20}, // 0x53 with value 0x20
	{0x54, 0x20}, // 0x54 with value 0x20
	{0x71, 0x0C}, // 0x71 with value 0x0C
	{0x72, 0x07}, // 0x72 with value 0x07
	{0x73, 0x07}, // 0x73 with value 0x07
	{0x7F, 0x0A}, // 0x7F with value 0x0A
	{0x4A, 0x14}, // 0x4A with value 0x14
	{0x4C, 0x14}, // 0x4C with value 0x14
	{0x55, 0x19}, // 0x55 with value 0x19
	{0x7F, 0x14}, // 0x7F with value 0x14
	{0x4B, 0x30}, // 0x4B with value 0x30
	{0x4C, 0x03}, // 0x4C with value 0x03
	{0x61, 0x0B}, // 0x61 with value 0x0B
	{0x62, 0x0A}, // 0x62 with value 0x0A
	{0x63, 0x02}, // 0x63 with value 0x02
	{0x7F, 0x15}, // 0x7F with value 0x15
	{0x4C, 0x02}, // 0x4C with value 0x02
	{0x56, 0x02}, // 0x56 with value 0x02
	{0x41, 0x91}, // 0x41 with value 0x91
	{0x4D, 0x0A}, // 0x4D with value 0x0A
	{0x7F, 0x0C}, // 0x7F with value 0x0C
	{0x4A, 0x10}, // 0x4A with value 0x10
	{0x4B, 0x0C}, // 0x4B with value 0x0C
	{0x4C, 0x40}, // 0x4C with value 0x40
	{0x41, 0x25}, // 0x41 with value 0x25
	{0x55, 0x18}, // 0x55 with value 0x18
	{0x56, 0x14}, // 0x56 with value 0x14
	{0x49, 0x0A}, // 0x49 with value 0x0A
	{0x42, 0x00}, // 0x42 with value 0x00
	{0x43, 0x2D}, // 0x43 with value 0x2D
	{0x44, 0x0C}, // 0x44 with value 0x0C
	{0x54, 0x1A}, // 0x54 with value 0x1A
	{0x5A, 0x0D}, // 0x5A with value 0x0D
	{0x5F, 0x1E}, // 0x5F with value 0x1E
	{0x5B, 0x05}, // 0x5B with value 0x05
	{0x5E, 0x0F}, // 0x5E with value 0x0F
	{0x7F, 0x0D}, // 0x7F with value 0x0D
	{0x48, 0xDD}, // 0x48 with value 0xDD
	{0x4F, 0x03}, // 0x4F with value 0x03
	{0x52, 0x49}, // 0x52 with value 0x49
	{0x51, 0x00}, // 0x51 with value 0x00
	{0x54, 0x5B}, // 0x54 with value 0x5B
	{0x53, 0x00}, // 0x53 with value 0x00
	{0x56, 0x64}, // 0x56 with value 0x64
	{0x55, 0x00}, // 0x55 with value 0x00
	{0x58, 0xA5}, // 0x58 with value 0xA5
	{0x57, 0x02}, // 0x57 with value 0x02
	{0x5A, 0x29}, // 0x5A with value 0x29
	{0x5B, 0x47}, // 0x5B with value 0x47
	{0x5C, 0x81}, // 0x5C with value 0x81
	{0x5D, 0x40}, // 0x5D with value 0x40
	{0x71, 0xDC}, // 0x71 with value 0xDC
	{0x70, 0x07}, // 0x70 with value 0x07
	{0x73, 0x00}, // 0x73 with value 0x00
	{0x72, 0x08}, // 0x72 with value 0x08
	{0x75, 0xDC}, // 0x75 with value 0xDC
	{0x74, 0x07}, // 0x74 with value 0x07
	{0x77, 0x00}, // 0x77 with value 0x00
	{0x76, 0x08}, // 0x76 with value 0x08
	{0x7F, 0x10}, // 0x7F with value 0x10
	{0x4C, 0xD0}, // 0x4C with value 0xD0
	{0x7F, 0x00}, // 0x7F with value 0x00
	{0x4F, 0x63}, // 0x4F with value 0x63
	{0x4E, 0x00}, // 0x4E with value 0x00
	{0x52, 0x63}, // 0x52 with value 0x63
	{0x51, 0x00}, // 0x51 with value 0x00
	{0x54, 0x54}, // 0x54 with value 0x54
	{0x5A, 0x10}, // 0x5A with value 0x10
	{0x77, 0x4F}, // 0x77 with value 0x4F
	{0x47, 0x01}, // 0x47 with value 0x01
	{0x5B, 0x40}, // 0x5B with value 0x40
	{0x64, 0x60}, // 0x64 with value 0x60
	{0x65, 0x06}, // 0x65 with value 0x06
	{0x66, 0x13}, // 0x66 with value 0x13
	{0x67, 0x0F}, // 0x67 with value 0x0F
	{0x78, 0x01}, // 0x78 with value 0x01
	{0x79, 0x9C}, // 0x79 with value 0x9C
	{0x40, 0x00}, // 0x40 with value 0x00
	{0x55, 0x02}, // 0x55 with value 0x02
	{0x23, 0x70}, // 0x23 with value 0x70
	{0x22, 0x01}, // 0x22 with value 0x01
};

static const uint8_t sensor_prog_seq_0x6C_fail[][2] = {
	{0x7F, 0x14}, // 0x7F with value 0x14
	{0x6C, 0x00}, // 0x6C with value 0x00
	{0x7F, 0x00}, // 0x7F with value 0x00
};

static const uint8_t sensor_prog_seq_second[][2] = {
	{0x22, 0x00}, // 0x22 with value 0x00
	{0x55, 0x00}, // 0x55 with value 0x00
	{0x7F, 0x07}, // 0x7F with value 0x07
	{0x40, 0x40}, // 0x40 with value 0x40
	{0x7F, 0x00}, // 0x7F with value 0x00
};

/*
Note:
Special precaution needs to be taken for register 0x40 to avoid overwrite other bits in the register. When writing
the bit[1:0] to configure to different modes, one need to read and store its current value first, then apply bit
masking and write back the new value into the register.
I'm not sure if this is necessary, because we just wrote the value 0x40 to the register in the previous step.
So I will use the previous value. This assumes we only change the mode once out of reset.
*/

/*
High Performance Mode (Default)
*/
static const uint8_t sensor_prog_seq_hpm[][2] = {
	{0x7F, 0x05}, // 0x7F with value 0x05
	{0x51, 0x40}, // 0x51 with value 0x40
	{0x53, 0x40}, // 0x53 with value 0x40
	{0x61, 0x31}, // 0x61 with value 0x31
	{0x6E, 0x0F}, // 0x6E with value 0x0F
	{0x7F, 0x07}, // 0x7F with value 0x07
	{0x42, 0x32}, // 0x42 with value 0x32
	{0x43, 0x00}, // 0x43 with value 0x00
	{0x7F, 0x0D}, // 0x7F with value 0x0D
	{0x51, 0x00}, // 0x51 with value 0x00
	{0x52, 0x49}, // 0x52 with value 0x49
	{0x53, 0x00}, // 0x53 with value 0x00
	{0x54, 0x5B}, // 0x54 with value 0x5B
	{0x55, 0x00}, // 0x55 with value 0x00
	{0x56, 0x64}, // 0x56 with value 0x64
	{0x57, 0x02}, // 0x57 with value 0x02
	{0x58, 0xA5}, // 0x58 with value 0xA5
	{0x7F, 0x00}, // 0x7F with value 0x00
	{0x54, 0x54}, // 0x54 with value 0x54
	{0x78, 0x01}, // 0x78 with value 0x01
	{0x79, 0x9C}, // 0x79 with value 0x9C
	{0x40, 0x40}, // 0x40 with value 0x00 | 0x40
};

/*
Low Power Mode
*/
static const uint8_t sensor_prog_seq_lpm[][2] = {
	{0x7F, 0x05}, // 0x7F with value 0x05
	{0x51, 0x40}, // 0x51 with value 0x40
	{0x53, 0x40}, // 0x53 with value 0x40
	{0x61, 0x3B}, // 0x61 with value 0x3B
	{0x6E, 0x1F}, // 0x6E with value 0x1F
	{0x7F, 0x07}, // 0x7F with value 0x07
	{0x42, 0x32}, // 0x42 with value 0x32
	{0x43, 0x00}, // 0x43 with value 0x00
	{0x7F, 0x0D}, // 0x7F with value 0x0D
	{0x51, 0x00}, // 0x51 with value 0x00
	{0x52, 0x49}, // 0x52 with value 0x49
	{0x53, 0x00}, // 0x53 with value 0x00
	{0x54, 0x5B}, // 0x54 with value 0x5B
	{0x55, 0x00}, // 0x55 with value 0x00
	{0x56, 0x64}, // 0x56 with value 0x64
	{0x57, 0x02}, // 0x57 with value 0x02
	{0x58, 0xA5}, // 0x58 with value 0xA5
	{0x7F, 0x00}, // 0x7F with value 0x00
	{0x54, 0x54}, // 0x54 with value 0x54
	{0x78, 0x01}, // 0x78 with value 0x01
	{0x79, 0x9C}, // 0x79 with value 0x9C
	{0x40, 0x41}, // 0x40 with value 0x01 | 0x40
};

/*
Office Mode
*/
static const uint8_t sensor_prog_seq_wrk[][2] = {
	{0x7F, 0x05}, // 0x7F with value 0x05
	{0x51, 0x28}, // 0x51 with value 0x28
	{0x53, 0x30}, // 0x53 with value 0x30
	{0x61, 0x3B}, // 0x61 with value 0x3B
	{0x6E, 0x1F}, // 0x6E with value 0x1F
	{0x7F, 0x07}, // 0x7F with value 0x07
	{0x42, 0x32}, // 0x42 with value 0x32
	{0x43, 0x00}, // 0x43 with value 0x00
	{0x7F, 0x0D}, // 0x7F with value 0x0D
	{0x51, 0x00}, // 0x51 with value 0x00
	{0x52, 0x49}, // 0x52 with value 0x49
	{0x53, 0x00}, // 0x53 with value 0x00
	{0x54, 0x5B}, // 0x54 with value 0x5B
	{0x55, 0x00}, // 0x55 with value 0x00
	{0x56, 0x64}, // 0x56 with value 0x64
	{0x57, 0x02}, // 0x57 with value 0x02
	{0x58, 0xA5}, // 0x58 with value 0xA5
	{0x7F, 0x00}, // 0x7F with value 0x00
	{0x54, 0x52}, // 0x54 with value 0x52
	{0x78, 0x0A}, // 0x78 with value 0x0A
	{0x79, 0x0F}, // 0x79 with value 0x0F
	{0x40, 0x42}, // 0x40 with value 0x02 | 0x40
};

/*
Corded Gaming Mode
*/
static const uint8_t sensor_prog_seq_crd[][2] = {
	{0x7F, 0x05}, // 0x7F with value 0x05
	{0x51, 0x40}, // 0x51 with value 0x40
	{0x53, 0x40}, // 0x53 with value 0x40
	{0x61, 0x31}, // 0x61 with value 0x31
	{0x6E, 0x0F}, // 0x6E with value 0x0F
	{0x7F, 0x07}, // 0x7F with value 0x07
	{0x42, 0x2F}, // 0x42 with value 0x2F
	{0x43, 0x00}, // 0x43 with value 0x00
	{0x7F, 0x0D}, // 0x7F with value 0x0D
	{0x51, 0x12}, // 0x51 with value 0x12
	{0x52, 0xDB}, // 0x52 with value 0xDB
	{0x53, 0x12}, // 0x53 with value 0x12
	{0x54, 0xDC}, // 0x54 with value 0xDC
	{0x55, 0x12}, // 0x55 with value 0x12
	{0x56, 0xEA}, // 0x56 with value 0xEA
	{0x57, 0x15}, // 0x57 with value 0x15
	{0x58, 0x2D}, // 0x58 with value 0x2D
	{0x7F, 0x00}, // 0x7F with value 0x00
	{0x54, 0x55}, // 0x54 with value 0x55
	{0x40, 0x83}, // 0x40 with value 0x83 (Note: We are writing the entire register)
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

// Initialize the software latches for the mouse buttons.
static void mb_latch_init(void)
{
    ESP_ERROR_CHECK(gpio_config(&lmb_config));
    ESP_ERROR_CHECK(gpio_config(&rmb_config));
    ESP_LOGI(TAG, "USB mb_latch_init");
}

static latch_event_t lmb_latch_event = LATCH_EVENT_CLEAR;
static latch_event_t rmb_latch_event = LATCH_EVENT_CLEAR;

static mouse_button_state_t current_lmb_state = MOUSE_BUTTON_UP;
static mouse_button_state_t current_rmb_state = MOUSE_BUTTON_UP;

// Get current mouse button state.
static mouse_button_state_t calculate_lmb_state(void)
{
    // Check if the mouse button is pressed or released.
    mouse_button_state_t observed_lmb_no_state = gpio_get_level(GPIO_NUM_4);
    mouse_button_state_t observed_lmb_nc_state = gpio_get_level(GPIO_NUM_5);
    // Check for a valid transition.
    if (observed_lmb_no_state == observed_lmb_nc_state)
    {
        return current_lmb_state;
    }

    // Pins are active low, so the observed state should be 1 for pressed and 0 for released.
    // Change the mouse button state.
    return observed_lmb_no_state;
}

static mouse_button_state_t calculate_rmb_state(void)
{
    // Check if the mouse button is pressed or released.
    mouse_button_state_t observed_rmb_no_state = gpio_get_level(GPIO_NUM_6);
    mouse_button_state_t observed_rmb_nc_state = gpio_get_level(GPIO_NUM_7);
    // Check for a valid transition.
    if (observed_rmb_no_state == observed_rmb_nc_state)
    {
        return current_lmb_state;
    }

    // Pins are active low, so the observed state should be 1 for pressed and 0 for released.
    // Change the mouse button state.
    return observed_rmb_no_state;
}

static void lmb_isr(void *arg)
{
    mouse_button_state_t next_lmb_state = calculate_lmb_state();

    // Make sure the state is actually changing.
    if (next_lmb_state == current_lmb_state)
    {
        return;
    }

    // Set the latch event.
    lmb_latch_event = LATCH_EVENT_SET;
    current_lmb_state = next_lmb_state;
}

static void rmb_isr(void *arg)
{
    mouse_button_state_t next_rmb_state = calculate_rmb_state();

    // Make sure the state is actually changing.
    if (next_rmb_state == current_rmb_state)
    {
        return;
    }

    // Set the latch event.
    rmb_latch_event = LATCH_EVENT_SET;
    current_rmb_state = next_rmb_state;
}

// Report the lmb button state.
static void lmb_latch_task_report(void)
{
    // Send a mouse report to the host when the mouse button is pressed or released.
    if (current_lmb_state == MOUSE_BUTTON_DOWN)
    {
        ESP_LOGI(TAG, "LMB: DOWN");
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE, 0x01, 0, 0, 0, 0);
    }
    else
    {
        ESP_LOGI(TAG, "LMB: UP");
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE, 0x01, 0, 0, 0, 0);
    }
}

// Report the rmb button state.
static void rmb_latch_task_report(void)
{
    // Send a mouse report to the host when the mouse button is pressed or released.
    if (current_rmb_state == MOUSE_BUTTON_DOWN)
    {
        ESP_LOGI(TAG, "RMB: DOWN");
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE, 0x02, 0, 0, 0, 0);
    }
    else
    {
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

    while (1)
    {
        if (lmb_latch_event == LATCH_EVENT_SET)
        {
            lmb_latch_event = LATCH_EVENT_READ;
            lmb_latch_task_report();
            // Only clear if the state is matching the current state.
            if (lmb_latch_event == LATCH_EVENT_READ)
            {
                lmb_latch_event = LATCH_EVENT_CLEAR;
            }
        }
        if (rmb_latch_event == LATCH_EVENT_SET)
        {
            rmb_latch_event = LATCH_EVENT_READ;
            rmb_latch_task_report();
            // Only clear if the state is matching the current state.
            if (rmb_latch_event == LATCH_EVENT_READ)
            {
                rmb_latch_event = LATCH_EVENT_CLEAR;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

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

static mouse_button_state_t mmb_state = MOUSE_BUTTON_UP;
static mouse_button_state_t smb4_state = MOUSE_BUTTON_UP;
static mouse_button_state_t smb5_state = MOUSE_BUTTON_UP;

// Global variables for the button debouncing.
static uint8_t mmb_hold_start = 0;
static uint8_t smb4_hold_start = 0;
static uint8_t smb5_hold_start = 0;

static void mmb_isr(void *arg)
{
    // Don't allow button unpressed events to be sent if the hold time has not been met.
    // Eager debounce for DOWN events.
    if (mmb_state == MOUSE_BUTTON_DOWN)
    {
        // The button is debounced by waiting for it to be stable for STABLE_POLL_TIME_MS.
        // But we take samples every 1ms to check for glitches.
        // Since this is the UP state, we should get low glitching.
        for (int i = 0; i < STABLE_POLL_TIME_MS; i++)
        {
            bool observed_mmb_state = gpio_get_level(GPIO_NUM_10);
            if (observed_mmb_state == mmb_state)
            {
                return;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    else
    {
        // Track the time the button was pressed.
        mmb_hold_start = esp_timer_get_time();
    }

    mmb_state = !mmb_state;
    mmb_event = true;
}

static void smb4_isr(void *arg)
{
    // Don't allow button unpressed events to be sent if the hold time has not been met.
    // Eager debounce for DOWN events.
    if (smb4_state == MOUSE_BUTTON_DOWN)
    {
        // The button is debounced by waiting for it to be stable for STABLE_POLL_TIME_MS.
        // But we take samples every 1ms to check for glitches.
        // Since this is the UP state, we should get low glitching.
        for (int i = 0; i < STABLE_POLL_TIME_MS; i++)
        {
            bool observed_smb4_state = gpio_get_level(GPIO_NUM_18);
            if (observed_smb4_state == smb4_state)
            {
                return;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    else
    {
        // Track the time the button was pressed.
        smb4_hold_start = esp_timer_get_time();
    }

    smb4_state = !smb4_state;
    smb4_event = true;
}

static void smb5_isr(void *arg)
{
    // Don't allow button unpressed events to be sent if the hold time has not been met.
    // Eager debounce for DOWN events.
    if (smb5_state == MOUSE_BUTTON_DOWN)
    {
        // The button is debounced by waiting for it to be stable for STABLE_POLL_TIME_MS.
        // But we take samples every 1ms to check for glitches.
        // Since this is the UP state, we should get low glitching.
        for (int i = 0; i < STABLE_POLL_TIME_MS; i++)
        {
            bool observed_smb5_state = gpio_get_level(GPIO_NUM_19);
            if (observed_smb5_state == smb5_state)
            {
                return;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    else
    {
        // Track the time the button was pressed.
        smb5_hold_start = esp_timer_get_time();
    }

    smb5_state = !smb5_state;
    smb5_event = true;
}

// Report the mmb button state.
static void mmb_debounce_task_report(void)
{
    // Send a mouse report to the host when the mouse button is pressed or released.
    if (mmb_event == MOUSE_BUTTON_DOWN)
    {
        ESP_LOGI(TAG, "MMB: DOWN");
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE, 0x01, 0, 0, 0, 0);
    }
    else
    {
        ESP_LOGI(TAG, "MMB: UP");
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE, 0x01, 0, 0, 0, 0);
    }
}

// Report the smb4 button state.
static void smb4_debounce_task_report(void)
{
    // Send a mouse report to the host when the mouse button is pressed or released.
    if (smb4_event == MOUSE_BUTTON_DOWN)
    {
        ESP_LOGI(TAG, "SMB4: DOWN");
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE, 0x02, 0, 0, 0, 0);
    }
    else
    {
        ESP_LOGI(TAG, "SMB4: UP");
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE, 0x02, 0, 0, 0, 0);
    }
}

// Report the smb5 button state.
static void smb5_debounce_task_report(void)
{
    // Send a mouse report to the host when the mouse button is pressed or released.
    if (smb5_event == MOUSE_BUTTON_DOWN)
    {
        ESP_LOGI(TAG, "SMB5: DOWN");
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE, 0x04, 0, 0, 0, 0);
    }
    else
    {
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

    while (1)
    {
        if (mmb_event)
        {
            mmb_event = false;
            mmb_debounce_task_report();
        }
        if (smb4_event)
        {
            smb4_event = false;
            smb4_debounce_task_report();
        }
        if (smb5_event)
        {
            smb5_event = false;
            smb5_debounce_task_report();
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// Initialize the rotary encoder for the scroll wheel.
static void swheel_init(void)
{
    ESP_ERROR_CHECK(gpio_config(&swheel_a_config));
    ESP_ERROR_CHECK(gpio_config(&swheel_b_config));
    ESP_LOGI(TAG, "USB swheel_init");
}

static swheel_active_t swheel_a_state = SWHEEL_A_LOW;
static swheel_active_t swheel_b_state = SWHEEL_B_LOW;

static scroll_wheel_dir_t swheel_dir = SCROLL_WHEEL_NONE;

static bool swheel_event = false;

// The rotary encoder is debounced in hardware, so no software debouncing is needed.
static void swheel_a_isr(void *arg)
{
    swheel_a_state = gpio_get_level(GPIO_NUM_11);
    // Direction is determined by the active level of the other pin.
    if (swheel_a_state == SWHEEL_A_HIGH)
    {
        if (swheel_b_state == SWHEEL_B_HIGH)
        {
            // A Low to High transition when B is High is a scroll wheel up event.
            swheel_dir = SCROLL_WHEEL_UP;
        }
        else
        {
            // A Low to High transition when B is Low is a scroll wheel down event.
            swheel_dir = SCROLL_WHEEL_DOWN;
        }
    }
    else
    {
        if (swheel_b_state == SWHEEL_B_HIGH)
        {
            // A High to Low transition when B is High is a scroll wheel down event.
            swheel_dir = SCROLL_WHEEL_DOWN;
        }
        else
        {
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
    if (swheel_b_state == SWHEEL_B_HIGH)
    {
        if (swheel_a_state == SWHEEL_A_HIGH)
        {
            // B Low to High transition when A is High is a scroll wheel down event.
            swheel_dir = SCROLL_WHEEL_DOWN;
        }
        else
        {
            // B Low to High transition when A is Low is a scroll wheel up event.
            swheel_dir = SCROLL_WHEEL_UP;
        }
    }
    else
    {
        if (swheel_a_state == SWHEEL_A_HIGH)
        {
            // B High to Low transition when A is High is a scroll wheel up event.
            swheel_dir = SCROLL_WHEEL_UP;
        }
        else
        {
            // B High to Low transition when A is Low is a scroll wheel down event.
            swheel_dir = SCROLL_WHEEL_DOWN;
        }
    }
    swheel_event = true;
}

static int scroll_wheel_speed = SCROLL_WHEEL_SPEED_MIN;
static int scroll_stopped_cnt = 0;

static bool scroll_wheel_speed_adjustable = false;

// Report the scroll wheel state.
static void swheel_task_report(void)
{
    // Send a mouse report to the host when the scroll wheel is scrolled.
    if (swheel_dir == SCROLL_WHEEL_UP)
    {
        ESP_LOGI(TAG, "SWHEEL: UP");
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE, 0x00, 0, 0, scroll_wheel_speed, 0);
    }
    else if (swheel_dir == SCROLL_WHEEL_DOWN)
    {
        ESP_LOGI(TAG, "SWHEEL: DOWN");
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE, 0x00, 0, 0, -scroll_wheel_speed, 0);
    }
}

// Function to adjust the scroll wheel speed.
static void swheel_speed_adjust(bool swheel_event)
{
    if (swheel_event)
    {
        scroll_stopped_cnt = 0;
        if (scroll_wheel_speed < SCROLL_WHEEL_SPEED_MAX)
        {
            // If there is a scroll wheel event, then increase the scroll wheel speed.
            scroll_wheel_speed++;
        }
    }
    else if (scroll_wheel_speed > SCROLL_WHEEL_SPEED_MIN)
    {
        if (scroll_stopped_cnt > SCROLL_WHEEL_PAUSE_MS)
        {
            // If there is no scroll wheel event, then decrease the scroll wheel speed.
            scroll_wheel_speed--;
            scroll_stopped_cnt = 0;
        }
        else
        {
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

    while (1)
    {
        if (scroll_wheel_speed_adjustable)
        {
            swheel_speed_adjust(swheel_event);
        }
        if (swheel_event)
        {
            swheel_event = false;
            swheel_task_report();
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

spi_device_handle_t sensor_spi_device;

MotionData motion_data_buffer[MOTION_DATA_BUFFER_SIZE];
int motion_data_buffer_write_index = 0;
int motion_data_buffer_read_index = 0;

// Function to add motion data to the buffer
static void add_motion_data_to_buffer(uint8_t motion_x, uint8_t motion_y, uint32_t timestamp)
{
    motion_data_buffer[motion_data_buffer_write_index].motion_x = motion_x;
    motion_data_buffer[motion_data_buffer_write_index].motion_y = motion_y;
    motion_data_buffer[motion_data_buffer_write_index].timestamp = timestamp;

    // Check if the buffer is full
    // Next write index
    int next_write_index = (motion_data_buffer_write_index + 1) % MOTION_DATA_BUFFER_SIZE;
    if (motion_data_buffer_write_index == motion_data_buffer_read_index)
    {
        int next_read_index = (motion_data_buffer_read_index + 1) % MOTION_DATA_BUFFER_SIZE;
        // If the buffer is full, then drop the oldest data by combining it with the second oldest data
        motion_data_buffer[next_read_index].motion_x += motion_data_buffer[motion_data_buffer_read_index].motion_x;
        motion_data_buffer[next_read_index].motion_y += motion_data_buffer[motion_data_buffer_read_index].motion_y;
        motion_data_buffer_read_index = next_read_index;
    }
    motion_data_buffer_write_index = next_write_index;
}

// Function to read the oldest motion data from the buffer
static MotionData read_oldest_motion_data_from_buffer(void)
{
    MotionData oldest_data = motion_data_buffer[motion_data_buffer_read_index];

    motion_data_buffer_read_index = (motion_data_buffer_read_index + 1) % MOTION_DATA_BUFFER_SIZE;

    return oldest_data;
}

// Function to process motion data
static void process_motion_data(void)
{
    // Catch up to the new data by processing the remaining motion data in the buffer
    while (motion_data_buffer_read_index != motion_data_buffer_write_index)
    {
        MotionData data = read_oldest_motion_data_from_buffer();

        // Process the motion data by moving the mouse cursor
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE, 0x00, data.motion_x, data.motion_y, 0, 0);
    }
}

// Function to read a register on the Pixart PAW3395 sensor.
static void sensor_read_register(uint8_t address, uint8_t *response, size_t response_size)
{
    spi_transaction_t transaction;
    spi_transaction_ext_t transaction_ext;

    // Read the response from the sensor.
    memset(&transaction, 0, sizeof(transaction));
    memset(&transaction_ext, 0, sizeof(transaction_ext));
    // Set flag to use extended SPI transaction and set dummy bits
    transaction.flags = SPI_TRANS_VARIABLE_DUMMY;
    transaction.cmd = 0;
    transaction.addr = address & 0x7F;
    transaction.length = response_size * 8;
    transaction.rx_buffer = response;
    transaction_ext.base = transaction;
    transaction_ext.dummy_bits = SENSOR_DUMMY_BITS;

    ESP_ERROR_CHECK(spi_device_transmit(sensor_spi_device, &transaction_ext));

    // Log the response.
    ESP_LOGI(TAG, "Read register 0x%02X", address);
    ESP_LOG_BUFFER_HEX(TAG, response, response_size);
}

// Function to set the sensor into motion burst mode from the Pixart PAW3395 sensor.
/**
5.2 Motion Pin Timing
The motion pin is an active low output that signals the micro-controller when motion has occurred. The motion pin
is lowered whenever the motion bit is set; in other words, whenever there is non-zero data in the Delta_X_L,
Delta_X_H, Delta_Y_L or Delta_Y_H registers. Clearing the motion bit (by reading Delta_X_L, Delta_X_H, Delta_Y_L
or Delta_Y_H registers) will put the motion pin high.

5.7 Burst Mode Operation
Burst mode is a special serial port operation mode which is used to reduce the serial transaction time for
predefined registers. The speed improvement is achieved by continuous data clocking to or from multiple registers
without the need to specify the register address and by not requiring the normal delay period between data bytes.

5.7.2 Procedure to Start Motion Burst
1. Lower NCS.
2. Wait for tNCS-SCLK
3. Send Motion_Burst address (0x16). After sending this address, MOSI must be held static (either high or low)
until the burst transmission is complete.
4. Wait for tSRAD
5. Start reading SPI data continuously up to 12 bytes. Motion burst must be terminated by pulling NCS high for at
least tBEXIT.
6. To read new motion burst data, repeat from step 1.

5.7.1 Motion Read
Reading the Motion_Burst register activates the Motion Read mode. The chip will respond with the following
motion burst report in this order.

BYTE[00] = Motion
BYTE[01] = Observation
BYTE[02] = Delta_X_L
BYTE[03] = Delta_X_H
BYTE[04] = Delta_Y_L
BYTE[05] = Delta_Y_H
BYTE[06] = SQUAL
BYTE[07] = RawData_Sum
BYTE[08] = Maximum_RawData
BYTE[09] = Minimum_Rawdata
BYTE[10] = Shutter_Upper
BYTE[11] = Shutter_Lower

After sending the Motion_Burst register address, the microcontroller must wait for tSRAD, and then begins reading
data. All data bits can be read with no delay between bytes by driving SCLK at the normal rate. The data is latched
into the output buffer after the last address bit is received. After the burst transmission is complete, the
microcontroller must raise the NCS line for at least tBEXIT to terminate burst mode. The serial port is not available for
use until it is reset with NCS, even for a second burst transmission.
*/
static void sensor_read_motion_burst(void)
{
    // SPI Interface will Lower NCS and wait for tNCS-SCLK.
    // Send Motion_Burst address (0x16). After sending this address, MOSI must be held static (either high or low)
    uint8_t motion_burst_reg_address = 0x16;
    uint8_t response[12];
    sensor_read_register(motion_burst_reg_address, response, sizeof(response));
    // Wait
    vTaskDelay(pdUS_TO_TICKS(SENSOR_READ_DELAY_US));

    // If there was no motion data then return.
    if (response[0] == 0)
    {
        return;
    }

    // Process the motion data
    // The motion data is a 16 bit signed integer.
    int16_t motion_x = response[2] | response[3] << 8;
    int16_t motion_y = response[4] | response[5] << 8;
    int32_t timestamp = esp_timer_get_time();
    ESP_LOGI(TAG, "Motion data: %d, %d", motion_x, motion_y);
    // Add motion data to the buffer
    add_motion_data_to_buffer(motion_x, motion_y, timestamp);
}

// Function to write a register on the Pixart PAW3395 sensor.
static void sensor_write_register(uint8_t address, uint8_t value)
{
    // Send the command to write the register.
    // The first byte contains the address (7-bit) and has a “1” as its MSB to indicate data direction.
    // The second byte contains the data.
    uint8_t command[1] = {value};
    spi_transaction_t transaction;
    memset(&transaction, 0, sizeof(transaction));
    transaction.cmd = 1;
    transaction.addr = address & 0x7F;
    transaction.length = sizeof(command) * 8;
    transaction.tx_buffer = command;
    ESP_ERROR_CHECK(spi_device_transmit(sensor_spi_device, &transaction));

    ESP_LOGI(TAG, "Register 0x%02X written with value 0x%02X", address, value);
}

// Initialize the SPI device for the sensor.
static void sensor_spi_init(void)
{
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &sensor_spi_bus_config, 1));
    ESP_ERROR_CHECK(spi_bus_add_device(SPI3_HOST, &sensor_spi_device_config, &sensor_spi_device));
    ESP_LOGI(TAG, "SPI device initialized");
}

// Function to configure the Pixart PAW3395 sensor.
/*
Please note that upon chip start-up per the recommended Power-Up Sequence, the chip is set to High Performance
Mode as default.
*/
static void sensor_configure(void)
{
    // Configure the sensor's first set of registers.
    for (int i = 0; i < sizeof(sensor_prog_seq_first) / sizeof(sensor_prog_seq_first[0]); i++)
    {
        sensor_write_register(sensor_prog_seq_first[i][0], sensor_prog_seq_first[i][1]);
        // Wait
        vTaskDelay(pdUS_TO_TICKS(SENSOR_WRITE_DELAY_US));
    }

    // Wait for the sensor to initialize.
    int attempts = 0;
    while (attempts < SENSOR_0x6C_READ_ATTEMPTS)
    {
        uint8_t response[1];
        sensor_read_register(0x6C, response, sizeof(response));
        vTaskDelay(pdMS_TO_TICKS(SENSOR_0x6C_READ_INTERVAL_MS));
        if (response == SENSOR_0x6C_READ_VALUE)
        {
            break;
        }
        attempts++;
    }

    if (attempts == SENSOR_0x6C_READ_ATTEMPTS)
    {
        ESP_LOGE(TAG, "Failed to initialize sensor");
        // Configure the sensor's fail registers.
        for (int i = 0; i < sizeof(sensor_prog_seq_0x6C_fail) / sizeof(sensor_prog_seq_0x6C_fail[0]); i++)
        {
            sensor_write_register(sensor_prog_seq_0x6C_fail[i][0], sensor_prog_seq_0x6C_fail[i][1]);
            // Wait
            vTaskDelay(pdUS_TO_TICKS(SENSOR_WRITE_DELAY_US));
        }
    }

    // Configure the sensor's second set of registers.
    for (int i = 0; i < sizeof(sensor_prog_seq_second) / sizeof(sensor_prog_seq_second[0]); i++)
    {
        sensor_write_register(sensor_prog_seq_second[i][0], sensor_prog_seq_second[i][1]);
        // Wait
        vTaskDelay(pdUS_TO_TICKS(SENSOR_WRITE_DELAY_US));
    }

    ESP_LOGI(TAG, "SPI device configured");
}

// Initialize the IO pins for the sensor.
/*
6.1 Power on Sequence
Although the chip performs an internal power up self-reset, it is still recommended that the Power_Up_Reset
register is written every time power is applied. The recommended chip power up sequence is as follows:
1. Apply power to VDD and VDDIOin any order, with a delay of no more than 100ms in between each supply.
Ensure all supplies are stable.
2. Wait for at least 50 ms.
3. Drive NCS high, and then low to reset the SPI port.
4. Write 0x5A to Power_Up_Reset register (or alternatively toggle the NRESET pin).
5. Wait for at least 5ms.
6. Load Power-up initialization register setting.
7. Read registers 0x02, 0x03, 0x04, 0x05 and 0x06 one time regardless of the motion bit state.
*/
static void sensor_init(void)
{
    ESP_ERROR_CHECK(gpio_config(&sensor_ncs_config));
    ESP_ERROR_CHECK(gpio_config(&sensor_mosi_config));
    ESP_ERROR_CHECK(gpio_config(&sensor_sclk_config));
    ESP_ERROR_CHECK(gpio_config(&sensor_miso_config));
    ESP_ERROR_CHECK(gpio_config(&sensor_nreset_config));
    ESP_ERROR_CHECK(gpio_config(&sensor_motion_config));
    ESP_ERROR_CHECK(gpio_config(&sensor_pwr_en_config));

    // Excess delays are assumed fine in this sequence.

    // Power up the sensor.
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_39, 1));
    // Wait for the sensor to power up.
    vTaskDelay(pdMS_TO_TICKS(SENSOR_WAKEUP_DELAY_MS));
    // Reset the SPI port.
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_27, 1));
    vTaskDelay(pdUS_TO_TICKS(SENSOR_RESET_DELAY_US));
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_27, 0));
    vTaskDelay(pdUS_TO_TICKS(SENSOR_RESET_DELAY_US));
    // Toggle the reset pin.
    // The NRESET pin needs to be asserted (held to logic 0) for at least
    // 100 ns duration for the chip to reset.
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_31, 1));
    vTaskDelay(pdUS_TO_TICKS(SENSOR_RESET_DELAY_US));
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_31, 0));
    vTaskDelay(pdUS_TO_TICKS(SENSOR_RESET_DELAY_US));
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_31, 1));
    // Wait for the sensor/spi to reset.
    vTaskDelay(pdMS_TO_TICKS(5));
    // Load the power-up initialization register settings.
    sensor_configure();
    // Read registers 0x02, 0x03, 0x04, 0x05 and 0x06 one time regardless of the motion bit state.
    uint8_t regs[5] =
        {0x02, 0x03, 0x04, 0x05, 0x06};
    for (int i = 0; i < 5; i++)
    {
        uint8_t reg = regs[i];
        uint8_t response[1];
        sensor_read_register(reg, response, sizeof(response));
        ESP_LOGI(TAG, "Register 0x%02X: 0x%02X", reg, response);
        // Wait
        vTaskDelay(pdUS_TO_TICKS(SENSOR_READ_DELAY_US));
    }

    // Wait for the sensor to initialize.
    vTaskDelay(pdMS_TO_TICKS(SENSOR_MOTION_DELAY_MS));

    ESP_LOGI(TAG, "USB sensor_init");
}

// Sensor task
static void sensor_task(void *arg)
{
    while (1)
    {
        // Time stamp to ensure we do not exceed REPORT_RATE_MS
        uint32_t start_us = esp_timer_get_time();
        // Process motion data
        process_motion_data();
        // Lower NCS.
        ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_27, 0));
        // Wait for tNCS-SCLK
        vTaskDelay(pdNS_TO_TICKS(SENSOR_NCS_SCLK_DELAY_NS));
        // Read the motion data from the Pixart PAW3395 sensor.
        sensor_read_motion_burst();
        // After the burst transmission is complete, the
        // microcontroller must raise the NCS line for at least tBEXIT to terminate burst mode. The serial port is not available for
        // use until it is reset with NCS, even for a second burst transmission.
        ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_27, 1));
        // Wait until SENSOR_BURST_EXIT_DELAY_NS has elapsed
        vTaskDelay(pdNS_TO_TICKS(SENSOR_BURST_EXIT_DELAY_NS));
        // Time stamp to ensure we do not exceed REPORT_RATE_MS
        // Wait until REPORT_RATE_MS has elapsed
        uint32_t diff_us = (esp_timer_get_time() - start_us) / 1000;
        if (diff_us < REPORT_RATE_US)
        {
            vTaskDelay(pdUS_TO_TICKS(REPORT_RATE_US - diff_us));
        }
    }
}

/********* Application ***************/

static void app_main(void)
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
    while (1)
    {
    }
}
