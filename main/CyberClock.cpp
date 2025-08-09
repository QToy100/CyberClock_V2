#include <esp_log.h>
#include <vector>
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <ctime>
#include <string>
#include <esp_sntp.h>
#include <esp_log.h>
#include <time.h>
#include "esp_http_server.h"
#include "settings.h"
#include "cyberclock.h"
#include "esp_wifi.h"

#include <algorithm>

extern SemaphoreHandle_t server_time_ready_semaphore;

#define TAG "CyberClock"

bool is_wifi_connected() {
    wifi_ap_record_t ap_info;
    return esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK;
}
 
struct ServoState {
    int channel;            // Servo channel
    int current_position;   // Current servo position
    int target_position;    // Target servo position
    int front_channel = -1; // Preceding task channel, -1 means no dependency
    int state = -1;         // Task state: -1 not executed, 0 executing, 1 finished
    bool smooth = false;    // Smooth movement
};

static bool servo_driver_available_ = false; // Is servo driver available

std::vector<ServoState> servo_states_;

// Function GetTargetPosition: sets clock_target_position_ based on abcd digits
// Parameter 0xA means all off, 0xB means idle
void CyberClock::GetTargetPosition(int a, int b, int c, int d) {
    // Set target positions for each digit
    for (int i = 0; i < 7; i++) {
        clock_target_position_[i] = digits[a][i] ? segmentOn[i] + servo_offsets_[i] : segmentOff[i] + servo_offsets_[i];
        clock_target_position_[i + 7] = digits[b][i] ? segmentOn[i + 7] + servo_offsets_[i + 7] : segmentOff[i + 7] + servo_offsets_[i + 7];
        clock_target_position_[i + 14] = digits[c][i] ? segmentOn[i + 14] + servo_offsets_[i + 14] : segmentOff[i + 14] + servo_offsets_[i + 14];
        clock_target_position_[i + 21] = digits[d][i] ? segmentOn[i + 21] + servo_offsets_[i + 21] : segmentOff[i + 21] + servo_offsets_[i + 21];
    }

    // Check for out-of-range values
    for(int i=0;i<28;i++)
    {
        if(clock_target_position_[i] < 100)clock_target_position_[i] = 100; // Minimum value
        if(clock_target_position_[i] > 550)clock_target_position_[i] = 550; // Maximum value   
    }
}

// Initialize I2C bus
bool CyberClock::InitI2CBus() {
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = GPIO_NUM_17,
        .scl_io_num = GPIO_NUM_18,
        .clk_source = I2C_CLK_SRC_DEFAULT,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C bus: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "I2C bus initialized successfully");
    return true;
}

// Initialize PCA9685 chip
bool CyberClock::InitPCA9685(i2c_master_dev_handle_t* dev_handle, uint8_t addr) {

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400000,
    };

    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device 0x%02X: %s", addr, esp_err_to_name(ret));
        return false;
    }

    // Initialize registers
    uint8_t prescale = (uint8_t)(25000000 / (4096 * 50) - 1);
    if (!SafeI2CWrite(*dev_handle, 0x00, 0x00) ||  // MODE1
        !SafeI2CWrite(*dev_handle, 0x01, 0x04) ||  // MODE2
        !SafeI2CWrite(*dev_handle, 0x00, 0x10) ||  // Sleep mode
        !SafeI2CWrite(*dev_handle, 0xFE, prescale) ||
        !SafeI2CWrite(*dev_handle, 0x00, 0x80)) {  // Wake up
        ESP_LOGE(TAG, "Failed to initialize PCA9685 at 0x%02X", addr);
        return false;
    }

    ESP_LOGI(TAG, "PCA9685 initialized at 0x%02X", addr);
    return true;
}

void CyberClock::InitializeCurrentPosition()
{
    // Initialize current servo positions
    for (int i = 0; i < 28; i++) {
        clock_current_position_[i] = segmentOff[i] + servo_offsets_[i]; // Initial position is off
    }
    ESP_LOGI(TAG, "Current servo positions initialized");
}

