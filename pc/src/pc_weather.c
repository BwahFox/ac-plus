/* pc_weather.c - City Folk style weather through the day. See pc_weather.h. */
#include "pc_weather.h"
#include "pc_platform.h"
#include "pc_settings.h"

#include "m_common_data.h"
#include "m_kankyo.h"

#include <stdio.h>
#include <stdlib.h>

/* m_kankyo_weather.c_inc: the season's weather for a roll of 0-9 */
extern void mEnv_PC_RollWeather(int roll, s16* weather, s16* intensity);

#define PC_WEATHER_CHANGE_PERCENT  18 /* each hour */
#define PC_WEATHER_CLEARING_PERCENT 35 /* each hour of rain or snow, so wet spells stay shorter */
#define PC_WEATHER_CLOUDY_PERCENT  30 /* of clear spells */
#define PC_WEATHER_RAINBOW_PERCENT 65 /* of clear spells right after rain, 7:00-16:00 */
#define PC_WEATHER_CLOUDY          0.7f
#define PC_WEATHER_FADE_MIN        20.0f /* overcast fades in and out over this long */
#define PC_WEATHER_PRE_RAIN_MIN    40.0f /* and builds up this long before rain */

typedef struct {
    int start; /* minute of the day */
    s16 weather;
    s16 intensity;
    u8 cloudy;
    u8 rainbow;
} pc_weather_spell_c;

static struct {
    u32 key;
    int count;
    pc_weather_spell_c spell[25];
} l_day = { 0xFFFFFFFF, 0, { { 0 } } };

