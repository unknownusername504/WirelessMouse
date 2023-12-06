#include "header/scroll_wheel.h"

static void swheel_a_isr(void *arg);
static void swheel_b_isr(void *arg);
static void swheel_task_report(void);
static void swheel_speed_adjust(bool swheel_event);

/************* IO Configs ****************/

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
void swheel_init(void)
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
void swheel_task(void *arg)
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