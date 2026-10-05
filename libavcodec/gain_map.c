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
