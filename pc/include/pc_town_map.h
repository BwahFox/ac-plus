#ifndef PC_TOWN_MAP_H
#define PC_TOWN_MAP_H

/* Picking the town's map on the train, as Animal Crossing Deluxe does (pc_town_map.c) */

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

void pc_town_map_new_town(int on); /* a brand-new town is being set up (or not: another start) */
int pc_town_map_ride(int index, u8* buf, int size, int buf_size);
int pc_town_map_msg(int index, u8* buf, int buf_size, int* len);
int pc_town_map_busy(void); /* Rover waits while the map is being picked */

#ifdef __cplusplus
}
#endif

#endif /* PC_TOWN_MAP_H */