// Initialize servo driver
bool CyberClock::InitializeServos() {
    if (!InitI2CBus()) {
        return false;
    }

    for (int retry = 0; retry < MAX_I2C_RETRIES; retry++) {
        if (InitPCA9685(&dev_handle_h, PCA9685_ADDR_H) &&
            InitPCA9685(&dev_handle_m, PCA9685_ADDR_M)) {
            servo_driver_available_ = true;
            ESP_LOGI(TAG, "Servo driver initialized successfully");
            return true;
        }

        ESP_LOGW(TAG, "Retrying PCA9685 initialization...");
    }

    servo_driver_available_ = false;
    ESP_LOGE(TAG, "Failed to initialize servo driver after %d attempts", MAX_I2C_RETRIES);
    return false;
}

// Check if in sleep time range
void CyberClock::CheckSleepTime() {

    // If sleep mode is not enabled, return false
    if(!sleep_clock_enable_)return;

    // Get current time
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    int current_hour = timeinfo->tm_hour;
    int current_minute = timeinfo->tm_min;
    
    // Check if entering sleep time
    if(!sleep_clock_enable_)
        return;

    // If current time matches sleep start and not in idle, enter sleep (idle)
    if( current_hour == sleep_start_hour_ && 
        current_minute == sleep_start_minute_ && 
        current_mode_ != MODE_03_IDLE) {
        ESP_LOGW(TAG, "Entering sleep mode at %02d:%02d", current_hour, current_minute);
        IdleClock(); // Enter sleep mode
        return;
    }

    // If current time matches sleep end and is idle, exit sleep (normal clock)
    if( current_hour == sleep_end_hour_ && 
        current_minute == sleep_end_minute_ && 
        current_mode_ == MODE_03_IDLE) {
        ESP_LOGW(TAG, "Exiting sleep mode at %02d:%02d", current_hour, current_minute);
        ShowTime(); // Exit sleep mode
        return;
    }
}

// Timer callback (with log rate control)
void CyberClock::TimerCallback(TimerHandle_t xTimer) {
    CyberClock* clock = static_cast<CyberClock*>(pvTimerGetTimerID(xTimer));
    static uint32_t last_warn_time = 0;
    const uint32_t now = xTaskGetTickCount();

    // If servo driver is not available, limit log rate and exit
    if (!servo_driver_available_) {
        if (now - last_warn_time > pdMS_TO_TICKS(20000)) { // Log every 20 seconds
            ESP_LOGW(TAG, "Servo driver unavailable");
            last_warn_time = now;
        }
        return; // Ensure OnTimerTick is not called
    }

    // Call timer tick logic
    clock->OnTimerTick();
}

