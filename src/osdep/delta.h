/*
 *
 * UAE - Redquark Delta values
 * 
 * (c) 2021 Chris Smith
 */
#pragma once

typedef enum {
    DELTA_FLAG_NONE      = 0,
    DELTA_FLAG_LOG       = 1<<0,
    DELTA_FLAG_LOG_INV   = 1<<1,
    DELTA_FLAG_LINEAR    = 1<<2,
    DELTA_FLAG_PRECISION = 1<<3,
} DeltaFlag;

typedef struct {
    int           start;
    int           target;          // The value to hit
    float         current;         // The current fractional value
    unsigned long target_t;        // The absolute time at which to hit the target value
    unsigned long period_ms;       // The number of ms within which to hit the target value.
    unsigned long frame_period_ms; // The smallest duration between process calls
    DeltaFlag     flag;
} Delta;

typedef struct {
    unsigned long target_t;
    int           cancelled;
    unsigned long frame_period_ms; // The smallest duration between process calls
} DelayPeriod;

void  delta_initialise( Delta *d, int target, int period, DeltaFlag flag );
void  delta_set( Delta *d, int target, DeltaFlag flag );
void  delta_reset( Delta *d, int target );
int   delta_process( Delta *d );
float delta_get( Delta *d );

void  delay_period_set( DelayPeriod *d, int frame_units );
int   delay_period_process( DelayPeriod *d );
void  delay_period_cancel( DelayPeriod *d );
