#include "header/motion_sensor.h"

#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "hal/spi_types.h"

static void add_motion_data_to_buffer(uint8_t motion_x, uint8_t motion_y, uint32_t timestamp);
static MotionData read_oldest_motion_data_from_buffer(void);
static void process_motion_data(void);
static void sensor_read_register(uint8_t address, uint8_t *response, size_t response_size);
static void sensor_read_motion_burst(void);
static void sensor_write_register(uint8_t address, uint8_t value);
static void sensor_configure(void);

/************* IO Configs ****************/

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
/*
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
*/

/*
Low Power Mode
*/
/*
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
*/

/*
Office Mode
*/
/*
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
*/

/*
Corded Gaming Mode
*/
/*
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
*/

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

    ESP_ERROR_CHECK(spi_device_transmit(sensor_spi_device, (spi_transaction_t *)(&transaction_ext)));

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
void sensor_spi_init(void)
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
        if (response[0] == SENSOR_0x6C_READ_VALUE)
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
void sensor_init(void)
{
    // Initialize the SPI device for the sensor.
    sensor_spi_init();

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
        ESP_LOGI(TAG, "Register 0x%02X: 0x%02X", reg, response[0]);
        // Wait
        vTaskDelay(pdUS_TO_TICKS(SENSOR_READ_DELAY_US));
    }

    // Wait for the sensor to initialize.
    vTaskDelay(pdMS_TO_TICKS(SENSOR_MOTION_DELAY_MS));

    ESP_LOGI(TAG, "USB sensor_init");
}

// Sensor task
void sensor_task(void *arg)
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
        uint32_t diff_us = (esp_timer_get_time() - start_us);
        if (diff_us < REPORT_RATE_US)
        {
            vTaskDelay(pdUS_TO_TICKS(REPORT_RATE_US - diff_us));
        }
    }
}