#ifndef PC_MP_NPC_H
#define PC_MP_NPC_H

/* The town's villagers in the same places in every game during a visit (pc_mp_npc.c) */

#include "types.h"
#include "../src/mp/mp_wire.h"

struct game_play_s;
struct actor_s;

/* visitor: where the villagers this game has loaded are, to send to the host */
void pc_mp_npc_collect(struct game_play_s* play, mp_npcs* out);
/* host: where the villagers visitor to_slot doesn't move are */
void pc_mp_npc_collect_for(struct game_play_s* play, int to_slot, unsigned long long now_ms, mp_npcs* out);
/* a list from another game (host: from a visitor's slot; visitor: from the host, slot 0) */
void pc_mp_npc_received(int from_slot, const mp_npcs* in, unsigned long long now_ms);
/* someone in another game is talking to villager actor a */
int pc_mp_npc_busy(struct actor_s* a, int host, unsigned long long now_ms);
/* villagers' memories of a visitor (host: send on joining, take from them; visitor: take, send changes) */
int pc_mp_memory_for(int v, const u8* name, const u8* town, mp_memory* out);
void pc_mp_memory_host_apply(const mp_memory* in, const u8* name, const u8* town);
void pc_mp_memory_visitor_received(const mp_memory* in);
int pc_mp_memory_visitor_tick(mp_memory* out);
void pc_mp_memory_reset(void);
/* a visitor left */
void pc_mp_npc_forget(int slot);
void pc_mp_npc_reset(void);
/* after the actors moved: put the villagers the other game moves where it said */
void pc_mp_npc_apply(struct game_play_s* play, int host, unsigned long long now_ms);

#endif
