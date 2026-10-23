#ifndef PC_CF_DATA_H
#define PC_CF_DATA_H

/* ac-plus needs files converted from the player's own City Folk disc (pc/tools/extract_cf_sky.py), in
 * cf_sky/. A complete extraction ends by writing cf_sky/version.txt; the game starts only when it holds
 * this number. Raise it together with CF_DATA_VERSION in extract_cf_sky.py whenever what the extraction
 * writes changes, so players are asked to run it again. */
#define PC_CF_DATA_VERSION 1

#endif /* PC_CF_DATA_H */
