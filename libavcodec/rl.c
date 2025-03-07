/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdint.h>
#include <string.h>

#include "libavutil/attributes.h"

#include "rl.h"

av_cold void ff_rl_init_level_run(uint8_t max_level[MAX_LEVEL + 1],
                                  uint8_t index_run[MAX_RUN + 1],
                                  const uint8_t table_run[/* n */],
                                  const uint8_t table_level[/* n*/],
                                  int n)
{
    memset(index_run, n, MAX_RUN + 1);
    for (int i = 0; i < n; i++) {
        int run   = table_run[i];
        int level = table_level[i];
        if (index_run[run] == n)
            index_run[run] = i;
        if (level > max_level[run])
            max_level[run] = level;
    }
}

av_cold void ff_rl_init(RLTable *rl,
                        uint8_t static_store[2][2 * MAX_RUN + MAX_LEVEL + 3])
{
    int last, run, level, start, end, i;

    /* compute max_level[], max_run[] and index_run[] */
    for (last = 0; last < 2; last++) {
        int8_t *max_level  = static_store[last];
        int8_t *max_run    = static_store[last] + MAX_RUN + 1;
        uint8_t *index_run = static_store[last] + MAX_RUN + 1 + MAX_LEVEL + 1;
        if (last == 0) {
            start = 0;
            end = rl->last;
        } else {
            start = rl->last;
            end = rl->n;
        }

        memset(index_run, rl->n, MAX_RUN + 1);
        for (i = start; i < end; i++) {
            run   = rl->table_run[i];
            level = rl->table_level[i];
            if (index_run[run] == rl->n)
                index_run[run] = i;
            if (level > max_level[run])
                max_level[run] = level;
            if (run > max_run[level])
                max_run[level] = run;
        }
        rl->max_level[last] = max_level;
        rl->max_run[last]   = max_run;
        rl->index_run[last] = index_run;
    }
}

av_cold void ff_rl_init_vlc(RLTable *rl, unsigned static_size)
{
    VLCElem *vlc;

    ff_vlc_init_table_sparse(rl->rl_vlc[0], static_size, 9, rl->n + 1,
                             &rl->table_vlc[0][1], 4, 2,
                             &rl->table_vlc[0][0], 4, 2,
                             NULL, 0, 0, 0);

    vlc = rl->rl_vlc[0];

    // We count down to avoid trashing the first RL-VLC
    for (int q = 32; --q >= 0;) {
        int qmul = q * 2;
        int qadd = (q - 1) | 1;

        if (!rl->rl_vlc[q])
            continue;

        if (q == 0) {
            qmul = 1;
            qadd = 0;
        }
        for (unsigned i = 0; i < static_size; i++) {
            int idx  = vlc[i].sym;
            int len  = vlc[i].len;
            int level, run;

            if (len == 0) { // illegal code
                run   = 66;
                level = MAX_LEVEL;
            } else if (len < 0) { // more bits needed
                run   = 0;
                level = idx;
            } else {
                if (idx == rl->n) { // esc
                    run   = 66;
                    level =  0;
                } else {
                    run   = rl->table_run[idx] + 1;
                    level = rl->table_level[idx] * qmul + qadd;
                    if (idx >= rl->last) run += 192;
                }
            }
            rl->rl_vlc[q][i].len8  = len;
            rl->rl_vlc[q][i].level = level;
            rl->rl_vlc[q][i].run   = run;
        }
    }
}
