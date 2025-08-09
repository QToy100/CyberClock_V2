#pragma once
#include "driver/i2c_master.h"
#include <stdint.h>
#include <ctime>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <vector>
#include "settings.h"
#include <sys/time.h>

#define I2C_MASTER_NUM I2C_NUM_1

#define PCA9685_ADDR_H 0x47     
#define PCA9685_ADDR_M 0x41   

#define MAX_I2C_RETRIES 3
#define I2C_ERROR_THRESHOLD 5

#define MAX_SERVO_TASK_NUM 5 // 同时运行的最大舵机任务数

#define    MODE_00_NORMAL_CLOCK 0
#define    MODE_01_SET_NUMBER 1
#define    MODE_02_SET_COUNTDOWN 2
#define    MODE_03_IDLE 3
#define    MODE_04_SET_TIMER 4    
#define    MODE_98_ADJUST 98
#define    MODE_99_SHUTDOWN 99
#define    MODE_100_TEST 100

class CyberClock {
private:
    //二值信号量
    SemaphoreHandle_t display_mutex_;
    //全局的时钟数字
    int a_number_ = 0;
    int b_number_ = 0;
    int c_number_ = 0;
    int d_number_ = 0;

    int servo_offsets_[28] = {0}; // 每个舵机的偏移量，初始为 0


    // 配置参数
    int timezone_offset_ = 8; // 时区偏移（单位：小时）
    int timezone_offset_minute_ = 0; // 时区偏移（单位：分钟）
    int nap_start_hour_ = 12; // 午休开始时间（小时）
    int nap_end_hour_ = 13;   // 午休结束时间（小时）
    bool sleep_clock_enable_ = false;//晚上是否开启睡眠
    int sleep_start_hour_ ; // 晚上睡眠开始时间（小时）
    int sleep_end_hour_ ;   // 晚上睡眠结束时间（小时）
    int sleep_start_minute_ ; // 晚上睡眠开始时间（分钟）
    int sleep_end_minute_ ;   // 晚上睡眠结束时间（分钟）
    int countdown_time_ = 0; // 倒计时秒数
    int timer_tick_ = 0; // 定时器滴答计数


    int Servo_Mode_ = 0; // 舵机模式，0表示A模式，1表示B模式

    //语音调试模式
    bool debug_mode_ = false; //可以通过语音打开调试模式

    //静音移动（仅正常显示时有效），可以保证日常显示静音
    bool servo_mute_mode_ = true; // 静音模式开关，启用后，舵机转动缓和
    bool debug_servo_disabled_ = false; //是否驱动舵机运动，如果为true则不要运动舵机
    SemaphoreHandle_t servo_mute_mode_semaphore_; // 信号量，用于控制ExecuteTask
    SemaphoreHandle_t task_ready_semaphore_ = nullptr;// 添加一个信号量，用于控制任务执行的开始
    
            
    // 状态变量
    int clock_12_hour_ = 0; // 12小时制时钟
    TimerHandle_t clock_timer_;
    int i2c_error_count_ = 0;

    int alarm_time_ = -1; // -1 表示无闹钟

 
    // 硬件句柄
    i2c_master_bus_handle_t bus_handle = nullptr;
    i2c_master_dev_handle_t dev_handle_h = nullptr;
    i2c_master_dev_handle_t dev_handle_m = nullptr;
 
    // 数字显示配置
    const int digits[12][7] = {
        {1, 1, 1, 1, 1, 1, 0}, {0, 1, 1, 0, 0, 0, 0},
        {1, 1, 0, 1, 1, 0, 1}, {1, 1, 1, 1, 0, 0, 1},
        {0, 1, 1, 0, 0, 1, 1}, {1, 0, 1, 1, 0, 1, 1},
        {1, 0, 1, 1, 1, 1, 1}, {1, 1, 1, 0, 0, 0, 0},
        {1, 1, 1, 1, 1, 1, 1}, {1, 1, 1, 1, 0, 1, 1},
        {0, 0, 0, 0, 0, 0, 0}, // 0xA 全关
        {0, 0, 0, 0, 0, 0, 1}  // 0xB idle

    };

