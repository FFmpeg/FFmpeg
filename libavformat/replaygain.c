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

/**
 * @file
 * replaygain tags parsing
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "libavutil/avstring.h"
#include "libavutil/dict.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem.h"
#include "libavutil/replaygain.h"

#include "avformat.h"
#include "replaygain.h"

static int32_t parse_value(const char *value, int32_t min)
{
    char *fraction;
    int  scale = 10000;
    int32_t mb = 0;
    int sign   = 1;
    int db;

    if (!value)
        return min;

    value += strspn(value, " \t");

    if (*value == '-')
        sign = -1;

    db = strtol(value, &fraction, 0);
    if (*fraction++ == '.') {
        while (av_isdigit(*fraction) && scale) {
            mb += scale * (*fraction - '0');
            scale /= 10;
            fraction++;
        }
    }

    if (abs(db) > (INT32_MAX - mb) / 100000)
        return min;

    return db * 100000 + sign * mb;
}

static int replaygain_export(AVStream *st,
                             const uint8_t *track_gain, const uint8_t *track_peak,
                             const uint8_t *album_gain, const uint8_t *album_peak)
{
    AVPacketSideData *sd, *tmp;
    AVReplayGain *replaygain;
    int32_t tg, ag;
    uint32_t tp, ap;

    tg = parse_value(track_gain, INT32_MIN);
    ag = parse_value(album_gain, INT32_MIN);
    tp = parse_value(track_peak, 0);
    ap = parse_value(album_peak, 0);

    if (tg == INT32_MIN && ag == INT32_MIN)
        return 0;

    replaygain = av_mallocz(sizeof(*replaygain));
    if (!replaygain)
        return AVERROR(ENOMEM);

    tmp = av_realloc_array(st->side_data, st->nb_side_data + 1, sizeof(*tmp));
    if (!tmp) {
        av_freep(&replaygain);
        return AVERROR(ENOMEM);
    }
    st->side_data = tmp;
    st->nb_side_data++;

    sd = &st->side_data[st->nb_side_data - 1];
    sd->type = AV_PKT_DATA_REPLAYGAIN;
    sd->data = (uint8_t*)replaygain;
    sd->size = sizeof(*replaygain);

    replaygain->track_gain = tg;
    replaygain->track_peak = tp;
    replaygain->album_gain = ag;
    replaygain->album_peak = ap;

    return 0;
}

int ff_replaygain_export(AVStream *st, AVDictionary *metadata)
{
    const AVDictionaryEntry *tg, *tp, *ag, *ap;

    tg = av_dict_get(metadata, "REPLAYGAIN_TRACK_GAIN", NULL, 0);
    tp = av_dict_get(metadata, "REPLAYGAIN_TRACK_PEAK", NULL, 0);
    ag = av_dict_get(metadata, "REPLAYGAIN_ALBUM_GAIN", NULL, 0);
    ap = av_dict_get(metadata, "REPLAYGAIN_ALBUM_PEAK", NULL, 0);

    return replaygain_export(st,
                             tg ? tg->value : NULL,
                             tp ? tp->value : NULL,
                             ag ? ag->value : NULL,
                             ap ? ap->value : NULL);
}
