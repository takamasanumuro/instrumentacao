#include "TimingUtils.h"

void interval_timer_init(IntervalTimer* timer, double interval_seconds) {
    if (!timer) return;
    
    timer->send_interval_seconds = interval_seconds;
    clock_gettime(CLOCK_MONOTONIC, &timer->last_send_time);
}

bool interval_timer_should_trigger(IntervalTimer* timer) {
    if (!timer) return false;
    
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    
    double time_diff = (current_time.tv_sec - timer->last_send_time.tv_sec) + 
                       (current_time.tv_nsec - timer->last_send_time.tv_nsec) / 1e9;
    
    return time_diff >= timer->send_interval_seconds;
}

void interval_timer_mark_triggered(IntervalTimer* timer) {
    if (!timer) return;
    
    clock_gettime(CLOCK_MONOTONIC, &timer->last_send_time);
}