void CyberClock::AddServoTask(int ch, int start_position, int to_position, int front_channel , bool smooth ) {
    // Validate channel range
    if (ch < 0 || ch > 27) {
        ESP_LOGE(TAG, "Invalid channel: %d", ch);
        return;
    }

    // Validate position range
    if (start_position < 100 || start_position > 550 || to_position < 100 || to_position > 550) {
        ESP_LOGE(TAG, "Invalid position at channel %d: start_position=%d, to_position=%d", ch, start_position, to_position);
        start_position = std::clamp(start_position, 100, 550);
        to_position = std::clamp(to_position, 100, 550);
    }

    // Create task
    ServoState task = {ch, start_position, to_position, front_channel, -1, smooth};

    // Lock task array
    if (xSemaphoreTake(task_queue_mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Add task to array
        servo_states_.push_back(task);
        ESP_LOGI(TAG, "AddTask: channel=%d, start_pos=%d, to_pos=%d, front_ch=%d, smooth=%d",
                 ch, start_position, to_position, front_channel, smooth);

        // Unlock
        xSemaphoreGive(task_queue_mutex_);
    } else {
        ESP_LOGE(TAG, "Failed to acquire task queue mutex");
    }
}

void CyberClock::TaskUpdateDisplay(int a, int b, int c, int d, bool smooth) {
    if (!servo_driver_available_) return;

    // Acquire semaphore
    if (xSemaphoreTake(display_mutex_, portMAX_DELAY) == pdTRUE) {
        //ESP_LOGW(TAG, "== Task UpdateDisplay ==  %d %d : %d %d smooth=%d", a, b, c, d, smooth);

        // Get target positions
        GetTargetPosition(a, b, c, d);

        int midOffset;
        if(Servo_Mode_ == 0) midOffset = 120; 
        if(Servo_Mode_ == 1) midOffset = 50; // New version: smaller avoidance distance

        auto ProcessDigit = [&](int digit, int base_channel) {
            //ESP_LOGI(TAG, "Processing digit %d at base_channel %d", digit, base_channel);

            int middle_channel = base_channel + 6; // Middle pointer channel
            int adjCh1 = base_channel + 1;         // Adjacent pointer channel 1
            int adjCh5 = base_channel + 5;         // Adjacent pointer channel 5

            // Record avoidance positions for 1 and 5
            int adjCh1_position = 0;
            int adjCh5_position = 0;
            int front_channel = -1;

            //if(Servo_Mode_ == 0) // Old version needs avoidance
            {
                // Check if middle pointer needs avoidance
                if (abs(clock_current_position_[middle_channel] - clock_target_position_[middle_channel]) > 100) {
                    // If middle pointer is at SegmentOn, check adjacent pointers
                    if (clock_current_position_[adjCh1] == segmentOn[adjCh1] + servo_offsets_[adjCh1]) {
                        adjCh1_position = segmentOn[adjCh1] + midOffset + servo_offsets_[adjCh1];
                        front_channel = adjCh1; // If adjCh1 moves, set as dependency
                        AddServoTask(adjCh1, clock_current_position_[adjCh1], adjCh1_position, -1,smooth);
                    }
                    if (clock_current_position_[adjCh5] == segmentOn[adjCh5] + servo_offsets_[adjCh5]) {
                        adjCh5_position = segmentOn[adjCh5] - midOffset + servo_offsets_[adjCh5];
                        front_channel = adjCh5; // If adjCh5 moves, set as dependency
                        AddServoTask(adjCh5, clock_current_position_[adjCh5], adjCh5_position,-1, smooth);
                    }
                }
            }

            // Move middle pointer to target position
            if (clock_current_position_[middle_channel] != clock_target_position_[middle_channel]) {
                AddServoTask(middle_channel, clock_current_position_[middle_channel], clock_target_position_[middle_channel], front_channel,smooth);
            }

            // Process other pointers
            for (int i = 0; i < 7; i++) {
                if (i == 6) continue; // Skip middle pointer

                if (i == 1 && adjCh1_position != 0) {
                    // For 1, move from avoidance to target
                    AddServoTask(adjCh1, adjCh1_position,
                                 digits[digit][1] ? segmentOn[adjCh1] + servo_offsets_[adjCh1] : segmentOff[adjCh1] + servo_offsets_[adjCh1], middle_channel, smooth);
                } else if (i == 5 && adjCh5_position != 0) {
                    // For 5, move from avoidance to target
                    AddServoTask(adjCh5, adjCh5_position,
                                 digits[digit][5] ? segmentOn[adjCh5] + servo_offsets_[adjCh5] : segmentOff[adjCh5] + servo_offsets_[adjCh5], middle_channel, smooth);
                } else if (clock_current_position_[base_channel + i] != clock_target_position_[base_channel + i]) {
                    // For others, move from current to target
                    AddServoTask(base_channel + i, clock_current_position_[base_channel + i], clock_target_position_[base_channel + i],-1, smooth);
                }
            }
        };

        // Process a/b/c/d digits
        ProcessDigit(a, 0);  // Hour tens
        ProcessDigit(b, 7);  // Hour units
        ProcessDigit(c, 14); // Minute tens
        ProcessDigit(d, 21); // Minute units

        // Release semaphore
        xSemaphoreGive(display_mutex_);
    } else {
        ESP_LOGW(TAG, "Failed to acquire display mutex");
    }
}

void CyberClock::ShutdownClock() {
    if (!servo_driver_available_) return;

    ESP_LOGW(TAG, "*** SHUT DOWN CLOCK ***");
    current_mode_ = MODE_99_SHUTDOWN;
}

void CyberClock::UpdateIdleClock() {
    if (!servo_driver_available_) return;

    ESP_LOGW(TAG, "*** IDLE CLOCK ***");
    current_mode_ = MODE_03_IDLE;
}

void CyberClock::SetCountDown(int seconds)
{
    // Set countdown
    countdown_time_ = seconds;
    current_mode_ = MODE_02_SET_COUNTDOWN;
    ESP_LOGI(TAG, "Countdown started: %d minutes, %d seconds", seconds / 60, seconds % 60);
}

void CyberClock::SetTimer(int operation)
{
    // Reset timer
    if (operation == 0) { // 0 means cancel timer
        a_number_ = 0;
        b_number_ = 0;
        c_number_ = 0;
        d_number_ = 0;
        current_mode_ = MODE_01_SET_NUMBER;   
        timer_tick_ = 0; // Reset timer
        ESP_LOGW(TAG, "Timer reset to 00:00");
        return;
    }
    
    // Start timer
    if (operation == 1) {
        current_mode_ = MODE_04_SET_TIMER;
        ESP_LOGW(TAG, "Timer started");
        return;
    }

    // Stop timer
    if (operation == 2) {
        int minutes = timer_tick_ / 60;
        int seconds = timer_tick_ % 60;
        a_number_ = minutes / 10;
        b_number_ = minutes % 10;
        c_number_ = seconds / 10;
        d_number_ = seconds % 10;
        current_mode_ = MODE_01_SET_NUMBER; // Stop timer, return to normal clock
        ESP_LOGW(TAG, "Timer stopped");
        return;
    }
}

void CyberClock::SetNumber(int a,int b,int c,int d)
{
    a_number_ = a;
    b_number_ = b;
    c_number_ = c;
    d_number_ = d;
    current_mode_ = MODE_01_SET_NUMBER;   
    ESP_LOGI(TAG, "SetNumber: %d%d:%d%d", a_number_, b_number_, c_number_, d_number_);         
}

void CyberClock::SetServoSilentMode(bool mode)
{
    // Control mute mode
    if (mode) {
        ESP_LOGI(TAG, "Enabling mute mode");
        servo_mute_mode_ = true;
    } else {
        ESP_LOGI(TAG, "Disabling mute mode");
        servo_mute_mode_ = false;
    }
    Settings settings("cyberclock",true);
    settings.SetInt("servo_mute_en", servo_mute_mode_);
    ESP_LOGI(TAG, "Save to settings mute mode");
}

void CyberClock::ShowTime()
{
    // Restore to MODE_00_NORMAL_CLOCK
    current_mode_ = MODE_00_NORMAL_CLOCK;
    ESP_LOGI(TAG, "ShowTime: Restored to normal clock mode");
}

void CyberClock::SetSleepTime(bool mode, int start_hour, int start_minute, int end_hour, int end_minute)
{
    // Set sleep time
    sleep_clock_enable_ = mode;
    sleep_start_hour_ = start_hour;
    sleep_start_minute_ = start_minute; 
    sleep_end_hour_ = end_hour;
    sleep_end_minute_ = end_minute;

    Settings settings("cyberclock",true);
    settings.SetInt("sleep_clock_en", sleep_clock_enable_);
    settings.SetInt("sleep_s_hour", sleep_start_hour_);
    settings.SetInt("sleep_s_minute", sleep_start_minute_);
    settings.SetInt("sleep_e_hour", sleep_end_hour_);
    settings.SetInt("sleep_e_minute", sleep_end_minute_);

    ESP_LOGI(TAG, "Sleep time set to %02d:%02d - %02d:%02d", 
             sleep_start_hour_, sleep_start_minute_, 
             sleep_end_hour_, sleep_end_minute_);
}


void CyberClock::OnTimerTick() {

    // TimerCallback or OnTimerTick
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now); // localtime includes timezone and minute offset
    int now_sec = timeinfo->tm_hour * 3600 + timeinfo->tm_min * 60 + timeinfo->tm_sec;
    int current_hour = timeinfo->tm_hour;
    int current_minute = timeinfo->tm_min;

    // Check if alarm time is set
    if (alarm_time_ != -1 && now_sec / 60 == alarm_time_ / 60) {
        //Application::GetInstance().PlaySound(Lang::Sounds::P3_SUCCESS); // Replace with your sound
        alarm_time_ = -1; // Cancel after ringing
    }

    // Check if in sleep time
    CheckSleepTime();

    // Priority: shutdown > idle > set number > countdown > normal clock

    // 1. shutdown mode
    if(current_mode_ == MODE_99_SHUTDOWN) {
        TaskUpdateDisplay(0xA, 0xA, 0xA, 0xA); // Show all off
    }

    // 2. idle mode
    if(current_mode_ == MODE_03_IDLE) {
        TaskUpdateDisplay(0xB, 0xB, 0xB, 0xB); // Show idle
    }

    // 3. set number mode
    if(current_mode_ == MODE_01_SET_NUMBER) {
        TaskUpdateDisplay(a_number_, b_number_,c_number_, d_number_);
    }

    // 4. countdown mode
    if (current_mode_ == MODE_02_SET_COUNTDOWN) {
        if (countdown_time_ >= 0) {
            int minutes = countdown_time_ / 60;
            int seconds = countdown_time_ % 60;
            TaskUpdateDisplay(minutes / 10, minutes % 10, seconds / 10, seconds % 10);
            countdown_time_--;
            ESP_LOGI(TAG, "Countdown: %02d:%02d", minutes, seconds);
        } else {
            ESP_LOGI(TAG, "Countdown finished");
        }
    }

    // 5. set timer mode
    if (current_mode_ == MODE_04_SET_TIMER) {
        if (timer_tick_ >= 0) {
            timer_tick_++;
            int minutes = timer_tick_ / 60;
            int seconds = timer_tick_ % 60;
            TaskUpdateDisplay(minutes / 10, minutes % 10, seconds / 10, seconds % 10);
            ESP_LOGI(TAG, "Timer: %02d:%02d", minutes, seconds);
        } else {
            ESP_LOGI(TAG, "Timer finished");
        }
    }

    // 6. normal clock mode
    if (current_mode_ == MODE_00_NORMAL_CLOCK) {
        static int last_hour = -1;
        static int last_minute = -1;
        if (xSemaphoreTake(server_time_ready_semaphore, 0) == pdTRUE) {
            int display_hour = current_hour;
            if(clock_12_hour_) {
                // 12 hour mode
                // Convert to 12-hour format
                // If display_hour > 12, subtract 12; if display_hour == 0, set to 12
                // This is only for display purposes, not for internal logic
                if (display_hour > 12) {
                    display_hour -= 12; 
                } else if (display_hour == 0) {
                    display_hour = 12; 
                }
            }
            // Check if the current time is different from the last displayed time
            if (display_hour != last_hour || current_minute != last_minute) {
                ESP_LOGI(TAG, "Time changed: %02d:%02d -> %02d:%02d", last_hour, last_minute, display_hour, current_minute);
                last_hour = display_hour;
                last_minute = current_minute;
            }
            // Regardless of whether it changes, call TaskUpdateDisplay
            TaskUpdateDisplay(display_hour / 10, display_hour % 10,
                              current_minute / 10, current_minute % 10, servo_mute_mode_);
            xSemaphoreGive(server_time_ready_semaphore);
        } 
    }

    // 100. Test Mode
    if (current_mode_ == MODE_100_TEST) {
        static bool test_servo_state = false;
        if(test_servo_state == false) {
            TaskUpdateDisplay(8, 8, 8, 8);
        }else {
            TaskUpdateDisplay(0xA, 0xA, 0xA, 0xA);
        }
        test_servo_state = !test_servo_state; 
    }

    xSemaphoreGive(servo_mute_mode_semaphore_);
    ExecuteTask();
}

