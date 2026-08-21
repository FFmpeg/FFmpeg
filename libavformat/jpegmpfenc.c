/*
 * JPEG Multi-Picture Format muxer
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
 * Muxes several JPEG images into a single JPEG Multi-Picture Format file.
 */

#include <string.h>
#include <stddef.h>

#include "libavutil/avassert.h"
#include "libavutil/gain_map.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"

#include "libavcodec/bytestream.h"
#include "libavcodec/exif.h"
#include "libavcodec/mjpeg.h"

#include "avformat.h"
#include "avio_internal.h"
#include "mux.h"

/* Individual Image Attribute bits, from CIPA DC-007 Figure 8 */
enum MPFAttribute {
    MPF_DEPENDENT_PARENT = 0x80000000,
    MPF_DEPENDENT_CHILD  = 0x40000000,
    MPF_FORMAT_JPEG      = 0x00000000,
};

/* MP Index IFD tags, from CIPA DC-007 Table 3 */
enum MPFTag {
    MPF_TAG_MPFVersion      = 0xB000,
    MPF_TAG_NumberOfImages  = 0xB001,
    MPF_TAG_MPEntry         = 0xB002,
};

typedef enum MPFRole {
    MPF_ROLE_UNKNOWN = 0,
    MPF_ROLE_PRIMARY,
    MPF_ROLE_GAIN_MAP,
    MPF_ROLE_NB,
} MPFRole;

/* APP2 extension segments */
typedef enum MPFExt {
    MPF_EXT_INDEX,      /* MP Index IFD, First Individual Image only */
    MPF_EXT_ISO21496,   /* ISO 21496-1 version fields (in the base image) */
    MPF_EXT_NB,
} MPFExt;

static const struct {
    const char *name;
    uint32_t    type;  /* from CIPA DC-007 Table 4 */
} mpf_roles[MPF_ROLE_NB] = {
    [MPF_ROLE_PRIMARY]  = { "primary image",    0x030000 },
    [MPF_ROLE_GAIN_MAP] = { "gain map image",   0x050000 },
};

typedef struct MPFImage MPFImage;
struct MPFImage {
    AVPacket   *packet;
    int         index;
    MPFRole     role;

    /* Relative to the packet data (before splicing) */
    int         app2;        /* offset to role-specific APP2 header */
    int         splice_pos;  /* where in `packet->data` to splice our segments */

    /* Relative to the output file (after splicing) */
    int64_t     start, end;  /* start and end of this image in the output stream */
    int64_t     ext_size[MPF_EXT_NB]; /* APP2 payload sizes */

    /* Image dependency metadata */
    MPFImage   *dep;
    MPFImage   *rev_deps[2]; /* maximum 2 permissible by the spec */
    int         nb_rev_deps;
};

typedef struct MPFMuxContext {
    MPFImage *images;
} MPFMuxContext;

static int register_dep(AVFormatContext *ctx, MPFImage *img, MPFImage *base)
{
    if (base->nb_rev_deps == FF_ARRAY_ELEMS(base->rev_deps)) {
        av_log(ctx, AV_LOG_ERROR, "Stream %d cannot depend on %d, maximum "
               "number of dependents reached (%d).\n", img->index, base->index,
               (int) FF_ARRAY_ELEMS(base->rev_deps));
        return AVERROR(EINVAL);
    }

    base->rev_deps[base->nb_rev_deps++] = img;
    img->dep = base;
    return 0;
}

static int init_gain_map(AVFormatContext *ctx, const AVStreamGroup *sg)
{
    MPFMuxContext *s = ctx->priv_data;
    const AVStreamGroupLayeredVideo *lv = sg->params.layered_video;
    if (sg->nb_streams != 2 || lv->el_index >= sg->nb_streams)
        return AVERROR(EINVAL);

    const AVStream *bl = sg->streams[!lv->el_index];
    const AVStream *el = sg->streams[lv->el_index];
    MPFImage *base = &s->images[bl->index];
    MPFImage *gain = &s->images[el->index];

    if (base->index != 0) {
        av_log(ctx, AV_LOG_ERROR, "Stream group %u: the gain map base layer must "
               "be the primary image (stream 0), got stream %d\n",
               sg->index, base->index);
        return AVERROR(EINVAL);
    }

    if (gain->role != MPF_ROLE_UNKNOWN) {
        av_log(ctx, AV_LOG_ERROR, "Stream group %u: stream %d already used "
               "as %s for a different stream group.\n", sg->index, gain->index,
               mpf_roles[gain->role].name);
        return AVERROR(EINVAL);
    }

    for (int i = 0; i < base->nb_rev_deps; i++) {
        const MPFImage *other = base->rev_deps[i];
        if (other->role == MPF_ROLE_GAIN_MAP) {
            av_log(ctx, AV_LOG_ERROR, "Stream group %u: stream %d already "
                   "specified as a gain map for stream %d.\n", sg->index,
                   other->index, base->index);
            return AVERROR(EINVAL);
        }
    }

    gain->role = MPF_ROLE_GAIN_MAP;
    return register_dep(ctx, gain, base);
}

