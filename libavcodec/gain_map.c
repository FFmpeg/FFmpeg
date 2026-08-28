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

#include <stdio.h>

#include "libavutil/avassert.h"
#include "libavutil/common.h"
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

int ff_gain_map_params_check_xmp(const AVGainMapParams *p)
{
    if (av_gain_map_params_validate(p) < 0)
        return 0;

    if (p->version > FF_GAIN_MAP_VERSION)
        return 0; /* e.g. libraries out of sync */

    /* XMP gain maps only store one set of parameters (shared across channels),
     * have no way to describe a map applied in the alternate rendition's
     * color space, and explicitly reject no-op parameters */
    return (p->nb_channels == 1 || av_gain_map_channels_identical(p)) &&
           p->use_base_color_space &&
           av_cmp_q(p->alternate_hdr_headroom, p->base_hdr_headroom) != 0;
}

#define XMP_GAIN_MAP_TEMPLATE                                                 \
    "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" x:xmptk=\"XMP Core 5.5.0\">\n"     \
    "  <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n" \
    "    <rdf:Description rdf:about=\"\"\n"                                   \
    "     xmlns:hdrgm=\"" FF_GAIN_MAP_XMP_NAMESPACE "\"\n"                    \
    "     hdrgm:Version=\"1.0\"\n"                                            \
    "     hdrgm:GainMapMin=\"%g\"\n"                                          \
    "     hdrgm:GainMapMax=\"%g\"\n"                                          \
    "     hdrgm:Gamma=\"%g\"\n"                                               \
    "     hdrgm:OffsetSDR=\"%g\"\n"                                           \
    "     hdrgm:OffsetHDR=\"%g\"\n"                                           \
    "     hdrgm:HDRCapacityMin=\"%g\"\n"                                      \
    "     hdrgm:HDRCapacityMax=\"%g\"\n"                                      \
    "     hdrgm:BaseRenditionIsHDR=\"%s\"/>\n"                                \
    "  </rdf:RDF>\n"                                                          \
    "</x:xmpmeta>"

static_assert(FF_GAIN_MAP_XMP_MAX_LEN > sizeof(XMP_GAIN_MAP_TEMPLATE) + 8 * 10,
              "XMP buffer too small for template and values");

int ff_gain_map_params_to_xmp(const AVGainMapParams *p, char *buf)
{
    if (!p || !buf)
        return AVERROR(EINVAL);

    if (!ff_gain_map_params_check_xmp(p))
        return AVERROR(ENOTSUP);

    const struct AVGainMapChannel *const ch = &p->channels[0];
    AVRational offset_sdr  = ch->base_offset;
    AVRational offset_hdr  = ch->alternate_offset;
    AVRational hdr_cap_min = p->base_hdr_headroom;
    AVRational hdr_cap_max = p->alternate_hdr_headroom;

    const int base_is_hdr = av_cmp_q(p->base_hdr_headroom,
                                     p->alternate_hdr_headroom) > 0;
    if (base_is_hdr) {
        FFSWAP(AVRational, offset_sdr, offset_hdr);
        FFSWAP(AVRational, hdr_cap_min, hdr_cap_max);
    }

    const size_t size = FF_GAIN_MAP_XMP_MAX_LEN;
    int ret = snprintf(buf, size, XMP_GAIN_MAP_TEMPLATE,
                       av_q2d(ch->gain_map_min), av_q2d(ch->gain_map_max),
                       av_q2d(ch->gamma),
                       av_q2d(offset_sdr), av_q2d(offset_hdr),
                       av_q2d(hdr_cap_min), av_q2d(hdr_cap_max),
                       base_is_hdr ? "True" : "False");
    if (ret < 0 || ret >= size)
        return AVERROR_BUG;

    return ret;
}
