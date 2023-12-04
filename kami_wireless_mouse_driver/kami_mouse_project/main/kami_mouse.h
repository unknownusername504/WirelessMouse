#pragma once

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

static const char *TAG = "KamiKomplexMouse";

// IDE doesn't like stdint.h, so we need to define the types here.
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef long unsigned int uint32_t;
typedef unsigned long long uint64_t;

#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)

/**************** Main Mouse Buttons ****************/

// Enum for a handshake between the ISR and the task.
typedef enum
{
	LATCH_EVENT_CLEAR,
	LATCH_EVENT_SET,
	LATCH_EVENT_READ,
} latch_event_t;

// Enum for state as mouse_down or mouse_up.
typedef enum
{
	MOUSE_BUTTON_UP,
	MOUSE_BUTTON_DOWN,
} mouse_button_state_t;

/**************** Side Mouse Buttons ****************/

// This needs to be sufficiently long to debounce the buttons and for the report to be sent.
// But it also needs to be short enough to not cause the mouse to lag.
// These butttons are not high preformance, so 10ms should be sufficient.
#define STABLE_POLL_TIME_MS 10

/**************** Scroll Wheel ****************/

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

/**************** Motion Sensor ****************/

// The sensor is configured to use SPI mode 3.
#define SENSOR_SPI_MODE 3
// The sensor is configured to use a clock speed of 10MHz.
#define SENSOR_SPI_CLOCK_SPEED_HZ SPI_MASTER_FREQ_10M

// Motion data struct, 2 motion bytes and a timestamp in 12 packet bursts so allow for 2 bursts to be stored.
#define MOTION_DATA_BUFFER_SIZE 24

typedef struct
{
	uint8_t motion_x;
	uint8_t motion_y;
	uint32_t timestamp;
} MotionData;

/*
10 MHz = 100 ns(p) = 0.1 μs(p)
Motion Delay After Reset (tMOT-RST) : 50 ms - From reset to valid motion, assuming motion is present
Shutdown (tSTDWN) : 500 ms - From Shutdown mode active to low current
Wake from Shutdown (tWAKEUP) : 50 ms - From Shutdown mode inactive to valid motion. A RESET must be asserted after a shutdown.
MISO Rise Time (tr-MISO) : 6 ns
MISO Fall Time (tf-MISO) : 6 ns
MISO Delay After SCLK (tDLY-MISO) : 35 ns - From SCLK falling edge to MISO data valid
MISO Hold Time (thold-MISO) : 25 ns - Data held until next falling SCLK edge
MOSI Hold Time (thold-MOSI) : 25 ns - Amount of time data is valid after SCLK rising edge
MOSI Setup Time (tsetup-MOSI) : 25 ns - From data valid to SCLK rising edge
SPI Time Between Write Commands (tSWW) : 5 μs - From rising SCLK for last bit of the first data byte, to rising SCLK for last bit of the second data byte
SPI Time Between Write and Read Commands (tSWR) : 5 μs - From rising SCLK for last bit of the first data byte, to rising SCLK for last bit of the second address byte
SPI Time Between Read and Subsequent Commands (tSRW/tSRR) : 2 μs - From rising SCLK for last bit of the first data byte, to falling SCLK for the first bit of the address byte of the next command
SPI Read Address-Data Delay (tSRAD) : 2 μs - From rising SCLK for last bit of the address byte, to falling SCLK for first bit of data being read
NCS Inactive After Motion Burst (tBEXIT) : 500 ns - Minimum NCS inactive time after motion burst before next SPI usage
NCS To SCLK Active (tNCS-SCLK) : 120 ns - From last NCS falling edge to first SCLK rising edge
SCLK To NCS Inactive For Read Operation (tSCLK-NCS) : 120 ns - From last SCLK rising edge to NCS rising edge, for valid MISO data transfer
SCLK To NCS Inactive For Write Operation (tSCLK-NCS) : 1 μs - From last SCLK rising edge to NCS rising edge, for valid MOSI data transfer
*/

#define T_MOT_RST_MS 50
#define T_STDWN_MS 500
#define T_WAKEUP_MS 50
#define T_MISO_RISE_NS 6
#define T_MISO_FALL_NS 6
#define T_DLY_MISO_NS 35
#define T_HOLD_MISO_NS 25
#define T_HOLD_MOSI_NS 25
#define T_SETUP_MOSI_NS 25
#define T_SWW_US 5
#define T_SWR_US 5
#define T_SRW_US 2
#define T_SRR_US 2
#define T_SRAD_US 2
#define T_BEXIT_NS 500
#define T_NCS_SCLK_NS 120
#define T_SCLK_NCS_READ_NS 120
#define T_SCLK_NCS_WRITE_NS 1000

