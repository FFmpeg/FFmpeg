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

#include "gain_map.h"
#include "mem.h"
#include "libavcodec/bytestream.h"

static void gain_map_params_default(AVGainMapParams *p)
{
    *p = (AVGainMapParams) {
        .base_hdr_headroom      = { 0, 1 },
        .alternate_hdr_headroom = { 0, 1 },
        .nb_channels = 1,
    };

    /* Pre-fill all three channels as a convenience */
    for (int c = 0; c < 3; c++) {
        p->channels[c] = (struct AVGainMapChannel) {
            .gain_map_min       = { 0, 1 },
            .gain_map_max       = { 0, 1 },
            .gamma              = { 1, 1 },
            .base_offset        = { 1, 64 },
            .alternate_offset   = { 1, 64 },
        };
    }
}

#define CHECK(cond) do { if (!(cond)) return AVERROR_INVALIDDATA; } while (0)
#define CHECK_SIGNED(q)   CHECK((q).den > 0);
#define CHECK_UNSIGNED(q) CHECK((q).num >= 0 && (q).den > 0);

int av_gain_map_params_validate(const AVGainMapParams *p)
{
    CHECK(0 <= p->version && p->version <= AV_ISO21496_VERSION);
    CHECK(p->nb_channels == 1 || p->nb_channels == 3);
    CHECK_UNSIGNED(p->base_hdr_headroom);
    CHECK_UNSIGNED(p->alternate_hdr_headroom);

    for (int c = 0; c < p->nb_channels; c++) {
        const struct AVGainMapChannel *const ch = &p->channels[c];
        CHECK_SIGNED(ch->gain_map_min);
        CHECK_SIGNED(ch->gain_map_max);
        CHECK_UNSIGNED(ch->gamma);
        CHECK(ch->gamma.num != 0);
        /**
         * Note: The ISO spec declares these as signed, but a negative value
         * makes no numeric sense and would result in infinities in the
         * middle of the valid signal range; so just constrain them to be
         * unsigned as a safeguard. Note that the Adobe hdrgm spec explicitly
         * requires them to be nonnegative as well, so this avoids an
         * inconsistency.
         */
        CHECK_UNSIGNED(ch->base_offset);
        CHECK_UNSIGNED(ch->alternate_offset);
        CHECK(av_cmp_q(ch->gain_map_min, ch->gain_map_max) <= 0);
    }

    return 0;
}

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

int av_gain_map_params_from_iso21496(AVGainMapParams *restrict p,
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
    if (minimum_version > AV_ISO21496_VERSION || writer_version < minimum_version)
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

    p->version              = AV_ISO21496_VERSION;
    p->nb_channels          =   (flags & ISO21496_FLAG_MULTICHANNEL) ? 3 : 1;
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

int av_gain_map_params_to_iso21496(const AVGainMapParams *p, uint8_t *buf)
{
    if (!p || !buf)
        return AVERROR(EINVAL);

    /* Ensures representability in the bitstream */
    int ret = av_gain_map_params_validate(p);
    if (ret < 0)
        return ret;

    PutByteContext pb;
    bytestream2_init_writer(&pb, buf, AV_GAIN_MAP_MAX_PAYLOAD_SIZE);

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

int av_gain_map_channels_identical(const AVGainMapParams *p)
{
    static_assert(sizeof(p->channels[0]) == sizeof(int[2]) * 5,
                  "AVGainMapChannel struct is tightly packed");

    return !memcmp(&p->channels[0], &p->channels[1], sizeof(p->channels[0])) &&
           !memcmp(&p->channels[0], &p->channels[2], sizeof(p->channels[0]));
}

AVGainMapParams *av_gain_map_params_alloc(size_t *size)
{
    AVGainMapParams *p = av_malloc(sizeof(AVGainMapParams));
    if (!p)
        return NULL;

    gain_map_params_default(p);

    if (size)
        *size = sizeof(*p);

    return p;
}

AVGainMapParams *av_gain_map_params_create_side_data(AVFrameSideData ***fsd, int *nb_sd)
{
    AVFrameSideData *sd;
    sd = av_frame_side_data_new(fsd, nb_sd, AV_FRAME_DATA_GAIN_MAP_PARAMS,
                                sizeof(AVGainMapParams),
                                AV_FRAME_SIDE_DATA_FLAG_REPLACE);
    if (!sd)
        return NULL;

    AVGainMapParams *const p = (AVGainMapParams *) sd->data;
    gain_map_params_default(p);
    return p;
}