static int mpf_init(AVFormatContext *ctx)
{
    int ret;

    MPFMuxContext *s = ctx->priv_data;
    if (ctx->nb_streams < 2)
        av_log(ctx, AV_LOG_WARNING, "Specified only a single stream (no-op)\n");

    s->images = av_calloc(ctx->nb_streams, sizeof(*s->images));
    if (!s->images)
        return AVERROR(ENOMEM);

    for (int i = 0; i < ctx->nb_streams; i++) {
        MPFImage *img = &s->images[i];
        img->index  = i;
        img->start  = img->end = -1;
        for (int n = 0; n < MPF_EXT_NB; n++)
            img->ext_size[n] = -1;
        img->packet = av_packet_alloc();
        if (!img->packet)
            return AVERROR(ENOMEM);
    }

    /* Classify streams by their role */
    s->images[0].role = MPF_ROLE_PRIMARY;

    for (unsigned i = 0; i < ctx->nb_stream_groups; i++) {
        const AVStreamGroup *sg = ctx->stream_groups[i];
        switch (sg->type) {
        case AV_STREAM_GROUP_PARAMS_GAIN_MAP:
            if ((ret = init_gain_map(ctx, sg)) < 0)
                return ret;
            break;

        default:
            av_log(ctx, AV_LOG_ERROR, "Stream group %u: unknown type '%s'\n", i,
                   avformat_stream_group_name(sg->type));
            return AVERROR(EINVAL);
        }
    }

    for (int i = 0; i < ctx->nb_streams; i++) {
        if (s->images[i].role == MPF_ROLE_UNKNOWN) {
            av_log(ctx, AV_LOG_ERROR, "Missing stream group for stream %d.\n", i);
            return AVERROR(EINVAL);
        }
    }

    return 0;
}

static int parse_app2(AVFormatContext *ctx, MPFImage *img,
                      const GetByteContext *gb, int len)
{
#define CHECK_IDENT(strc) (len >= sizeof(strc) && !memcmp(gb->buffer, strc, sizeof(strc)))
    switch (img->role) {
    case MPF_ROLE_GAIN_MAP:
        if (CHECK_IDENT(AV_ISO21496_IDENTIFIER)) {
            if (img->app2) {
                av_log(ctx, AV_LOG_ERROR, "Stream %d: multiple ISO 21496-1 "
                        "APP2 segments\n", img->index);
                return AVERROR_INVALIDDATA;
            }

            if (len < sizeof(AV_ISO21496_IDENTIFIER) + sizeof(uint16_t[2])) {
                av_log(ctx, AV_LOG_ERROR, "Stream %d: ISO 21496-1 payload too "
                       "short\n", img->index);
                return AVERROR_INVALIDDATA;
            }
            img->app2 = bytestream2_tell(gb);
        }
        break;
    }
#undef CHECK_IDENT

    return 0;
}

static int mpf_write_packet(AVFormatContext *ctx, AVPacket *pkt)
{
    int ret;
    MPFMuxContext *s = ctx->priv_data;
    MPFImage *img = &s->images[pkt->stream_index];
    if (img->packet->size) {
        av_log(ctx, AV_LOG_ERROR, "Stream %d: expected a single JPEG image\n", img->index);
        return AVERROR(EINVAL);
    }

    GetByteContext gb;
    bytestream2_init(&gb, pkt->data, pkt->size);
    if (bytestream2_get_bytes_left(&gb) < 2 ||
        bytestream2_get_be16(&gb) != (0xFF00 | SOI))
    {
        av_log(ctx, AV_LOG_ERROR, "Stream %d: not a JPEG image (no SOI)\n", img->index);
        return AVERROR_INVALIDDATA;
    }

    /* Walk the JPEG marker sections up until the start of entropy coded data */
    while (bytestream2_get_bytes_left(&gb) >= 4) {
        const int pos = bytestream2_tell(&gb);
        if (bytestream2_get_byte(&gb) != 0xFF) {
            av_log(ctx, AV_LOG_ERROR, "Stream %d: malformed JPEG marker at 0x%x\n",
                   img->index, pos);
            return AVERROR_INVALIDDATA;
        }

        int marker = bytestream2_get_byte(&gb);
        if (marker == SOS || marker == TEM || (marker >= RST0 && marker <= EOI)) {
            if (!img->splice_pos)
                img->splice_pos = pos;
            break; /* start of entropy coded data */
        }

        int len = bytestream2_get_be16(&gb) - 2;
        if (len < 0 || len > bytestream2_get_bytes_left(&gb)) {
            av_log(ctx, AV_LOG_ERROR, "Stream %d: truncated marker segment at "
                   "0x%x\n", img->index, pos);
            return AVERROR_INVALIDDATA;
        }

        switch (marker) {
        case APP0:
        case APP1:
            break; /* ignored */
        case APP2:
            ret = parse_app2(ctx, img, &gb, len);
            if (ret < 0)
                return ret;
            av_fallthrough;
        default:
            /**
             * Splice position for the MPF metadata shall be the first segment
             * position after any SOI, APP0 or APP1 (cf. CIPA DC-007)
             */
            if (!img->splice_pos)
                img->splice_pos = pos;
            break;
        }

        bytestream2_skip(&gb, len);
    }

    /* Verify completeness of all needed information */
    switch (img->role) {
    case MPF_ROLE_PRIMARY:
        if (!img->splice_pos) {
            av_log(ctx, AV_LOG_ERROR, "Stream %d: truncated JPEG data\n", img->index);
            return AVERROR_INVALIDDATA;
        }
        break;
    case MPF_ROLE_GAIN_MAP:
        if (!img->app2) {
            av_log(ctx, AV_LOG_ERROR, "Stream %d was declared as a gain map "
                   "but is missing ISO 21496-1 metadata\n", img->index);
            return AVERROR_INVALIDDATA;
        }
        break;
    }

    return av_packet_ref(img->packet, pkt);
}