    // 舵机位置参数，实测有效范围是100~550，中间值是325
    const int segmentOn_A[28] = { 110, 110, 120, 330, 330, 310, 110, 
                                  110, 110, 120, 330, 330, 310, 110,
                                  110, 110, 120, 330, 330, 310, 110, 
                                  110, 110, 120, 330, 330, 310, 110};
    const int segmentOff_A[28] = {300, 300, 310, 140, 140, 120, 300,
                                  300, 300, 310, 140, 140, 120, 300, 
                                  300, 300, 310, 140, 140, 120, 300, 
                                  300, 300, 310, 140, 140, 120, 300};

    const int segmentOn_B[28] = {   325, 325, 325, 325, 325, 325, 325, 
                                    325, 325, 325, 325, 325, 325, 325,
                                    325, 325, 325, 325, 325, 325, 325, 
                                    325, 325, 325, 325, 325, 325, 325};
    const int segmentOff_B[28] = {  525, 525, 525, 125, 125, 125, 525,
                                    525, 525, 525, 125, 125, 125, 525, 
                                    525, 525, 525, 125, 125, 125, 525, 
                                    525, 525, 525, 125, 125, 125, 525};
    
    // const int segment_smile[28] = { 325, 525, 525, 125, 125, 125, 525,
    //                                 325, 325, 525, 125, 125, 325, 325, 
    //                                 325, 325, 525, 125, 125, 325, 325, 
    //                                 325, 525, 525, 125, 125, 125, 525};
    
    int segmentOn[28]; // 舵机打开位置
    int segmentOff[28]; // 舵机关闭位置
     
    // 舵机的位置
    int clock_current_position_[28]; //当前位置
    int clock_target_position_[28]; // 目标位置
    bool sntp_cb_set = false;

    void GetTargetPosition(int a, int b, int c, int d);
    bool InitI2CBus();
    bool InitPCA9685(i2c_master_dev_handle_t* dev_handle, uint8_t addr);
    bool InitializeServos();
    void InitializeCurrentPosition();
    void CheckSleepTime() ; 
    void AddServoTask(int ch, int start_position, int to_position, int front_channel = -1, bool smooth = false);
    void TaskUpdateDisplay(int a, int b, int c, int d, bool smooth = false);
    void UpdateIdleClock();
    void LoadSettings();
    void InitialMutexAndSemaphore();
    //void MoveServoStepByStep(i2c_master_dev_handle_t dev_handle, int channel, int start_position, int target_position);
    void SetPWM(i2c_master_dev_handle_t dev_handle, uint8_t channel, uint16_t on, uint16_t off);
    bool SafeI2CWrite(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint8_t value);
    void ExecuteTask();
    void OnTimerTick();
    void DetectServoMode();//判断是A模式还是B模式
    static void TimerCallback(TimerHandle_t xTimer);

public:
    QueueHandle_t servo_task_queue_; // 队列，用于存储舵机移动任务
    SemaphoreHandle_t task_queue_mutex_; // 互斥锁，用于保护任务队列

    int current_mode_ = MODE_00_NORMAL_CLOCK; // 当前模式，默认为正常时钟模式

    static CyberClock& GetInstance();
    int GetCurrentMode() const { return current_mode_; }
    void ShutdownClock();
    void IdleClock();
    void ShowTime();
    void SetNumber(int a, int b, int c, int d);
    void SetCountDown(int seconds);
    void SetTimer(int operation);
    void SetServoSilentMode(bool mode);
    void Set12HourMode(bool mode);
    void SetSleepTime(bool mode, int start_hour, int start_minute, int end_hour, int end_minute);
    int* GetServoOffsets() { return servo_offsets_; }

private:
    CyberClock();
    CyberClock(const CyberClock&) = delete;
    CyberClock& operator=(const CyberClock&) = delete;
    virtual ~CyberClock();
};
