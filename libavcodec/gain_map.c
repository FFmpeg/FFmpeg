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

#include "libavutil/avassert.h"
#include "libavutil/error.h"
#include "libavutil/gain_map.h"
#include "libavutil/mem.h"
#include "libavutil/rational.h"

#include "bytestream.h"
#include "gain_map.h"

#define ISO21496_HEADER_SIZE 5

#define ISO21496_FLAG_MULTICHANNEL (1 << 7)
#define ISO21496_FLAG_USE_BASE_CG  (1 << 6)
#define ISO21496_FLAG_RESERVED     0x3f

static size_t iso21496_payload_size(int nb_channels)
{
    return 4 * 4 + (size_t) nb_channels * 10 * 4;
}

static int make_q(int64_t num, uint32_t den, AVRational *out)
{
    if (!den)
        return AVERROR_INVALIDDATA;

    /* Ensure the result fits in AVRational */
    av_reduce(&out->num, &out->den, num, den, INT32_MAX);
    return 0;
}

static int get_sq(GetByteContext *gb, AVRational *out)
{
    int32_t  num = (int32_t) bytestream2_get_be32(gb);
    uint32_t den = bytestream2_get_be32u(gb);
    return make_q(num, den, out);
}

static int get_uq(GetByteContext *gb, AVRational *out)
{
    uint32_t num = bytestream2_get_be32u(gb);
    uint32_t den = bytestream2_get_be32u(gb);
    return make_q(num, den, out);
}

int ff_gain_map_params_from_iso21496(AVGainMapParams *restrict p,
                                     const uint8_t *data, size_t size)
{
    int ret;
    if (!p || !data)
        return AVERROR(EINVAL);

    if (size < ISO21496_HEADER_SIZE || size > INT_MAX)
        return AVERROR_INVALIDDATA;

    GetByteContext gb;
    bytestream2_init(&gb, data, size);

    /* Parse header */
    const uint16_t minimum_version = bytestream2_get_be16(&gb);
    const uint16_t writer_version  = bytestream2_get_be16(&gb);
    if (minimum_version > FF_GAIN_MAP_VERSION || writer_version < minimum_version)
        return AVERROR_PATCHWELCOME;
    const uint8_t flags = bytestream2_get_byte(&gb);

    /**
     * While the spec does not require reserved bits to be 0, older versions
     * of the ISO 21496-1 draft specification had extra flags that got removed
     * from the final publication, so conservatively error out just in case;
     * unless the writer version is >0 in which case these may be genuine
     * additions to later revisions of the published spec, the validity of
     * ignoring which is already guarded by the `minimum_version` check.
     */
    if (writer_version == 0 && (flags & ISO21496_FLAG_RESERVED))
        return AVERROR_INVALIDDATA;

    p->version              = FF_GAIN_MAP_VERSION;
    p->nb_channels          = (flags & ISO21496_FLAG_MULTICHANNEL) ? 3 : 1;
    p->use_base_color_space = !!(flags & ISO21496_FLAG_USE_BASE_CG);

    /* Parse payload */
    if (bytestream2_get_bytes_left(&gb) < iso21496_payload_size(p->nb_channels))
        return AVERROR_INVALIDDATA;

    if ((ret = get_uq(&gb, &p->base_hdr_headroom))      < 0 ||
        (ret = get_uq(&gb, &p->alternate_hdr_headroom)) < 0)
        return ret;

    for (int c = 0; c < p->nb_channels; c++) {
        struct AVGainMapChannel *const ch = &p->channels[c];
        if ((ret = get_sq(&gb, &ch->gain_map_min))     < 0 ||
            (ret = get_sq(&gb, &ch->gain_map_max))     < 0 ||
            (ret = get_uq(&gb, &ch->gamma))            < 0 ||
            (ret = get_sq(&gb, &ch->base_offset))      < 0 ||
            (ret = get_sq(&gb, &ch->alternate_offset)) < 0)
            return ret;
    }

    /* nb_channels == 1 means every channel shares the same parameters */
    for (int c = p->nb_channels; c < 3; c++)
        p->channels[c] = p->channels[0];

    return av_gain_map_params_validate(p);
}

static void put_q(PutByteContext *pb, AVRational q)
{
    bytestream2_put_be32(pb, (unsigned) q.num);
    bytestream2_put_be32(pb, (unsigned) q.den);
}

int ff_gain_map_params_to_iso21496(const AVGainMapParams *p, uint8_t *buf)
{
    if (!p || !buf)
        return AVERROR(EINVAL);

    /* Ensures representability in the bitstream */
    int ret = av_gain_map_params_validate(p);
    if (ret < 0)
        return ret;

    if (p->version > FF_GAIN_MAP_VERSION)
        return AVERROR_PATCHWELCOME; /* e.g. libraries out of sync */

    PutByteContext pb;
    bytestream2_init_writer(&pb, buf, FF_GAIN_MAP_MAX_PAYLOAD_SIZE);

    int channels = av_gain_map_channels_identical(p) ? 1 : p->nb_channels;
    uint8_t flags = 0;
    if (channels == 3)
        flags |= ISO21496_FLAG_MULTICHANNEL;
    if (p->use_base_color_space)
        flags |= ISO21496_FLAG_USE_BASE_CG;

    bytestream2_put_be16(&pb, p->version); /* minimum_version */
    bytestream2_put_be16(&pb, p->version); /* writer_version */
    bytestream2_put_byte(&pb, flags);
    put_q(&pb, p->base_hdr_headroom);
    put_q(&pb, p->alternate_hdr_headroom);

    for (int c = 0; c < channels; c++) {
        const struct AVGainMapChannel *const ch = &p->channels[c];
        put_q(&pb, ch->gain_map_min);
        put_q(&pb, ch->gain_map_max);
        put_q(&pb, ch->gamma);
        put_q(&pb, ch->base_offset);
        put_q(&pb, ch->alternate_offset);
    }

    return bytestream2_tell_p(&pb);
}
