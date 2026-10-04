/*
 * JPEG Multi-Picture Format demuxer
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
 * Demuxer for the JPEG Multi-Picture Format file format.
 */

#include <stdlib.h>
#include <string.h>

#include "libavutil/intreadwrite.h"
#include "libavutil/gain_map.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "libavcodec/bytestream.h"
#include "libavcodec/exif.h"
#include "libavcodec/mjpeg.h"

#include "avformat.h"
#include "demux.h"
#include "jpegmpf.h"
#include "internal.h"

typedef struct MPFDemuxImage {
    AVStream *st;
    int64_t  start;
    int64_t  size;

    uint32_t type;
    int      index; /* of the corresponding MP Entry */
    int      child; /* is a dependent image */
} MPFDemuxImage;

typedef struct MPFDemuxContext {
    const AVClass *class;
    AVRational framerate;
    int loop;

    MPFDemuxImage *images; /* sorted by position in the file */
    int nb_images;

    int cur_image;    /* index of next image to return */
    int64_t pts;      /* number of times all images were returned */
    int has_gain_map; /* the primary image carries ISO 21496-1 metadata */
    int passthrough;  /* not an MPF file */
    int first_img;    /* index of the first individual image */

    /* image data parsed during mpf_read_header() */
    AVPacket *initial;
    int64_t file_start;
} MPFDemuxContext;

#define CHECK_IDENT(strc) (len >= sizeof(strc) && !memcmp(gb->buffer, strc, sizeof(strc)))

static int mpf_probe(const AVProbeData *p)
{
    GetByteContext gbc, *gb = &gbc;
    bytestream2_init(gb, p->buf, p->buf_size);
    if (bytestream2_get_be16(gb) != (0xFF00 | SOI))
        return 0;

    while (bytestream2_get_bytes_left(gb) >= 4) {
        if (bytestream2_get_byte(gb) != 0xFF)
            return 0; /* not a JPEG marker */

        int marker = bytestream2_get_byte(gb);
        while (marker == 0xFF && bytestream2_get_bytes_left(gb) > 0) /* fill bytes */
            marker = bytestream2_get_byte(gb);

        if (marker == SOS || marker == TEM || (marker >= RST0 && marker <= EOI))
            return 0; /* start of entropy coded data */
        else if (marker == 0xFF || bytestream2_get_bytes_left(gb) < 2)
            break; /* need more data */

        const int len = bytestream2_get_be16(gb) - 2;
        if (len < 0)
            return 0;
        if (len > bytestream2_get_bytes_left(gb))
            break;

        switch (marker) {
        case APP2:
            /* Marker segments unique to MPF files; the base image of a gain
             * map file only carries the version fields of the ISO 21496-1
             * metadata (two 16-bit integers); we don't want to false positive
             * on standalone JPEGs that happen to contain gain map metadata */
            if (CHECK_IDENT("MPF") ||
                (CHECK_IDENT(AV_ISO21496_IDENTIFIER) &&
                 len == sizeof(AV_ISO21496_IDENTIFIER) + sizeof(uint16_t[2])))
                return AVPROBE_SCORE_EXTENSION + 10;
            break;
        }

        bytestream2_skip(gb, len);
    }

    /* If we ran out of probe bytes, we don't know yet whether this is an
     * MPF file or not, unless the extension is unique to MPF files */
    if (av_match_ext(p->filename, "mpo"))
        return AVPROBE_SCORE_EXTENSION + 5;

    return AVERROR(EAGAIN);
}

/* returns the offset of the first byte read */
static int grow_header(AVFormatContext *ctx, int size)
{
    MPFDemuxContext *s = ctx->priv_data;
    const int pos = s->initial->size;
    int ret = av_append_packet(ctx->pb, s->initial, size);
    if (ret < 0)
        return ret;
    if (ret < size)
        return AVERROR_INVALIDDATA;
    return pos;
}

