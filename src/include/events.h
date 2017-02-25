#ifndef EVENTS_H
#define EVENTS_H

 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Events
  * These are best for low-frequency events. Having too many of them,
  * or using them for events that occur too frequently, can cause massive
  * slowdown.
  *
  * Copyright 1995-1998 Bernd Schmidt
  */

#undef EVENT_DEBUG

#include "machdep/rpt.h"

extern frame_time_t vsyncmintime;
extern int vsynctimebase, syncbase;
extern void reset_frame_rate_hack ();
extern int speedup_timelimit;

extern void compute_vsynctime ();
extern void init_eventtab ();
extern void events_schedule ();

extern long last_synctime;
typedef void (*evfunc)();
typedef void (*evfunc2)(uae_u32);

typedef void (*do_cycles_func)(unsigned long);
extern do_cycles_func do_cycles;
void do_cycles_cpu_fastest (unsigned long cycles_to_add);
void do_cycles_cpu_norm (unsigned long cycles_to_add);

typedef unsigned long int evt;

struct ev
{
    bool active;
    evt evtime, oldcycles;
    evfunc handler;
};

struct ev2
{
    bool active;
    evt evtime;
    uae_u32 data;
    evfunc2 handler;
};

enum {
    ev_copper, 
    ev_cia, ev_audio, ev_blitter, ev_dmal, ev_misc, ev_hsync,
    ev_max
};

enum {
    ev2_disk, ev2_disk_motor0, ev2_disk_motor1, ev2_disk_motor2, ev2_disk_motor3,
    ev2_max
};

extern int pissoff_value;
#define countdown (regs.pissoff)
//TODO: check and implement this
//#define do_cycles do_cycles_slow

extern struct ev eventtab[ev_max];
extern struct ev2 eventtab2[ev2_max];

extern volatile bool vblank_found_chipset;
extern volatile bool vblank_found_rtg;
extern int hpos_offset;
extern int maxhpos;

STATIC_INLINE void cycles_do_special ()
{
#ifdef JIT
	if (currprefs.cachesize) {
		if (regs.pissoff >= 0)
			regs.pissoff = -1;
	} else 
#endif
  {
		regs.pissoff = 0;
	}
}

STATIC_INLINE void do_extra_cycles(unsigned long cycles_to_add)
{
	regs.pissoff -= cycles_to_add;
}

STATIC_INLINE unsigned long int get_cycles ()
{
  return currcycle;
}

STATIC_INLINE void set_cycles (unsigned long int x)
{
  currcycle = x;
	eventtab[ev_hsync].oldcycles = x;
}

STATIC_INLINE int current_hpos_safe ()
{
  int hp = (get_cycles () - eventtab[ev_hsync].oldcycles) / CYCLE_UNIT;
	return hp;
}

STATIC_INLINE int current_hpos ()
{
  int hp = (get_cycles () - eventtab[ev_hsync].oldcycles) / CYCLE_UNIT;
	return hp;
}

STATIC_INLINE bool cycles_in_range (unsigned long endcycles)
{
	signed long c = get_cycles ();
	return static_cast<signed long>(endcycles) - c > 0;
}

extern void MISC_handler();

STATIC_INLINE void event2_newevent (int no, evt t, uae_u32 data)
{
	eventtab2[no].active = true;
  eventtab2[no].evtime = (t * CYCLE_UNIT) + get_cycles();
  eventtab2[no].data = data;
  MISC_handler();
}

STATIC_INLINE void event2_remevent (int no)
{
	eventtab2[no].active = false;
}

STATIC_INLINE void event_newevent (int no, evt t)
{
  evt ct = get_cycles();
	eventtab[no].active = true;
  eventtab[no].evtime = ct + t * CYCLE_UNIT;
  events_schedule();
}

STATIC_INLINE void event_remevent (int no)
{
	eventtab[no].active = false;
}

#endif
