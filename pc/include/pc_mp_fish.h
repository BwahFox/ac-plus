#ifndef PC_MP_FISH_H
#define PC_MP_FISH_H
/* The fish shadows shared during a visit (pc_mp_fish.c) */
#include "types.h"
#include "../src/mp/mp_wire.h"

struct game_play_s;

/* host: its fish, to send to every visitor */
void pc_mp_fish_collect_host(mp_fish* out);
/* host: a visitor's report (the fish they're fishing for, the ones they caught) */
void pc_mp_fish_host_received(int slot, const mp_fish* in, unsigned long long now_ms);
/* visitor: the host's fish */
void pc_mp_fish_visitor_received(const mp_fish* in, unsigned long long now_ms);
/* visitor: what to tell the host (FALSE: nothing to say) */
int pc_mp_fish_collect_visitor(mp_fish* out);
/* every frame after the actors: host - fish a visitor is fishing for go where they say; visitor - the host's fish
 * here (host_x/z: where the host is, in_town: the host is out in the town) */
void pc_mp_fish_apply(struct game_play_s* play, int host, int host_in_town, f32 host_x, f32 host_z,
                      unsigned long long now_ms);
/* visitor in the host's acre: the host's fish are the fish here (ac_set_ovl_gyoei.c makes none of its own) */
int pc_mp_fish_shared(void);
void pc_mp_fish_reset(void);

/* The insects, the same way (pc_mp_bugs.c; MP_MSG_BUGS, the same layout as the fish's) */
void pc_mp_bugs_collect_host(mp_fish* out);
void pc_mp_bugs_host_received(int slot, const mp_fish* in, unsigned long long now_ms);
void pc_mp_bugs_visitor_received(const mp_fish* in, unsigned long long now_ms);
int pc_mp_bugs_collect_visitor(mp_fish* out);
void pc_mp_bugs_apply(struct game_play_s* play, int host, int host_in_town, f32 host_x, f32 host_z,
                      unsigned long long now_ms);
int pc_mp_bugs_shared(void);
void pc_mp_bugs_reset(void);
#endif