static void update_offset(AVIOContext *pb, int64_t base, int64_t *offset)
{
    int64_t val = avio_tell(pb) - base;
    av_assert0(val >= 0);
    av_assert0(*offset < 0 || *offset == val);
    *offset = val;
}

static int mpf_write_index(AVFormatContext *ctx, AVIOContext *pb, int64_t base,
                           MPFImage *img)
{
    const MPFMuxContext *s = ctx->priv_data;

    avio_w8(pb, 0xFF);
    avio_w8(pb, APP2);
    const int64_t app2_start = avio_tell(pb) - base;
    avio_wb16(pb, img->ext_size[MPF_EXT_INDEX]);
    avio_write(pb, "MPF\0", 4);

    /* every offset below is relative to this byte */
    int64_t tiff_base = avio_tell(pb) - base;
#define RELPOS(x) (avio_tell(pb) - base - tiff_base + (x))

    avio_write(pb, "MM\0*", 4); /* big endian */
    avio_wb32(pb, RELPOS(4)); /* offset to first IFD */

    /* First (and only) IFD */
    const int nb_tags = 3;  /* MPFVersion, NumberOfImages, MPEntry */
    avio_wb16(pb, nb_tags); /* number of IFD tags */

    /* MPFVersion tag */
    avio_wb16(pb, MPF_TAG_MPFVersion);
    avio_wb16(pb, AV_TIFF_UNDEFINED);
    avio_wb32(pb, 4); /* count */
    avio_write(pb, "0100", 4);

    /* NumberOfImages tag */
    avio_wb16(pb, MPF_TAG_NumberOfImages);
    avio_wb16(pb, AV_TIFF_LONG);
    avio_wb32(pb, 1);
    avio_wb32(pb, ctx->nb_streams);

    /* MPEntry tag */
    const int64_t entry_size = sizeof(int32_t[3]) + sizeof(int16_t[2]);
    avio_wb16(pb, MPF_TAG_MPEntry);
    avio_wb16(pb, AV_TIFF_UNDEFINED);
    avio_wb32(pb, ctx->nb_streams * entry_size);
    avio_wb32(pb, RELPOS(4 + 4)); /* offset to MPEntry data */
    avio_wb32(pb, 0); /* no MP Attribute IFD, per DC-007 6.1.1.1 */

    for (int i = 0; i < ctx->nb_streams; i++) {
        const MPFImage *entry = &s->images[i];
        int64_t size = entry->end - entry->start;
        av_assert0(size <= UINT32_MAX); /* would overflow null buffer anyways */
        uint32_t attr = MPF_FORMAT_JPEG | mpf_roles[entry->role].type;
        if (entry->nb_rev_deps)
            attr |= MPF_DEPENDENT_PARENT;
        if (entry->dep)
            attr |= MPF_DEPENDENT_CHILD;

        avio_wb32(pb, attr);
        avio_wb32(pb, size);

        /* The data offset of the First Individual Image is always zero */
        int64_t offset = i ? entry->start - tiff_base : 0;
        av_assert0(offset <= UINT32_MAX);
        avio_wb32(pb, offset);

        /* Indices are 1-based; 0 means "no dependent image" */
        for (int j = 0; j < FF_ARRAY_ELEMS(entry->rev_deps); j++) {
            const MPFImage *rdep = entry->rev_deps[j];
            avio_wb16(pb, rdep ? rdep->index + 1 : 0);
        }
    }
#undef RELPOS

    update_offset(pb, base + app2_start, &img->ext_size[MPF_EXT_INDEX]);
    return 0;
}

