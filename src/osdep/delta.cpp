/*
 *
 * UAE - Redquark Delta values
 * 
 * (c) 2021 Chris Smith
 */
#ifdef REDQUARK

#include "amiberry_filesys.hpp"
#include "custom.h"

#include "math.h"
#include "delta.h"

#define TIME_NOW_MS (read_processor_time()/1000)
extern int get_host_hz(); // amiberry_gfx.cpp

// ----------------------------------------------------------------------------

void
delta_initialise( Delta *d, int target, int period, DeltaFlag flag )
{
    d->flag = flag;
    d->period_ms = period;
    d->frame_period_ms = (get_host_hz() == 50) ? 20 : 17;

    d->start = d->current = d->target = target * ((d->flag & DELTA_FLAG_PRECISION) ? 100 : 1);
}

// ----------------------------------------------------------------------------------
// Sets target value and sets up time period
void
delta_set( Delta *d, int target, DeltaFlag flag )
{
    if( flag ) d->flag = (DeltaFlag)((d->flag & DELTA_FLAG_PRECISION) | flag);

    d->start    = d->current;
    d->target   = target * ((d->flag & DELTA_FLAG_PRECISION) ? 100 : 1);
    d->target_t = TIME_NOW_MS + d->period_ms;
}

// ----------------------------------------------------------------------------------
//
void
delta_reset( Delta *d, int target )
{
    d->start = d->current = d->target = target * ((d->flag & DELTA_FLAG_PRECISION) ? 100 : 1);
}

// ----------------------------------------------------------------------------------
// Returns the time difference in ms until delta process finishes.
int
delta_process( Delta *d )
{
    if( d->target == d->current ) return 0;

    int remaining = d->target_t - (TIME_NOW_MS + d->frame_period_ms);

    if( remaining <= 0 ) {
        d->current = d->target;
        // If remaining was == 0, then 0 would be returned and the caller would
        // not be able to tell the difference between "nothing to do" and "delta has just done the last step".
        // Set remaining to 1 so the _next_ time delta_process is called, zero will be returned.
        remaining = 1;
    }
    else if( d->flag & DELTA_FLAG_LOG )                                             // PI/2 .. 0
        d->current = d->start  + (float)(d->target - d->start) * sin( M_PI_2 - M_PI_2 * ((float)(d->target_t - TIME_NOW_MS) / (d->period_ms)) );
    else if( d->flag & DELTA_FLAG_LOG_INV )
        d->current = d->target - (float)(d->target - d->start) * sin(          M_PI_2 * ((float)(d->target_t - TIME_NOW_MS) / (d->period_ms)) );
    else
        d->current += (d->frame_period_ms * (d->target - d->current)) / (d->target_t - TIME_NOW_MS ); // Linear

    return remaining;
}

// ----------------------------------------------------------------------------------
//
float
delta_get( Delta *d )
{
    float r = d->current / ((d->flag & DELTA_FLAG_PRECISION) ? 100 : 1);
    return d->flag & DELTA_FLAG_PRECISION ? r : rint(r);
}

// ----------------------------------------------------------------------------------

void
delay_period_set( DelayPeriod *d, int frame_units )
{
    d->frame_period_ms = (get_host_hz() == 50) ? 20 : 17;

    d->target_t = (int)(ceil( frame_units / d->frame_period_ms )) * d->frame_period_ms;

    d->target_t += TIME_NOW_MS;
    d->cancelled = 0;
}

// ----------------------------------------------------------------------------------
// Returns the number of ms until timeout. If  0, timeout has past. If < 0, cancelled
int
delay_period_process( DelayPeriod *d )
{
    if( d->cancelled )     return -1;
    if( d->target_t == 0 ) return  0;

    int remaining = d->target_t - (TIME_NOW_MS + d->frame_period_ms);

    if( remaining <= 0 ) {
        remaining = 0;
        d->target_t = 0;
    }

    return remaining;
}

// ----------------------------------------------------------------------------------
//
void
delay_period_cancel( DelayPeriod *d )
{
    d->cancelled = 1;
}

#endif //REDQUARK
