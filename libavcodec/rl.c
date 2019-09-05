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
#include "libavutil/avassert.h"
#include "libavutil/mem.h"

#include "rl.h"

void ff_rl_free(RLTable *rl)
{
    int i;

    for (i = 0; i < 2; i++) {
        av_freep(&rl->max_run[i]);
        av_freep(&rl->max_level[i]);
        av_freep(&rl->index_run[i]);
    }
}

av_cold int ff_rl_init(RLTable *rl,
                       uint8_t static_store[2][2 * MAX_RUN + MAX_LEVEL + 3])
{
    int8_t  max_level[MAX_RUN + 1], max_run[MAX_LEVEL + 1];
    uint8_t index_run[MAX_RUN + 1];
    int last, run, level, start, end, i;

    /* If table is static, we can quit if rl->max_level[0] is not NULL */
    if (static_store && rl->max_level[0])
        return 0;

    /* compute max_level[], max_run[] and index_run[] */
    for (last = 0; last < 2; last++) {
        if (last == 0) {
            start = 0;
            end = rl->last;
        } else {
            start = rl->last;
            end = rl->n;
        }

        memset(max_level, 0, MAX_RUN + 1);
        memset(max_run, 0, MAX_LEVEL + 1);
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
        if (static_store)
            rl->max_level[last] = static_store[last];
        else {
            rl->max_level[last] = av_malloc(MAX_RUN + 1);
            if (!rl->max_level[last])
                goto fail;
        }
        memcpy(rl->max_level[last], max_level, MAX_RUN + 1);
        if (static_store)
            rl->max_run[last]   = static_store[last] + MAX_RUN + 1;
        else {
            rl->max_run[last]   = av_malloc(MAX_LEVEL + 1);
            if (!rl->max_run[last])
                goto fail;
        }
        memcpy(rl->max_run[last], max_run, MAX_LEVEL + 1);
        if (static_store)
            rl->index_run[last] = static_store[last] + MAX_RUN + MAX_LEVEL + 2;
        else {
            rl->index_run[last] = av_malloc(MAX_RUN + 1);
            if (!rl->index_run[last])
                goto fail;
        }
        memcpy(rl->index_run[last], index_run, MAX_RUN + 1);
    }
    return 0;

fail:
    ff_rl_free(rl);
    return AVERROR(ENOMEM);
}

av_cold void ff_rl_init_vlc(RLTable *rl, unsigned static_size)
{
    int i, q;
    VLC_TYPE table[1500][2] = {{0}};
    VLC vlc = { .table = table, .table_allocated = static_size };
    av_assert0(static_size <= FF_ARRAY_ELEMS(table));
    init_vlc(&vlc, 9, rl->n + 1, &rl->table_vlc[0][1], 4, 2, &rl->table_vlc[0][0], 4, 2, INIT_VLC_USE_NEW_STATIC);

    for (q = 0; q < 32; q++) {
        int qmul = q * 2;
        int qadd = (q - 1) | 1;

        if (q == 0) {
            qmul = 1;
            qadd = 0;
        }
        for (i = 0; i < vlc.table_size; i++) {
            int code = vlc.table[i][0];
            int len  = vlc.table[i][1];
            int level, run;

            if (len == 0) { // illegal code
                run   = 66;
                level = MAX_LEVEL;
            } else if (len < 0) { // more bits needed
                run   = 0;
                level = code;
            } else {
                if (code == rl->n) { // esc
                    run   = 66;
                    level =  0;
                } else {
                    run   = rl->table_run[code] + 1;
                    level = rl->table_level[code] * qmul + qadd;
                    if (code >= rl->last) run += 192;
                }
            }
            rl->rl_vlc[q][i].len   = len;
            rl->rl_vlc[q][i].level = level;
            rl->rl_vlc[q][i].run   = run;
        }
    }
}