static void mpf_write_iso21496(AVIOContext *pb, MPFImage *src)
{
    avio_w8(pb, 0xFF);
    avio_w8(pb, APP2);

    const int64_t start = avio_tell(pb);
    avio_wb16(pb, src->ext_size[MPF_EXT_ISO21496]);

    /* The primary image contains a subset of the ISO 21496-1 struct, carrying
     * just the version header (two 16-bit integers), but no payload */
    avio_write(pb, AV_ISO21496_IDENTIFIER, sizeof(AV_ISO21496_IDENTIFIER));
    avio_write(pb, src->packet->data + src->app2 + sizeof(AV_ISO21496_IDENTIFIER),
               sizeof(uint16_t[2]));

    update_offset(pb, start, &src->ext_size[MPF_EXT_ISO21496]);
}

static int mpf_write_extensions(AVFormatContext *ctx, AVIOContext *pb,
                                int64_t base, MPFImage *img)
{
    int ret;

    switch (img->role) {
    case MPF_ROLE_PRIMARY:
        if (ctx->nb_streams > 1) {
            ret = mpf_write_index(ctx, pb, base, img);
            if (ret < 0)
                return ret;
        }
        break;
    }

    for (int i = 0; i < img->nb_rev_deps; i++) {
        MPFImage *rdep = img->rev_deps[i];
        switch (rdep->role) {
        case MPF_ROLE_GAIN_MAP:
            mpf_write_iso21496(pb, rdep);
            break;
        }
    }

    return 0;
}

static int mpf_write(AVFormatContext *ctx, AVIOContext *pb)
{
    MPFMuxContext *s = ctx->priv_data;
    const int64_t base = avio_tell(pb);
    int ret;

    for (int i = 0; i < ctx->nb_streams; i++) {
        MPFImage *img = &s->images[i];
        const AVPacket *pkt = img->packet;

        update_offset(pb, base, &img->start);
        avio_write(pb, pkt->data, img->splice_pos);
        if ((ret = mpf_write_extensions(ctx, pb, base, img)) < 0)
            return ret;
        avio_write(pb, pkt->data + img->splice_pos, pkt->size - img->splice_pos);
        update_offset(pb, base, &img->end);
    }

    return 0;
}

static int mpf_write_trailer(AVFormatContext *ctx)
{
    MPFMuxContext *s = ctx->priv_data;
    int ret;

    for (int i = 0; i < ctx->nb_streams; i++) {
        if (!s->images[i].packet->size) {
            av_log(ctx, AV_LOG_ERROR, "No image was supplied on stream %d\n", i);
            return AVERROR(EINVAL);
        }
    }

    /* Write the whole file once to a null buffer to settle offsets/sizes */
    AVIOContext *null;
    if ((ret = ffio_open_null_buf(&null)) < 0)
        return ret;
    ret = mpf_write(ctx, null);
    int size = ffio_close_null_buf(null);
    if (ret < 0)
        return ret;
    else if (size < 0)
        return size;

    for (int i = 0; i < ctx->nb_streams; i++) {
        MPFImage *img = &s->images[i];
        for (int j = 0; j < MPF_EXT_NB; j++) {
            if (img->ext_size[j] > UINT16_MAX) {
                av_log(ctx, AV_LOG_ERROR, "Stream %d: APP2 segment %d is too "
                       "large (%"PRId64" bytes)\n", img->index, j, img->ext_size[j]);
                return AVERROR(EINVAL);
            }
        }
    }

    return mpf_write(ctx, ctx->pb);
}

static void mpf_uninit(AVFormatContext *ctx)
{
    MPFMuxContext *s = ctx->priv_data;
    if (!s->images)
        return;

    for (int i = 0; i < ctx->nb_streams; i++)
        av_packet_free(&s->images[i].packet);

    av_freep(&s->images);
}

const FFOutputFormat ff_jpeg_mpf_muxer = {
    .p.name           = "jpeg_mpf",
    .p.long_name      = NULL_IF_CONFIG_SMALL("JPEG Multi-Picture Format"),
    .p.mime_type      = "image/jpeg",
    .p.extensions     = "jpg,jpeg",
    .priv_data_size   = sizeof(MPFMuxContext),
    .p.flags          = AVFMT_NOTIMESTAMPS | AVFMT_NODIMENSIONS,
    .p.video_codec    = AV_CODEC_ID_MJPEG,
    .flags_internal   = FF_OFMT_FLAG_ONLY_DEFAULT_CODECS,
    .init             = mpf_init,
    .deinit           = mpf_uninit,
    .write_packet     = mpf_write_packet,
    .write_trailer    = mpf_write_trailer,
    .interleave_packet = ff_interleave_packet_passthrough,
};
