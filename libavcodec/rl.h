/**
 * @file rl.h
 * rl header.
 */

#ifndef AVCODEC_RL_H
#define AVCODEC_RL_H

/** RLTable. */
typedef struct RLTable {
    int n;                         ///< number of entries of table_vlc minus 1
    int last;                      ///< number of values for last = 0
    const uint16_t (*table_vlc)[2];
    const int8_t *table_run;
    const int8_t *table_level;
    uint8_t *index_run[2];         ///< encoding only
    int8_t *max_level[2];          ///< encoding & decoding
    int8_t *max_run[2];            ///< encoding & decoding
    VLC vlc;                       ///< decoding only deprected FIXME remove
    RL_VLC_ELEM *rl_vlc[32];       ///< decoding only
} RLTable;

void init_rl(RLTable *rl, int use_static);
void init_vlc_rl(RLTable *rl, int use_static);

static inline int get_rl_index(const RLTable *rl, int last, int run, int level)
{
    int index;
    index = rl->index_run[last][run];
    if (index >= rl->n)
        return rl->n;
    if (level > rl->max_level[last][run])
        return rl->n;
    return index + level - 1;
}

#endif
