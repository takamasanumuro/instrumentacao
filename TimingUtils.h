#ifndef TIMING_UTILS_H
#define TIMING_UTILS_H

#include <time.h>
#include <stdbool.h>

typedef struct {
    struct timespec last_send_time;
    double send_interval_seconds;
} IntervalTimer;

// Initialize timer with interval
void interval_timer_init(IntervalTimer* timer, double interval_seconds);

// Check if enough time has elapsed since last trigger
bool interval_timer_should_trigger(IntervalTimer* timer);

// Mark that timer was triggered (updates last_send_time)
void interval_timer_mark_triggered(IntervalTimer* timer);

#endif // TIMING_UTILS_H