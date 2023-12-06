#include "header/latch_switch.h"

static mouse_button_state_t calculate_lmb_state(void);
static mouse_button_state_t calculate_rmb_state(void);
static void lmb_isr(void *arg);
static void rmb_isr(void *arg);
static void lmb_latch_task_report(void);
static void rmb_latch_task_report(void);

/************* IO Configs ****************/

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
void mb_latch_init(void)
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
void mb_latch_task(void *arg)
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