void CyberClock::Set12HourMode(bool mode){
    if (mode) {
        ESP_LOGI("CyberClock", "12-hour mode enabled");
    } else {
        ESP_LOGI("CyberClock", "24-hour mode enabled");
    }
    clock_12_hour_ = mode ? 1 : 0;
}



void CyberClock::LoadSettings(){
    Settings settings("cyberclock",true);
    servo_mute_mode_ = settings.GetInt("servo_mute_en", 0);
    sleep_clock_enable_ = settings.GetInt("sleep_clock_en", 0);
    sleep_end_hour_ = settings.GetInt("sleep_e_hour",7);
    sleep_end_minute_ = settings.GetInt("sleep_e_minute",0);
    sleep_start_hour_ = settings.GetInt("sleep_s_hour",22);
    sleep_start_minute_ = settings.GetInt("sleep_s_minute",0);
    timezone_offset_ = settings.GetInt("tz", 8); // read offset of hours 
    timezone_offset_minute_ = settings.GetInt("mtz", 0); // read offset of minutes 

    ESP_LOGI(TAG, "Loaded settings: servo_mute_mode=%d, sleep_clock_enable=%d, sleep_start_time=%02d:%02d, sleep_end_time=%02d:%02d, tz=%d, mtz=%d",
             servo_mute_mode_, sleep_clock_enable_, sleep_start_hour_, sleep_start_minute_, sleep_end_hour_, sleep_end_minute_, timezone_offset_, timezone_offset_minute_);  
}


