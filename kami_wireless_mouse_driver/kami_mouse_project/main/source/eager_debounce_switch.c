#include "header/eager_debounce_switch.h"

static void mmb_isr(void *arg);
static void smb4_isr(void *arg);
static void smb5_isr(void *arg);
static void mmb_debounce_task_report(void);
static void smb4_debounce_task_report(void);
static void smb5_debounce_task_report(void);

/************* IO Configs ****************/

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
void button_debounce_init(void)
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
void button_debounce_task(void *arg)
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