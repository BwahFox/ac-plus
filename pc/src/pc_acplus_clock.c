/* pc_acplus_clock.c - the town's calendar clock (lbRTC_GetHardTime), on top of Aurora's OSGetSystemTime.
 *
 * Aurora reads the computer's clock on every call, so a sleeping computer or an NTP correction no longer leaves the
 * town behind. On top of it:
 *  - --time / --date shift the clock (the town's time still runs on from there, as on ac-plus before), instead of
 *    pinning the hour;
 *  - online visits read the host's town time (pc_mp.c);
 *  - the clock never runs backwards in a running game (an autumn clock change waits for the next start);
 *  - dev: PC_CLOCK_SPEED=n runs the calendar n times faster, PC_CLOCK_LAG_AT (pc_devtest.c) sets it back. */
#include "pc_platform.h"

#include <dolphin/os.h>
#include <stdlib.h>
#include <string.h>

static int s_ready = 0;
static s64 s_shift = 0;    /* --time / --date */
static s64 s_mp_shift = 0; /* online: the host's town time */
static s64 s_start = 0;    /* the clock at startup (PC_CLOCK_SPEED) */
static s64 s_last = 0;
static int s_speed = 0;

static void pc_clock_init(void) {
    s64 now;

    if (s_ready) {
        return;
    }
    s_ready = 1;
    now = (s64)OSGetSystemTime();
    s_start = now;
    s_speed = getenv("PC_CLOCK_SPEED") != NULL ? atoi(getenv("PC_CLOCK_SPEED")) : 0;
    if (g_pc_time_override >= 0 || g_pc_date_month > 0) {
        OSCalendarTime c;

        OSTicksToCalendarTime(now, &c);
        if (g_pc_date_month > 0) {
            c.mon = g_pc_date_month - 1;
            c.mday = g_pc_date_day > 0 ? g_pc_date_day : 1;
            if (g_pc_date_year > 0) {
                c.year = g_pc_date_year;
            }
        }
        if (g_pc_time_override >= 0) {
            c.hour = g_pc_time_override;
            c.min = g_pc_min_override >= 0 ? g_pc_min_override : 0;
            c.sec = g_pc_sec_override >= 0 ? g_pc_sec_override : 0;
        }
        c.msec = 0;
        c.usec = 0;
        s_shift = (s64)OSCalendarTimeToTicks(&c) - now;
    }
}

static s64 pc_clock_own(void) {
    s64 t;

    pc_clock_init();
    t = (s64)OSGetSystemTime();
    if (s_speed > 1) {
        t = s_start + (t - s_start) * s_speed;
    }
    return t + s_shift;
}

s64 pc_os_calendar_ticks(void) {
    s64 t = pc_clock_own();

    if (t < s_last) {
        t = s_last; /* never backwards */
    }
    s_last = t;
    return t + s_mp_shift;
}

/* dev (pc_devtest.c PC_CLOCK_LAG_AT): set the calendar secs behind, as a sleep would have */
void pc_os_dev_clock_lag(int secs) {
    s_shift -= (s64)secs * (s64)OS_TIMER_CLOCK;
    s_last = 0;
}

/* Online visits (pc_mp.c): read the host's clock (a calendar date and time) from now on, 0 to go back to our own */
void pc_os_mp_set_clock(int on, int year, int month, int day, int hour, int min, int sec) {
    OSCalendarTime c;

    if (!on) {
        s_mp_shift = 0;
        return;
    }
    memset(&c, 0, sizeof(c));
    c.sec = sec;
    c.min = min;
    c.hour = hour;
    c.mday = day;
    c.mon = month - 1;
    c.year = year;
    s_mp_shift = (s64)OSCalendarTimeToTicks(&c) - pc_clock_own();
}
