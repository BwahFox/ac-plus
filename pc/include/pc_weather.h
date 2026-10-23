#ifndef PC_WEATHER_H
#define PC_WEATHER_H

/* City Folk style weather (pc_weather.c): the weather can change during the day,
 * clear spells can be overcast, and a rainbow can follow rain.
 *
 * The day's schedule comes from the date and the town's id, so it's the same on
 * every machine, after a reload or a time change, and needs no save data. Each hour
 * there's a chance of a change at some minute in it; a change rolls the season's
 * usual odds (m_kankyo_weather's table). The game applies it the way it applies the
 * daily weather (ac_weather.c), and events that fix the weather still win. */

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Settings on, no -weather override */
int pc_weather_active(void);

/* The schedule's weather and intensity now, before events (mEnv_RandomWeather) */
void pc_weather_now(s16* weather, s16* intensity);

/* 0-1, while the schedule and the game agree: how overcast a clear spell is (sky and
 * lighting), and how strong a rainbow is */
f32 pc_weather_cloud(void);
f32 pc_weather_rainbow(void);

#ifdef __cplusplus
}
#endif

#endif /* PC_WEATHER_H */