void CyberClock::InitialMutexAndSemaphore()
{
    display_mutex_ = xSemaphoreCreateMutex();
    if (display_mutex_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create display mutex");
    }      

    task_queue_mutex_ = xSemaphoreCreateMutex();
    if (task_queue_mutex_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create task queue mutex");
    }
 
    task_ready_semaphore_ = xSemaphoreCreateBinary();
    if (task_ready_semaphore_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create task ready semaphore");
    }

    servo_mute_mode_semaphore_ = xSemaphoreCreateBinary(); 
    if (servo_mute_mode_semaphore_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create servo mute mode semaphore");
    }

    servo_task_queue_ = xQueueCreate(50, sizeof(ServoState)); 
    if (servo_task_queue_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create servo task queue");
    }
}

void CyberClock::DetectServoMode()
{
    gpio_set_pull_mode(GPIO_NUM_1, GPIO_PULLUP_ONLY);   // Set GPIO 1 pull-up
    gpio_set_direction(GPIO_NUM_1, GPIO_MODE_INPUT);
    // Delay 5ms to wait for GPIO stabilization
    vTaskDelay(pdMS_TO_TICKS(5));
    Servo_Mode_ = !gpio_get_level(GPIO_NUM_1); // Read the level state of GPIO 1
    ESP_LOGI(TAG, "Servo_Mode_ = %d", Servo_Mode_);
    
    if (Servo_Mode_ == 1) {
        // High level, copy B array
        ESP_LOGW(TAG, "Servo mode detected: B");
        memcpy(segmentOn, segmentOn_B, sizeof(segmentOn));
        memcpy(segmentOff, segmentOff_B, sizeof(segmentOff));
    } else {
        // Low level, copy A array
        ESP_LOGW(TAG, "Servo mode detected: A");
        memcpy(segmentOn, segmentOn_A, sizeof(segmentOn));
        memcpy(segmentOff, segmentOff_A, sizeof(segmentOff));
    }        
}

