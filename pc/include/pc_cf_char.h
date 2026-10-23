#ifndef PC_CF_CHAR_H
#define PC_CF_CHAR_H

/* City Folk characters drawn in the GameCube game (pc_cf_char.c).
 *
 * pc/tools/extract_cf_sky.py converts characters from the player's City Folk disc into
 * cf_sky/chars/<name>.cfchar (pc/tools/cf_char.py). A character is posed by a GameCube skeleton:
 * the actor draws its skeleton without models while catching each joint's world matrix
 * (pc_cKF_capture in c_keyframe.c), then hands them here. City Folk's bones share the joints'
 * names and axes, so each bone takes its joint's rotation and keeps its own length; bones with
 * no joint keep their rest pose under their parent. The draw is a PC_NOOP_CF_CHAR tag in
 * POLY_OPA, rendered with the frame's camera into the same depth buffer. */

#ifdef __cplusplus
extern "C" {
#endif

#define PC_NOOP_CF_CHAR 0xAC5720u /* + instance 0..PC_CF_CHAR_INSTANCES-1 */
#define PC_CF_CHAR_INSTANCES 8
#define PC_CF_CHAR_SCALE 148.8f /* GameCube model units per City Folk unit */

struct game_play_s;

/* A GameCube joint's world matrix, column vectors: x' = m[r][0]x + m[r][1]y + m[r][2]z + m[r][3] */
typedef float pc_cf_mtx34[3][4];

/* actor: the matrix before the skeleton's root joint (the actor's place, turn and scale).
 * joints[i] is the world matrix of the joint named joint_names[i] (NULL names are skipped).
 * eye: 0-7, the eye frame (City Folk's e.N). light: 0-1. Returns FALSE if the character isn't there. */
int pc_cf_char_play_draw(struct game_play_s* play, const char* name, const pc_cf_mtx34 actor, int joint_num,
                         const char* const* joint_names, const pc_cf_mtx34* joints, int eye, float light);
/* Bones already posed: bone_names[i]'s world matrix (scale included) is bones[i]; the rest keep their
 * rest pose under their parent */
int pc_cf_char_play_draw_mtx(struct game_play_s* play, const char* name, int bone_num, const char* const* bone_names,
                             const pc_cf_mtx34* bones, int eye, float light);
int pc_cf_char_exists(const char* name);

/* Render side (emu64's NoOp handler) */
int pc_cf_char_is_tag(unsigned int tag);
void pc_cf_char_noop(unsigned int tag, const float proj[4][4], const float view[3][4]);

#ifdef __cplusplus
}
#endif

#endif /* PC_CF_CHAR_H */