static int parse_mp_index(AVFormatContext *ctx, const uint8_t *data, int len,
                          int64_t tiff_base)
{
    MPFDemuxContext *s = ctx->priv_data;
    AVExifMetadata ifd = { 0 };
    AVExifEntry *num = NULL, *entries = NULL;
    int ret;

    if (len < 2)
        return AVERROR_INVALIDDATA;
    const int le = AV_RB16(data) == AV_RB16("II");

    ret = av_exif_parse_buffer(ctx, data, len, &ifd, AV_EXIF_TIFF_HEADER);
    if (ret < 0)
        return ret;

    if ((ret = av_exif_get_entry(ctx, &ifd, MPF_TAG_NumberOfImages, 0, &num))     < 0 ||
        (ret = av_exif_get_entry(ctx, &ifd, MPF_TAG_MPEntry,        0, &entries)) < 0)
        goto fail;

    if (!num || num->type != AV_TIFF_LONG || num->count != 1 ||
        !entries || entries->type != AV_TIFF_UNDEFINED) {
        av_log(ctx, AV_LOG_ERROR, "Missing or malformed MP Index IFD\n");
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    /* The spec asserts MPEntry count = 16 * NumberOfImages, but tolerate
     * excess entries, e.g. left behind after deleting an image */
    const int entry_size = sizeof(uint32_t[3]) + sizeof(uint16_t[2]);
    const uint64_t nb_images = num->value.uint[0];
    if (!nb_images || nb_images * entry_size > entries->count) {
        av_log(ctx, AV_LOG_ERROR, "Invalid number of images: %"PRIu64"\n",
               nb_images);
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    s->images = av_calloc(nb_images, sizeof(*s->images));
    if (!s->images) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    GetByteContext gb;
    bytestream2_init(&gb, entries->value.ubytes, entries->count);

    const int explode = ctx->error_recognition & AV_EF_EXPLODE;
    for (int i = 0; i < nb_images; i++) {
        const uint32_t attr   = le ? bytestream2_get_le32u(&gb) : bytestream2_get_be32u(&gb);
        const uint32_t size   = le ? bytestream2_get_le32u(&gb) : bytestream2_get_be32u(&gb);
        const uint32_t offset = le ? bytestream2_get_le32u(&gb) : bytestream2_get_be32u(&gb);
        bytestream2_skipu(&gb, sizeof(uint16_t[2])); /* dependent images */

        if ((attr & MPF_ATTR_FORMAT_MASK) != MPF_FORMAT_JPEG) {
            const int fatal = !offset || explode;
            av_log(ctx, fatal ? AV_LOG_ERROR : AV_LOG_WARNING, "Image %d: "
                   "unsupported format 0x%"PRIx32"\n", i,
                   attr & MPF_ATTR_FORMAT_MASK);
            if (fatal) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            continue;
        }

        /* the First Individual Image is identified by an offset of 0 */
        if (!offset) {
            if (s->first_img >= 0) {
                av_log(ctx, AV_LOG_ERROR, "Duplicate First Individual Image\n");
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            s->first_img = s->nb_images;
        }

        MPFDemuxImage *img = &s->images[s->nb_images++];
        img->index = i;
        img->start = offset ? tiff_base + offset : s->file_start;
        img->size  = size;
        img->type  = attr & MPF_ATTR_TYPE_MASK;
        img->child = !!(attr & MPF_DEPENDENT_CHILD);
    }

    ret = 0;
fail:
    av_exif_free(&ifd);
    return ret;
}

static int parse_app2(AVFormatContext *ctx, int offset, int len)
{
    MPFDemuxContext *s = ctx->priv_data;
    GetByteContext gbc, *gb = &gbc;
    bytestream2_init(gb, s->initial->data + offset, len);

    if (CHECK_IDENT(AV_ISO21496_IDENTIFIER)) {
        s->has_gain_map = 1;
    } else if (CHECK_IDENT("MPF")) {
        if (s->images) {
            const int explode = ctx->error_recognition & AV_EF_EXPLODE;
            av_log(ctx, explode ? AV_LOG_ERROR : AV_LOG_WARNING,
                   "Duplicate MP Index IFD\n");
            return explode ? AVERROR_INVALIDDATA : 0;
        }

        /* MP offsets are relative to the start of the TIFF header */
        const int skip = sizeof("MPF");
        return parse_mp_index(ctx, gb->buffer + skip, len - skip,
                              s->file_start + offset + skip);
    }

    return 0;
}

static int cmp_image_start(const void *a, const void *b)
{
    const MPFDemuxImage *ia = a, *ib = b;
    if (ia->start != ib->start)
        return FFDIFFSIGN(ia->start, ib->start);
    return FFDIFFSIGN(ia->index, ib->index);
}

static int add_gain_map_group(AVFormatContext *ctx, const MPFDemuxImage *base,
                              const MPFDemuxImage *gain)
{
    if (gain == base) {
        av_log(ctx, AV_LOG_ERROR, "The first image cannot be a gain map\n");
        return AVERROR_INVALIDDATA;
    }

    AVStreamGroup *stg = avformat_stream_group_create(ctx, AV_STREAM_GROUP_PARAMS_GAIN_MAP, NULL);
    if (!stg)
        return AVERROR(ENOMEM);

    int ret;
    if ((ret = avformat_stream_group_add_stream(stg, base->st)) < 0 ||
        (ret = avformat_stream_group_add_stream(stg, gain->st)) < 0)
        return ret;

    stg->params.layered_video->el_index = stg->nb_entries - 1;
    return 0;
}

static int mpf_read_header(AVFormatContext *ctx)
{
    MPFDemuxContext *s = ctx->priv_data;
    int ret;

    s->first_img = -1;
    s->initial = av_packet_alloc();
    if (!s->initial)
        return AVERROR(ENOMEM);

    s->file_start = avio_tell(ctx->pb);
    if ((ret = grow_header(ctx, 2)) < 0)
        return ret;
    if (AV_RB16(s->initial->data) != (0xFF00 | SOI))
        return AVERROR_INVALIDDATA;

    /* Walk the JPEG marker sections up until the start of entropy coded data */
    for (;;) {
        const int pos = s->initial->size;
        if ((ret = grow_header(ctx, 1)) < 0)
            return ret;
        uint8_t marker = s->initial->data[ret];
        if (marker != 0xFF) {
            av_log(ctx, AV_LOG_ERROR, "Malformed JPEG marker at 0x%x\n", pos);
            return AVERROR_INVALIDDATA;
        }

        while (marker == 0xFF) {
            if ((ret = grow_header(ctx, 1)) < 0)
                return ret;
            marker = s->initial->data[ret];
        }

        if (marker == SOS || marker == TEM || (marker >= RST0 && marker <= EOI))
            break; /* start of entropy coded data */

        if ((ret = grow_header(ctx, 2)) < 0)
            return ret;
        const int len = AV_RB16(s->initial->data + ret) - 2;
        if (len < 0) {
            av_log(ctx, AV_LOG_ERROR, "Invalid marker segment at 0x%x\n", pos);
            return AVERROR_INVALIDDATA;
        }

        if ((ret = grow_header(ctx, len)) < 0)
            return ret;
        if (marker == APP2 && (ret = parse_app2(ctx, ret, len)) < 0)
            return ret;
    }

    if (!s->images) {
        /* Not an MPF file, just return as-is to the underlying decoder */
        const int64_t file_size = avio_size(ctx->pb);
        s->passthrough = file_size <= s->file_start;
        s->first_img = 0;
        s->nb_images = 1;
        s->images = av_malloc(sizeof(*s->images));
        if (!s->images)
            return AVERROR(ENOMEM);
        s->images[0] = (MPFDemuxImage){
            .type  = MPF_TYPE_PRIMARY,
            .start = s->file_start,
            .size  = s->passthrough ? s->initial->size
                                    : file_size - s->file_start,
        };
    }

    if (s->first_img < 0) {
        av_log(ctx, AV_LOG_ERROR, "No MP Entry for the first image\n");
        return AVERROR_INVALIDDATA;
    }

    /* Compatibility hack for libultrahdr, which produces invalid MPF files
     * whose gain maps have no MP type. Needed to support a large number
     * of JPEG Ultra HDR images in the wild */
    const MPFDemuxImage *first = &s->images[s->first_img];
    if (ctx->strict_std_compliance < FF_COMPLIANCE_STRICT &&
        s->has_gain_map && s->nb_images == 2)
    {
        MPFDemuxImage *other = &s->images[1 - s->first_img];
        if (other->type == MPF_TYPE_UNDEFINED)
            other->type = MPF_TYPE_GAIN_MAP;
    }

    for (int i = 0; i < s->nb_images; i++) {
        s->images[i].st = avformat_new_stream(ctx, NULL);
        if (!s->images[i].st)
            return AVERROR(ENOMEM);
    }

    for (int i = 0; i < s->nb_images; i++) {
        MPFDemuxImage *img = &s->images[i];
        AVStream *st = img->st;

        st->id = img->index;
        st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        st->codecpar->codec_id   = AV_CODEC_ID_MJPEG;
        if (img == first)
            st->disposition |= AV_DISPOSITION_DEFAULT;
        if (img->child || mpf_type_is_child(img->type))
            st->disposition |= AV_DISPOSITION_DEPENDENT;
        avpriv_set_pts_info(st, 64, s->framerate.den, s->framerate.num);
        st->avg_frame_rate = st->r_frame_rate = s->framerate;
        if (s->passthrough) /* we don't know the image size */
            ffstream(st)->need_parsing = AVSTREAM_PARSE_FULL;

        switch (img->type & MPF_TYPE_INFO_MASK) {
        case MPF_TYPE_GAIN_MAP:
            ret = add_gain_map_group(ctx, first, img);
            if (ret < 0)
                return ret;
            break;
        case MPF_TYPE_LARGE_THUMBNAIL:
        case MPF_TYPE_MULTI_FRAME:
        case MPF_TYPE_PRIMARY:
        case MPF_TYPE_ORIGINAL:
            /* no special handling */
            break;
        case MPF_TYPE_UNDEFINED:
        default:
            av_log(ctx, img->child ? AV_LOG_VERBOSE : AV_LOG_WARNING,
                   "Image %d: unhandled MP type 0x%06"PRIx32", treating as an "
                   "%s image\n", img->index, img->type,
                   img->child ? "unspecified dependent" : "unrelated");
            break;
        }
    }

    /* Sort the images array by file offset for mpf_read_packet() */
    qsort(s->images, s->nb_images, sizeof(*s->images), cmp_image_start);
    s->first_img = 0; /* by construction */

    for (int i = 0; i < s->nb_images - 1; i++) {
        MPFDemuxImage *img = &s->images[i], *next = &s->images[i + 1];
        if (next->start < img->start + img->size) {
            const int explode = ctx->error_recognition & AV_EF_EXPLODE;
            av_log(ctx, explode ? AV_LOG_ERROR : AV_LOG_WARNING,
                   "Image %d overlaps image %d%s\n", img->index, next->index,
                   explode ? "" : ", truncating");
            if (explode)
                return AVERROR_INVALIDDATA;
            img->size = next->start - img->start;
        }
    }

    return 0;
}

static int mpf_read_packet(AVFormatContext *ctx, AVPacket *pkt)
{
    MPFDemuxContext *s = ctx->priv_data;
    AVIOContext *pb = ctx->pb;
    int ret;

    if (s->cur_image >= s->nb_images && s->loop && !s->passthrough) {
        s->cur_image = 0;
        s->pts++;
    }

    if (s->cur_image >= s->nb_images) {
        if (!s->passthrough)
            return AVERROR_EOF;

        if ((ret = av_get_packet(pb, pkt, 4096)) < 0)
            return ret;
        pkt->flags &= ~AV_PKT_FLAG_CORRUPT; /* short read at EOF */
        pkt->stream_index = 0;
        return 0;
    }

    const MPFDemuxImage *img = &s->images[s->cur_image++];
    if (img->size > INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE)
        return AVERROR(ERANGE);

    if (s->initial) {
        av_packet_move_ref(pkt, s->initial);
        av_packet_free(&s->initial);
        av_shrink_packet(pkt, FFMIN(pkt->size, img->size));
    } else {
        int64_t pos = avio_seek(pb, img->start, SEEK_SET);
        if (pos < 0) {
            av_log(ctx, AV_LOG_ERROR, "Failed seeking to image %d at offset "
                   "%"PRId64"\n", img->st->id, img->start);
            return pos;
        }
    }

    if (pkt->size < img->size) {
        ret = av_append_packet(pb, pkt, img->size - pkt->size);
        if (ret < 0 && !pkt->size)
            return ret;
    }

    pkt->stream_index = img->st->index;
    pkt->pts = pkt->dts = s->pts;
    pkt->duration = 1;
    pkt->flags |= AV_PKT_FLAG_KEY;
    return 0;
}

static int mpf_read_seek(AVFormatContext *ctx, int stream_index,
                         int64_t timestamp, int flags)
{
    MPFDemuxContext *s = ctx->priv_data;
    if (s->passthrough || timestamp < 0 || (!s->loop && timestamp > 0))
        return -1;

    /* Don't seek if the initial packet is still pending; since this would
     * desynchronize the input stream otherwise */
    if (!s->initial) {
        int64_t pos = avio_seek(ctx->pb, s->images[0].start, SEEK_SET);
        if (pos < 0)
            return pos;
    }

    s->cur_image = 0;
    s->pts = timestamp;
    return 0;
}

static int mpf_read_close(AVFormatContext *ctx)
{
    MPFDemuxContext *s = ctx->priv_data;
    av_freep(&s->images);
    av_packet_free(&s->initial);
    return 0;
}

#define OFFSET(x) offsetof(MPFDemuxContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
static const AVOption mpf_options[] = {
    { "framerate", "set the video framerate", OFFSET(framerate), AV_OPT_TYPE_VIDEO_RATE, {.str = "25"}, 0, INT_MAX, DEC },
    { "loop",      "loop over the images in the file", OFFSET(loop), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, DEC },
    { NULL },
};

static const AVClass mpf_class = {
    .class_name = "jpeg_mpf demuxer",
    .item_name  = av_default_item_name,
    .option     = mpf_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFInputFormat ff_jpeg_mpf_demuxer = {
    .p.name         = "jpeg_mpf",
    .p.long_name    = NULL_IF_CONFIG_SMALL("JPEG Multi-Picture Format"),
    .p.priv_class   = &mpf_class,
    .p.extensions   = "mpo",
    .priv_data_size = sizeof(MPFDemuxContext),
    .flags_internal = FF_INFMT_FLAG_INIT_CLEANUP | FF_INFMT_FLAG_ID3V2_AUTO,
    .read_probe     = mpf_probe,
    .read_header    = mpf_read_header,
    .read_packet    = mpf_read_packet,
    .read_close     = mpf_read_close,
    .read_seek      = mpf_read_seek,
};
