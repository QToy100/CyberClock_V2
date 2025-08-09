#include "main.h"
// main.cpp
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2s.h"
#include "esp_err.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "CyberClock.h"
#include "webserver.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "config.h"
#include "settings.h"
#include "wifi_board.h"   
#include "wifi_station.h"
#include "esp_timer.h"
#include "esp_wifi.h" 
#include "ssid_manager.h" 
#include "esp_sntp.h"


static const char *TAG = "MAIN";

bool ClockState_ = true;

// I2C bus and PCA9685 handle
static i2c_master_bus_handle_t bus_handle = nullptr;
static i2c_master_dev_handle_t pca9685_dev_m = nullptr;
static i2c_master_dev_handle_t pca9685_dev_h = nullptr;

// Flag to indicate if server time has been obtained
SemaphoreHandle_t server_time_ready_semaphore = nullptr; // Semaphore to indicate if server time is ready

static void InitNVS() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
}

// static void InitCodec() {
//     Es8311AudioCodec codec(
//         nullptr,
//         I2C_NUM_0,
//         16000,
//         16000,
//         GPIO_NUM_NC,
//         GPIO_NUM_NC,
//         GPIO_NUM_NC,
//         GPIO_NUM_NC,
//         GPIO_NUM_NC,
//         GPIO_NUM_NC,
//         0x18,
//         true
//     );
//     codec.EnableOutput(true);
//     codec.SetOutputVolume(80);
// }

static void InitParams(){
    server_time_ready_semaphore = xSemaphoreCreateBinary();
    if (server_time_ready_semaphore == nullptr) {
        ESP_LOGE(TAG, "Failed to create server_time_ready_semaphore");
    }

    // Must initialize network stack and event loop first
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
}

// Set system timezone and minute offset
void ApplyTimezoneAndOffset(struct timeval* tv, int tz_hour, int tz_min) {
    // Print tz_hour and tz_min
    ESP_LOGI(TAG, "Applying timezone: %d hours, %d minutes", tz_hour, tz_min);
    if (!tv) return;
    char tz_str[16];
    if (tz_hour == 0) {
        strcpy(tz_str, "UTC0");
    } else if (tz_hour > 0) {
        snprintf(tz_str, sizeof(tz_str), "UTC-%d", tz_hour); // East zone is negative
    } else {
        snprintf(tz_str, sizeof(tz_str), "UTC+%d", -tz_hour); // West zone is positive
    }
    setenv("TZ", tz_str, 1);
    tzset();
    if (tz_min != 0) {
        tv->tv_sec += tz_min * 60;
    }
    tv->tv_usec = 0;
    settimeofday(tv, NULL);
}

// Global timezone and minute offset configuration
int timezone_offset = 8;
int timezone_offset_minute = 0;

// Set timezone (hour)
void SetTimezoneOffset(int offset) {
    timezone_offset = offset;
    // Save to NVS
    Settings settings("cyberclock", true);
    settings.SetInt("tz", offset);
    // Take effect immediately
    struct timeval tv;
    gettimeofday(&tv, NULL);
    tv.tv_sec = tv.tv_sec - timezone_offset_minute * 60; // Subtract minute offset to avoid repeated accumulation
    ApplyTimezoneAndOffset(&tv, timezone_offset, timezone_offset_minute);
}
// Set minute offset
void SetTimezoneOffsetMinute(int offset) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    tv.tv_sec = tv.tv_sec - timezone_offset_minute * 60; // Subtract old minute offset
    timezone_offset_minute = offset;
    // Save to NVS
    Settings settings("cyberclock", true);
    settings.SetInt("mtz", offset);
    ApplyTimezoneAndOffset(&tv, timezone_offset, timezone_offset_minute);
}


void cyberclock_task(void* arg) {
    // Only reference, no need to assign variable name
    (void)CyberClock::GetInstance();
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void Enable_5V_Output(int enable) {
    // Enable or disable the 5V output
    // Set GPIO6 level
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE; // Disable interrupt
    io_conf.mode = GPIO_MODE_OUTPUT; // Set as output mode
    io_conf.pin_bit_mask = (1ULL << GPIO_NUM_6); // Set GPIO6
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE; // Disable pull-down
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE; // Disable pull-up
    ESP_ERROR_CHECK(gpio_config(&io_conf)); // Configure GPIO6
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_6, enable)); // Set GPIO6 level
    ESP_LOGI(TAG, "5V output %s", enable ? "enabled" : "disabled");
}

