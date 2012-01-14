/*
 * Copyright (C) 2006 Smartjog S.A.S, Baptiste Coudurier <baptiste.coudurier@gmail.com>
 * Copyright (C) 2011 Smartjog S.A.S, Clément Bœsch      <clement.boesch@smartjog.com>
 *
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

/**
 * @file
 * Timecode helpers header
 */

#ifndef AVCODEC_TIMECODE_H
#define AVCODEC_TIMECODE_H

#include <stdint.h>
#include "avcodec.h"
#include "libavutil/rational.h"

#define TIMECODE_OPT(ctx, flags)                                         \
    "timecode", "set timecode value following hh:mm:ss[:;.]ff format, "  \
                "use ';' or '.' before frame number for drop frame",     \
    offsetof(ctx, tc.str),                                               \
    AV_OPT_TYPE_STRING, {.str=NULL}, CHAR_MIN, CHAR_MAX, flags

struct ff_timecode {
    char *str;       ///< string following the hh:mm:ss[:;.]ff format
    int start;       ///< timecode frame start
    int drop;        ///< drop flag (1 if drop, else 0)
    AVRational rate; ///< Frame rate in rational form
};

/**
 * @brief           Adjust frame number for NTSC drop frame time code
 * @param frame_num Actual frame number to adjust
 * @return          Adjusted frame number
 * @warning         Adjustment is only valid in NTSC 29.97
 */
int avpriv_framenum_to_drop_timecode(int frame_num);

/**
 * @brief       Convert frame id (timecode) to SMPTE 12M binary representation
 * @param frame Frame number
 * @param fps   Frame rate
 * @param drop  Drop flag
 * @return      The actual binary representation
 */
uint32_t avpriv_framenum_to_smpte_timecode(unsigned frame, int fps, int drop);

/**
 * @brief       Load timecode string in buf
 * @param buf   Destination buffer
 * @param tc    Timecode struct pointer
 * @param frame Frame id (timecode frame is computed with tc->start+frame)
 * @return a pointer to the buf parameter
 * @note  timecode representation can be a negative timecode and have
 *        more than 24 hours.
 * @note  buf must have enough space to store the timecode representation: 16
 *        bytes is the minimum required size.
 */
char *avpriv_timecode_to_string(char *buf, const struct ff_timecode *tc, unsigned frame);

/**
 * Check if timecode rate is valid and consistent with the drop flag.
 *
 * @return 0 on success, negative value on failure
 */
int avpriv_check_timecode_rate(void *avcl, AVRational rate, int drop);

/**
 * Parse SMTPE 12M time representation (hh:mm:ss[:;.]ff). str and rate fields
 * from tc struct must be set.
 *
 * @param avcl A pointer to an arbitrary struct of which the first field is a
 *             pointer to an AVClass struct (used for av_log).
 * @param tc   Timecode struct pointer
 * @return     0 on success, negative value on failure
 * @warning    Adjustement is only valid in NTSC 29.97
 */
int avpriv_init_smpte_timecode(void *avcl, struct ff_timecode *tc);

#if FF_API_OLD_TIMECODE
attribute_deprecated int ff_framenum_to_drop_timecode(int frame_num);
attribute_deprecated uint32_t ff_framenum_to_smtpe_timecode(unsigned frame, int fps, int drop);
attribute_deprecated int ff_init_smtpe_timecode(void *avcl, struct ff_timecode *tc);
#endif

#endif /* AVCODEC_TIMECODE_H */