CyberClock::CyberClock() {
    ESP_LOGI(TAG, "CyberClock::CyberClock() begin...");

    DetectServoMode(); // Set servo position parameters
    InitialMutexAndSemaphore();
    InitializeServos();// Initialize servo driver (non-blocking)
    // Alarm
    alarm_time_ = -1;
    // Load settings
    LoadSettings();
    InitializeCurrentPosition(); // Initialize servo current position
    // Display 8888 first
    TaskUpdateDisplay(8, 8, 8, 8);// Initialize to 8888
    ESP_LOGD(TAG, "CyberClock initialized, display set to 8888");
    // Create timer (always create, regardless of whether driver is available)
    clock_timer_ = xTimerCreate(
        "ClockTimer", 
        pdMS_TO_TICKS(1000), 
        pdTRUE, 
        this, 
        TimerCallback
    );
    if (clock_timer_) {
        xTimerStart(clock_timer_, pdMS_TO_TICKS(100));
    }
    ESP_LOGW(TAG, "CyberClock::CyberClock() finished.");
}


void CyberClock::SetPWM(i2c_master_dev_handle_t dev_handle, uint8_t channel, uint16_t on, uint16_t off) {
    if (!servo_driver_available_ || !dev_handle) return;
    if (debug_servo_disabled_) return;

    // PCA9685 pin mapping between silk screen and actual index
    const int pwm_pin_mapping[14] = {8, 9, 10, 11, 12, 13, 0, 1, 2, 3, 4, 5, 6, 7};
    uint8_t real_channel = pwm_pin_mapping[channel];
    if (!SafeI2CWrite(dev_handle, 0x06 + 4 * real_channel, on & 0xFF) ||
        !SafeI2CWrite(dev_handle, 0x07 + 4 * real_channel, on >> 8) ||
        !SafeI2CWrite(dev_handle, 0x08 + 4 * real_channel, off & 0xFF) ||
        !SafeI2CWrite(dev_handle, 0x09 + 4 * real_channel, off >> 8)) {
        ESP_LOGD(TAG, "PWM set failed on channel %d", real_channel);
    }
}

