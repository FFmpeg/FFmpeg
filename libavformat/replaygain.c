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

static int32_t parse_gain(const char *gain)
{
    char *fraction;
    int  scale = 10000;
    int32_t mb = 0;
    int db;

    if (!gain)
        return INT32_MIN;

    gain += strspn(gain, " \t");

    db = strtol(gain, &fraction, 0);
    if (*fraction++ == '.') {
        while (av_isdigit(*fraction) && scale) {
            mb += scale * (*fraction - '0');
            scale /= 10;
            fraction++;
        }
    }

    if (abs(db) > (INT32_MAX - mb) / 100000)
        return INT32_MIN;

    return db * 100000 + FFSIGN(db) * mb;
}

static uint32_t parse_peak(const uint8_t *peak)
{
    int64_t val = 0;
    int64_t scale = 1;

    if (!peak)
        return 0;

    peak += strspn(peak, " \t");

    if (peak[0] == '1' && peak[1] == '.')
        return UINT32_MAX;
    else if (!(peak[0] == '0' && peak[1] == '.'))
        return 0;

    peak += 2;

    while (av_isdigit(*peak)) {
        int digit = *peak - '0';

        if (scale > INT64_MAX / 10)
            break;

        val    = 10 * val + digit;
        scale *= 10;

        peak++;
    }

    return av_rescale(val, UINT32_MAX, scale);
}

static int replaygain_export(AVStream *st,
                             const uint8_t *track_gain, const uint8_t *track_peak,
                             const uint8_t *album_gain, const uint8_t *album_peak)
{
    AVPacketSideData *sd, *tmp;
    AVReplayGain *replaygain;
    int32_t tg, ag;
    uint32_t tp, ap;

    tg = parse_gain(track_gain);
    ag = parse_gain(album_gain);
    tp = parse_peak(track_peak);
    ap = parse_peak(album_peak);

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
