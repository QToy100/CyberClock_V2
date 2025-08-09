#ifndef MAIN_H
#define MAIN_H

#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

extern bool ClockState_; // 时钟状态，true表示运行中，false表示已关闭
void SyncTime();
void SetTimezoneOffset(int offset);
void SetTimezoneOffsetMinute(int offset);
void ApplyTimezoneAndOffset(struct timeval* tv, int tz_hour, int tz_min);
void Enable_5V_Output(int enable);

#ifdef __cplusplus
}
#endif

#endif // MAIN_H