// Safe I2C write
bool CyberClock::SafeI2CWrite(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint8_t value) {

    uint8_t write_buf[2] = {reg, value};
    for (int retry = 0; retry < MAX_I2C_RETRIES; retry++) {
        esp_err_t ret = i2c_master_transmit(dev_handle, write_buf, sizeof(write_buf), pdMS_TO_TICKS(100));
        if (ret == ESP_OK) {
            return true;
        }
        ESP_LOGW(TAG, "I2C write failed (retry %d): %s", retry, esp_err_to_name(ret));
    }

    ESP_LOGE(TAG, "I2C write failed after %d retries", MAX_I2C_RETRIES);
    return false;
}            



void CyberClock::ExecuteTask() {
    //ESP_LOGW(TAG, "Enter ExecuteTask");

    // Try to acquire servo operation semaphore
    if (xSemaphoreTake(servo_mute_mode_semaphore_, 0) == pdTRUE) {
        //ESP_LOGW(TAG, "Acquired servo_mute_mode_semaphore_, start executing tasks");

        // ESP_LOGW(TAG, "Tasks to execute:");
        // for (const auto& task : servo_states_) {
        //     ESP_LOGW(TAG, "channel=%d, current_position=%d, target_position=%d, front_channel=%d, state=%d",
        //              task.channel, task.current_position, task.target_position, task.front_channel, task.state);
        // }
        // ESP_LOGW(TAG, "-------------------");

        int step_size = 50; // Step size per move

        bool tasks_remaining = true;

        // Loop until all tasks are finished
        while (tasks_remaining) {
            tasks_remaining = false; // Assume no remaining tasks
            int tasks_executed = 0;  // Number of tasks executed in this round
            bool restart_iteration = false; // Flag to restart iteration

            // Lock task array
            if (xSemaphoreTake(task_queue_mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
                // Iterate task array
                for (auto it = servo_states_.begin(); it != servo_states_.end(); ) {
                    ServoState& task = *it;

                    // Control smooth movement
                    step_size = (task.smooth) ? 5 : 50; // Smooth: step=5, else step=50
                    // if(task.smooth) {
                    //     ESP_LOGW(TAG, "Smooth moving enabled for channel %d", task.channel);
                    // }

                    // Remove finished task
                    if (task.current_position == task.target_position) {
                        //ESP_LOGI(TAG, "Task completed and removed: channel=%d", task.channel);
                        it = servo_states_.erase(it); // Remove and update iterator
                        continue;
                    }

                    // Check if preceding task is finished
                    if (task.front_channel != -1) { // Has dependency
                        for (auto check_it = servo_states_.begin(); check_it != it; ++check_it) {
                            if (check_it->channel == task.front_channel) { // Find preceding task by channel
                                if (check_it->state != 1) { // If preceding task not finished
                                    //ESP_LOGI(TAG, "Front task not completed: front_channel=%d", task.front_channel);
                                    restart_iteration = true; // Mark to restart iteration
                                    break;
                                }else {
                                    break;
                                }
                            }
                        }
                        if (restart_iteration) break; // Restart from beginning
                    }

                    // If current task can execute, step once
                    tasks_remaining = true; // Mark there are unfinished tasks
                    int step = (task.current_position < task.target_position) ? step_size : -step_size;

                    // If step will overshoot, set to target
                    if (abs(task.target_position - task.current_position) <= step_size) {
                        task.current_position = task.target_position;
                        clock_current_position_[task.channel] = task.target_position; // Update current servo position
                    } else {
                        task.current_position += step;
                    }

                    // Execute servo movement
                    i2c_master_dev_handle_t dev_handle = (task.channel < 14) ? dev_handle_h : dev_handle_m;
                    int adjusted_channel = (task.channel < 14) ? task.channel : (task.channel - 14);
                    SetPWM(dev_handle, adjusted_channel, 0, task.current_position);
                    vTaskDelay(pdMS_TO_TICKS(15));

                    //ESP_LOGI(TAG, "Task step executed: channel=%d, current_position=%d, target_position=%d",
                    //         task.channel, task.current_position, task.target_position);

                    // Mark task as finished if done
                    if (task.current_position == task.target_position) {
                        //ESP_LOGI(TAG, "Task completed: channel=%d", task.channel);
                        task.state = 1;
                    }

                    // Check max concurrent tasks
                    tasks_executed++;
                    if (tasks_executed >= MAX_SERVO_TASK_NUM) {
                        //ESP_LOGI(TAG, "Reached MAX_SERVO_TASK_NUM in this round, moving to next round");
                        restart_iteration = true; // Reached max, restart
                        break;
                    }

                    ++it; // Next task
                }

                xSemaphoreGive(task_queue_mutex_);
            } else {
                ESP_LOGE(TAG, "Failed to acquire task queue mutex");
                break;
            }

            if (restart_iteration) {
                //ESP_LOGI(TAG, "Restarting task iteration from the beginning");
                continue;
            }

            vTaskDelay(pdMS_TO_TICKS(15));
        }

        //ESP_LOGI(TAG, "All tasks completed");

        xSemaphoreGive(servo_mute_mode_semaphore_);
    } else {
        ESP_LOGW(TAG, "Failed to acquire servo_mute_mode_semaphore_");
    }
}

// idle clock
void CyberClock::IdleClock() {
    if (!servo_driver_available_) {
        ESP_LOGW(TAG, "Cannot idle clock - servo driver not available");
        return;
    }

    ESP_LOGI(TAG, "Idle CyberClock");
    current_mode_ = MODE_03_IDLE;
}


CyberClock& CyberClock::GetInstance() {
    static CyberClock instance;
    return instance;
}


CyberClock::~CyberClock() {
    if (servo_mute_mode_semaphore_ != nullptr) {
        vSemaphoreDelete(servo_mute_mode_semaphore_);
    }
    
    if (servo_task_queue_ != nullptr) {
        vQueueDelete(servo_task_queue_);
    }

    if (clock_timer_) {
        xTimerStop(clock_timer_, 0);
        xTimerDelete(clock_timer_, 0);
    }
    
    if (servo_driver_available_) {
        i2c_master_bus_rm_device(dev_handle_h);
        i2c_master_bus_rm_device(dev_handle_m);
        i2c_del_master_bus(bus_handle);
    }

    if (display_mutex_ != nullptr) {
        vSemaphoreDelete(display_mutex_);
    }
}