void InitButtons() {
    // Initialize K1 button GPIO
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE; // Disable interrupt
    io_conf.mode = GPIO_MODE_INPUT; // Set as input mode
    io_conf.pin_bit_mask = (1ULL << K1_BUTTON_GPIO); // Set K1 button GPIO
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE; // Disable pull-down
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE; // Enable pull-up
    ESP_ERROR_CHECK(gpio_config(&io_conf)); // Configure K1 button GPIO

    ESP_LOGI(TAG, "Buttons initialized on K1_BUTTON_GPIO");
}

void K1_ButtonTask(void*) {
    int last_level = 1;
    int press_time = 0;
    while (1) {
        int level = gpio_get_level(K1_BUTTON_GPIO);
        if (last_level == 1 && level == 0) { // Pressed
            press_time = esp_timer_get_time();
        }
        if (last_level == 0 && level == 1) { // Released
            int duration = (esp_timer_get_time() - press_time) / 1000; // ms
            if (duration >= 1000) {
                ESP_LOGI(TAG, "K1 Long Press");
                // Clear WiFi config and restart
                ESP_LOGI(TAG, "Erasing WiFi config and restarting...");
                auto& ssidManager = SsidManager::GetInstance();
                ssidManager.Clear(); // Clear SSID list
                vTaskDelay(pdMS_TO_TICKS(500)); // Wait for operation to complete
                esp_restart(); // Restart board
            } else if (duration > 20) {
                // Single click
                if (ClockState_) {
                    ESP_LOGI(TAG, "K1 Click,Clock is shutting down...");
                    CyberClock::GetInstance().ShutdownClock();
                    ClockState_ = false;
                } else {
                    ESP_LOGI(TAG, "K1 Click,Clock is 8888");
                    CyberClock::GetInstance().SetNumber(8,8,8,8);
                    ClockState_ = true;
                }
            }
        }
        last_level = level;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}



void SyncTime()
{
    ESP_LOGI(TAG, "Starting SNTP time sync...");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "ntp.aliyun.com");
    sntp_init();

    // Wait for time sync (timeout 5 seconds)
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 10;
    while (retry < retry_count) {
        time(&now);
        localtime_r(&now, &timeinfo);
        if (timeinfo.tm_year > (2016 - 1900)) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
        retry++;
    }
    if (retry == retry_count) {
        ESP_LOGW(TAG, "SNTP sync timeout, time not set");
        return;
    }

    // Adjust timezone and minute offset
    struct timeval tv;
    gettimeofday(&tv, NULL);
    ApplyTimezoneAndOffset(&tv, timezone_offset, timezone_offset_minute);

    // Print system time hh:mm:ss
    time_t now2 = tv.tv_sec;
    struct tm timeinfo2;
    localtime_r(&now2, &timeinfo2);
    ESP_LOGW(TAG, "Time synced and adjusted to: %02d:%02d:%02d",
             timeinfo2.tm_hour, timeinfo2.tm_min, timeinfo2.tm_sec);

    // Give the semaphore to indicate time is ready
    xSemaphoreGive(server_time_ready_semaphore);
}

extern "C" void app_main(void)
{
    ESP_LOGW(TAG, "CyberClock_V2 started, version: %s", FIRMWARE_VERSION);
    InitParams(); // Initialize environment variables and parameters
    InitButtons(); // Initialize buttons
    Enable_5V_Output(1); // Enable 5V output for servos
    InitNVS(); // Initialize NVS (non-volatile storage)
    //InitCodec(); // Initialize Codec using ES8311AudioCodec class

    xTaskCreate(cyberclock_task, "cyberclock_task", 8192, NULL, 5, NULL);

    // Create K1 button detection task
    xTaskCreate(K1_ButtonTask, "K1_ButtonTask", 4096, NULL, 10, NULL);

    // New WiFi/network initialization, managed by wifi_board
    WifiBoard& wifi_board = WifiBoard::GetInstance();
    wifi_board.StartNetwork(); // Start all network functions: WiFi, provisioning, AP, DNS hijack, auto popup, etc.

    StartWebServer(); // Start WebServer, ensure network is ready

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