static u32 pc_weather_hash(u32 seed, u32 n) {
    u32 x = seed ^ (n * 0x9E3779B9u);

    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static int pc_weather_wet(s16 weather) {
    return weather == mEnv_WEATHER_RAIN || weather == mEnv_WEATHER_SNOW;
}

static void pc_weather_roll(u32 seed, int n, int changing, pc_weather_spell_c* spell) {
    mEnv_PC_RollWeather(pc_weather_hash(seed, n) % 10, &spell->weather, &spell->intensity);
    spell->cloudy = changing && spell->weather == mEnv_WEATHER_CLEAR &&
                    pc_weather_hash(seed, 100 + n) % 100 < PC_WEATHER_CLOUDY_PERCENT;
    spell->rainbow = 0;
}

static void pc_weather_build_day(const lbRTC_time_c* rtc) {
    u32 key = ((u32)rtc->year << 16 | (u32)rtc->month << 8 | rtc->day) ^ ((u32)Save_Get(land_info).id << 7);
    u32 seed = pc_weather_hash(key, 0x5EA50);
    s16 w0, i0, w9, i9;
    int changing, hour;

    if (key == l_day.key) {
        return;
    }
    l_day.key = key;

    /* days where the season allows one weather only (blossoms, New Year...) stay as they are */
    mEnv_PC_RollWeather(0, &w0, &i0);
    mEnv_PC_RollWeather(9, &w9, &i9);
    changing = w0 != w9 || i0 != i9;

    l_day.count = 1;
    l_day.spell[0].start = 0;
    pc_weather_roll(seed, 0, changing, &l_day.spell[0]);
    for (hour = 1; changing && hour < 24; hour++) {
        pc_weather_spell_c* prev = &l_day.spell[l_day.count - 1];
        pc_weather_spell_c next;

        if (pc_weather_hash(seed, 200 + hour) % 100 >=
            (pc_weather_wet(prev->weather) ? PC_WEATHER_CLEARING_PERCENT : PC_WEATHER_CHANGE_PERCENT)) {
            continue;
        }
        pc_weather_roll(seed, hour, changing, &next);
        if (next.weather == prev->weather && next.intensity == prev->intensity && next.cloudy == prev->cloudy) {
            continue;
        }
        next.start = hour * 60 + (int)(pc_weather_hash(seed, 300 + hour) % 60);
        next.rainbow = prev->weather == mEnv_WEATHER_RAIN && next.weather == mEnv_WEATHER_CLEAR && !next.cloudy &&
                       next.start >= 7 * 60 && next.start <= 16 * 60 &&
                       pc_weather_hash(seed, 400 + hour) % 100 < PC_WEATHER_RAINBOW_PERCENT;
        l_day.spell[l_day.count++] = next;
    }

    {
        static const char* names[] = { "clear", "rain", "snow", "blossoms", "leaves" };
        int i;

        printf("[Weather] %d/%d schedule:", rtc->month, rtc->day);
        for (i = 0; i < l_day.count; i++) {
            const pc_weather_spell_c* s = &l_day.spell[i];

            printf(" %02d:%02d %s%s%s%s", s->start / 60, s->start % 60, s->cloudy ? "overcast " : "",
                   s->weather < 5 ? names[s->weather] : "?", s->intensity >= mEnv_WEATHER_INTENSITY_HEAVY ? " heavy" : "",
                   s->rainbow ? " +rainbow" : "");
        }
        printf("\n");
        fflush(stdout);
    }
}

/* dev: PC_WEATHER_DUMP=n logs the schedules of the n days from today (ac_weather.c calls it in town) */
void pc_weather_dev_dump(void) {
    static int done;
    const char* env = getenv("PC_WEATHER_DUMP");
    lbRTC_time_c saved = Common_Get(time.rtc_time);
    int n, i;

    if (done || env == NULL) {
        return;
    }
    done = 1;
    n = atoi(env);
    for (i = 0; i < n; i++) {
        lbRTC_time_c* rtc = &Common_Get(time.rtc_time);
        static const u8 month_days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

        pc_weather_build_day(rtc);
        if (++rtc->day > month_days[rtc->month - 1]) {
            rtc->day = 1;
            if (++rtc->month > 12) {
                rtc->month = 1;
                rtc->year++;
            }
        }
    }
    Common_Get(time.rtc_time) = saved;
    l_day.key = 0xFFFFFFFF;
}

/* The spell now, and the minute of the day */
static int pc_weather_find(f32* minute) {
    const lbRTC_time_c* rtc = &Common_Get(time.rtc_time);
    int i = 0;

    pc_weather_build_day(rtc);
    *minute = rtc->hour * 60 + rtc->min + rtc->sec / 60.0f;
    while (i + 1 < l_day.count && *minute >= l_day.spell[i + 1].start) {
        i++;
    }
    return i;
}

int pc_weather_active(void) {
    return g_pc_settings.cf_weather && g_pc_weather_override < 0;
}

void pc_weather_now(s16* weather, s16* intensity) {
    f32 minute;
    int i = pc_weather_find(&minute);

    *weather = l_day.spell[i].weather;
    *intensity = l_day.spell[i].intensity;
}

/* The schedule's spell, if the game is showing it (events, the island and so on can differ) */
static int pc_weather_shown(f32* minute) {
    int i;

    if (!pc_weather_active()) {
        return -1;
    }
    i = pc_weather_find(minute);
    return Common_Get(weather) == l_day.spell[i].weather ? i : -1;
}

static f32 pc_weather_cloud_of(const pc_weather_spell_c* spell) {
    return pc_weather_wet(spell->weather) ? 1.0f : spell->cloudy ? PC_WEATHER_CLOUDY : 0.0f;
}

static f32 pc_weather_clamp01(f32 x) {
    return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
}

f32 pc_weather_cloud(void) {
    f32 minute, cloud, t;
    int i = pc_weather_shown(&minute);
    const pc_weather_spell_c* spell;

    if (i < 0 || Common_Get(weather) != mEnv_WEATHER_CLEAR) {
        return 0.0f;
    }
    spell = &l_day.spell[i];
    cloud = pc_weather_cloud_of(spell);
    if (i > 0) {
        t = pc_weather_clamp01((minute - spell->start) / PC_WEATHER_FADE_MIN);
        cloud = pc_weather_cloud_of(&spell[-1]) + t * (cloud - pc_weather_cloud_of(&spell[-1]));
    }
    if (i + 1 < l_day.count && pc_weather_wet(spell[1].weather)) {
        t = 1.0f - pc_weather_clamp01((spell[1].start - minute) / PC_WEATHER_PRE_RAIN_MIN);
        if (t * PC_WEATHER_CLOUDY > cloud) {
            cloud = t * PC_WEATHER_CLOUDY;
        }
    }
    return cloud;
}

f32 pc_weather_rainbow(void) {
    f32 minute, since, a;
    int i = pc_weather_shown(&minute);
    const pc_weather_spell_c* spell;

    if (i < 0 || !l_day.spell[i].rainbow) {
        return 0.0f;
    }
    spell = &l_day.spell[i];
    since = minute - spell->start;
    a = pc_weather_clamp01((since - 5.0f) / 10.0f) * pc_weather_clamp01((70.0f - since) / 25.0f);
    if (i + 1 < l_day.count) {
        a *= pc_weather_clamp01((spell[1].start - minute) / 10.0f);
    }
    return a;
}