// Time for sensor to wake up.
#define SENSOR_WAKEUP_DELAY_MS T_WAKEUP_MS

// Time between reset and valid motion.
#define SENSOR_MOTION_DELAY_MS T_MOT_RST_MS

// The NRESET pin needs to be asserted (held to logic 0) for at least 100 ns duration for the chip to reset.
// We round up to 1us to be safe.
#define SENSOR_RESET_DELAY_US 1

// Maximum data valid time of slave.
#define SENSOR_INPUT_DELAY_NS (T_MISO_RISE_NS + T_DLY_MISO_NS + T_HOLD_MISO_NS)

// Time between end of one write and start of next operation.
#define SENSOR_WRITE_DELAY_US max(T_SWW_US, T_SWR_US)

// Time between end of one read and start of next operation.
#define SENSOR_READ_DELAY_US max(T_SRW_US, T_SRR_US)

// Time between setup of read and start of read.
#define SENSOR_READ_SETUP_US T_SRAD_US

// Number of dummy bits needed to be sent to the sensor to read the motion data.
#define SENSOR_DUMMY_BITS (SENSOR_READ_SETUP_US * (SENSOR_SPI_CLOCK_SPEED_HZ / 1000000))

// After the burst transmission is complete, the
// microcontroller must raise the NCS line for at least tBEXIT to terminate burst mode. The serial port is not available for
// use until it is reset with NCS, even for a second burst transmission.
#define SENSOR_BURST_EXIT_DELAY_NS T_BEXIT_NS

#define SENSOR_NCS_SCLK_DELAY_NS T_NCS_SCLK_NS

/*
Wait for 1ms
Read register 0x6C at 1ms interval until value
0x80 is obtained or read up to 60 times, this
register read interval must be carried out at 1ms
interval with timing tolerance of ±1%
If value of 0x80 is not obtained from
register0x6C after 60 times:
a. Write register 0x7F with value 0x14
b. Write register 0x6C with value 0x00
c. Write register 0x7F with value 0x00
*/
#define SENSOR_0x6C_READ_ATTEMPTS 60
#define SENSOR_0x6C_READ_INTERVAL_MS 1
#define SENSOR_0x6C_READ_INTERVAL_TOLERANCE_MS 1
#define SENSOR_0x6C_READ_VALUE 0x80

// Enum for different mouse modes
typedef enum
{
	MOUSE_MODE_HPM = 0, // High performance mode, default
	MOUSE_MODE_LPM = 1, // Low power mode
	MOUSE_MODE_WRK = 2, // Office mode
	MOUSE_MODE_CRD = 3, // Corded gaming mode
} MouseMode;

// Max 4000Hz
#define REPORT_RATE_US 250

// Function to convert US to ticks
#define pdUS_TO_TICKS(xTimeInUs) ((TickType_t)((TickType_t)(pdMS_TO_TICKS(1) * 1000) * xTimeInUs))

// Function to convert NS to ticks
#define pdNS_TO_TICKS(xTimeInNs) ((TickType_t)((TickType_t)(pdMS_TO_TICKS(1) * 1000000) * xTimeInNs))


// Pre declarations
// Non static functions visible outside file
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance);
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen);
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize);
static void mb_latch_init(void);
static mouse_button_state_t calculate_lmb_state(void);
static mouse_button_state_t calculate_rmb_state(void);
static void lmb_isr(void *arg);
static void rmb_isr(void *arg);
static void lmb_latch_task_report(void);
static void rmb_latch_task_report(void);
static void mb_latch_task(void *arg);
static void button_debounce_init(void);
static void mmb_isr(void *arg);
static void smb4_isr(void *arg);
static void smb5_isr(void *arg);
static void mmb_debounce_task_report(void);
static void smb4_debounce_task_report(void);
static void smb5_debounce_task_report(void);
static void button_debounce_task(void *arg);
static void swheel_init(void);
static void swheel_a_isr(void *arg);
static void swheel_b_isr(void *arg);
static void swheel_task_report(void);
static void swheel_speed_adjust(bool swheel_event);
static void swheel_task(void *arg);
static void add_motion_data_to_buffer(uint8_t motion_x, uint8_t motion_y, uint32_t timestamp);
static MotionData read_oldest_motion_data_from_buffer(void);
static void sensor_start_motion_burst(void);
static void process_motion_data(void);
static void sensor_read_register(uint8_t address, uint8_t *response, size_t response_size);
static void sensor_read_motion_burst(void);
static void sensor_write_register(uint8_t address, uint8_t value);
static void sensor_spi_init(void);
static void sensor_configure(void);
static void sensor_init(void);
static void sensor_task(void *arg);
static void app_main(void);