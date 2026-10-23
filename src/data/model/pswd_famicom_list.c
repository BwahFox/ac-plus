#include "libforest/gbi_extensions.h"
#include "PR/gbi.h"
#include "evw_anime.h"
#include "c_keyframe.h"
#include "ac_npc.h"
#include "ef_effect_control.h"

#ifdef TARGET_PC
/* ac-plus: the "forbidden four" (Ice Climber, Mario Bros., Super Mario Bros., The Legend of Zelda) too */
u32 pswd_famicom_list_max = 19;

u16 pswd_famicom_list[] = { 0x36a, 0x36b, 0x36c, 0x36d, 0x36e, 0x36f, 0x370, 0x377, 0x371, 0x372,
                            0x373, 0x374, 0x375, 0x376, 0x378, 0x379, 0x37a, 0x37b, 0x37c };
#else
u32 pswd_famicom_list_max = 15;

u16 pswd_famicom_list[] = { 0x36a, 0x36b, 0x36c, 0x36d, 0x36e, 0x36f, 0x370, 0x377,
                            0x371, 0x372, 0x373, 0x374, 0x375, 0x376, 0x378 };
#endif
