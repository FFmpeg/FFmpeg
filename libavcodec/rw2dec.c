/*
 * Panasonic RW2 raw image decoder
 * Copyright (c) 2026 Lynne <dev@lynne.ee>
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
 * Panasonic RW2 decoder, RawFormat 8 only (S1II, S5II, GH6).
 * Bit-exact with LibRaw's pana8.cpp, quirks included.
 *
 * Input is the whole file as one packet.
 */

#include "libavutil/internal.h"
#include "libavutil/buffer.h"
#include "libavutil/mem.h"
#include "libavutil/raw_color_params.h"
#include "libavutil/reverse.h"

#define BITSTREAM_READER_LE
#define CACHED_BITSTREAM_READER 1
#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"
#include "exif.h"
#include "exif_internal.h"
#include "get_bits.h"
#include "hwaccel_internal.h"
#include "thread.h"
#include "tiff_common.h"

#include "rw2.h"

#define RW2_VLC_BITS   11
#define RW2_JOINT_BITS 16      /* longest code with its magnitude folded in */
#define RW2_SYM_DELTA  0x2000  /* joint symbols: delta + RW2_SYM_DELTA */
#define RW2_SYM_ESC    0x4000  /* other categories: RW2_SYM_ESC + category */

static av_always_inline int get_delta(const RW2DecContext *s,
                                      GetBitContext *bc, int *delta)
{
    int sym = get_vlc2(bc, s->vlc.table, RW2_VLC_BITS, 2);
    if (sym < RW2_SYM_ESC) {
        if (sym < 0)
            return AVERROR_INVALIDDATA;
        *delta = sym - RW2_SYM_DELTA;
        return 0;
    }

    /* Escapes read k - shift magnitude bits, MSB-first, the sign leading */
    int k = sym - RW2_SYM_ESC;
    int shift = s->h.c.code_shift[k], extra = k - shift;
    int d = 0;
    if (extra > 0) {
        unsigned v = get_bits(bc, extra);
        unsigned m = (ff_reverse[v & 0xFF] << 8 | ff_reverse[v >> 8]) >> (16 - extra);
        d = (m << shift) & 0xFFFF;
        if (!(v & 1))
            d += !shift - (1 << k);
    } else if (k && !show_bits(bc, 1)) {
        /* No magnitude, but LibRaw still tests the next bit for the sign */
        d = !shift - (1 << k);
    }

    *delta = d + (shift ? 1 << (shift - 1) : 0);
    return 0;
}

static int gamma_curve(const RW2Coding *c, uint32_t v)
{
    int seg = 0;
    while (seg < 5 && v >= c->gamma_point[seg + 1])
        seg++;

    /* LibRaw only keeps the second half of each 0x3A pair, which makes the
     * output offset of every segment 0 */
    uint32_t sh = c->gamma_shift[seg];
    uint32_t r  = v - c->gamma_point[seg];
    if ((sh & 0x1F) == 31)
        r = seg == 5 ? 0xFFFF : 0;
    else if (sh & 0x10)
        r <<= sh & 0xF;
    else if ((sh & 0x1F) == 15)
        r = 0;
    else if (sh & 0x1F)
        r = (r + (1 << ((sh & 0x1F) - 1))) >> (sh & 0x1F);

    return FFMIN(r, c->datamax);
}

static int build_tables(RW2DecContext *s)
{
    const RW2Coding *c = &s->h.c;
    int nb = 0;

    /* A code per magnitude where both fit and nothing is shifted, the rest escape */
    for (int k = 0; k < 17; k++) {
        int len = c->code_len[k], code = c->code_val[k];
        if (!len)
            continue;
        if (c->code_shift[k] || k >= RW2_JOINT_CATS || len + k > RW2_JOINT_BITS) {
            s->codes[nb++] = (RW2Code){ len, code, RW2_SYM_ESC + k };
            continue;
        }
        for (int m = 0; m < (1 << k); m++) {
            int d = k && !(m >> (k - 1)) ? m + 1 - (1 << k) : m;
            s->codes[nb++] = (RW2Code){ len + k, code << k | m, RW2_SYM_DELTA + d };
        }
    }

    ff_vlc_free(&s->vlc);
    int ret = ff_vlc_init_sparse(&s->vlc, RW2_VLC_BITS, nb,
                                 &s->codes[0].len,  sizeof(*s->codes), 1,
                                 &s->codes[0].code, sizeof(*s->codes), 2,
                                 &s->codes[0].sym,  sizeof(*s->codes), 2,
                                 VLC_INIT_OUTPUT_LE);
    if (ret < 0)
        return ret;

    /* The gamma LUT, an identity on every file seen so far */
    s->use_gamma = 0;
    for (int i = 0; i <= c->datamax; i++) {
        s->gamma[i] = gamma_curve(c, i);
        s->use_gamma |= s->gamma[i] != i;
    }

    return 0;
}

static int decode_stripe(AVCodecContext *avctx, void *tdata,
                         int jobnr, int threadnr)
{
    RW2DecContext *s = avctx->priv_data;
    AVFrame *frame = tdata;
    const RW2Stripe *st = &s->h.stripes[jobnr];
    const ptrdiff_t stride = frame->linesize[0];
    const uint16_t *gamma = s->use_gamma ? s->gamma : NULL;
    const int datamax = s->h.c.datamax;
    int line_base[4], cur[4];
    GetBitContext bc;
    int ret;

    ret = init_get_bits(&bc, s->payload + st->offset, st->size_bits);
    if (ret < 0)
        return ret;

    for (int i = 0; i < 4; i++)
        line_base[i] = s->h.c.initial[i];

    for (int y = 0; y < st->height; y += 2) {
        uint16_t *dst0 = (uint16_t *)(frame->data[0] + y*stride) + st->left;
        uint16_t *dst1 = (uint16_t *)((uint8_t *)dst0 + stride);

        /* Each sample predicts from the same position in the previous 2x2 */
        memcpy(cur, line_base, sizeof(cur));
        for (int x = 0; x < st->width; x += 2) {
            /* Coding order: top-left, bottom-left, top-right, bottom-right */
            static const uint8_t slot[4] = { 0, 2, 1, 3 };
            for (int i = 0; i < 4; i++) {
                int delta, j = slot[i];
                if ((ret = get_delta(s, &bc, &delta)) < 0)
                    return ret;
                cur[j] = av_clip(cur[j] + delta, 0, datamax);
            }

            if (gamma) {
                dst0[x] = gamma[cur[0]]; dst0[x + 1] = gamma[cur[1]];
                dst1[x] = gamma[cur[2]]; dst1[x + 1] = gamma[cur[3]];
            } else {
                dst0[x] = cur[0]; dst0[x + 1] = cur[1];
                dst1[x] = cur[2]; dst1[x + 1] = cur[3];
            }

            if (!x)
                memcpy(line_base, cur, sizeof(line_base));
        }

        if (get_bits_left(&bc) < 0)
            return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int read_array_count(GetByteContext *gb, unsigned type, unsigned count,
                            unsigned expected, int max)
{
    int n;
    if (type != AV_TIFF_UNDEFINED || count != expected)
        return 0;
    n = bytestream2_get_le16(gb);
    return FFMIN(n, max);
}

/* Tag layouts and clamps follow LibRaw */
static int parse_header(RW2Header *h, const uint8_t *data, int size)
{
    RW2Coding *c = &h->c;
    GetByteContext gb;

    memset(h, 0, sizeof(*h));

    bytestream2_init(&gb, data, size);
    if (bytestream2_get_le32(&gb) != MKTAG('I', 'I', 'U', '\0'))
        return AVERROR_INVALIDDATA;
    int64_t ifd0 = bytestream2_get_le32(&gb);
    if (ifd0 < 8 || ifd0 + 2 > size)
        return AVERROR_INVALIDDATA;
    bytestream2_seek(&gb, ifd0, SEEK_SET);

    int nb_entries = bytestream2_get_le16(&gb);
    if (nb_entries > RW2_MAX_IFD0_ENTRIES || bytestream2_get_bytes_left(&gb) < nb_entries*12)
        return AVERROR_INVALIDDATA;

    int64_t ifd_end = ifd0 + 2 + 12*(int64_t)nb_entries + 4;
    h->exif_off = ifd0;
    h->exif_end = size;

    for (int i = 0; i < nb_entries; i++) {
        unsigned tag, type, count, n, v;
        int next;

        if (ff_tread_tag(&gb, 1, &tag, &type, &count, &next) < 0)
            goto next;

        /* Keep the standard tags verbatim for EXIF in this same walk, leaving
         * out the Panasonic-private ones and the orientation that is exported
         * as a display matrix; the previews mark where the metadata ends */
        const uint8_t *rec = data + ifd0 + 2 + 12*i;
        if ((tag == 0x2E || tag == 0x111 || tag == 0x127) && AV_RL32(rec + 8) >= ifd_end)
            h->exif_end = FFMIN(h->exif_end, (int64_t)AV_RL32(rec + 8));
        if (tag >= 0x100 && tag != 0x111 && tag != 0x112 && tag != 0x117 &&
            !(tag >= 0x118 && tag <= 0x127)) {
            memcpy(h->exif + h->exif_count*12, rec, 12);
            h->exif_count++;
        }

        switch (tag) {
        case 0x02: h->raw_width  = ff_tget(&gb, type, 1); break;
        case 0x03: h->raw_height = ff_tget(&gb, type, 1); break;
        case 0x04: case 0x05: case 0x06: case 0x07:
            h->border[tag - 0x04] = ff_tget(&gb, type, 1);
            break;
        case 0x09: h->cfa = ff_tget(&gb, type, 1); break;
        case 0x0A: h->bpp = ff_tget(&gb, type, 1); break;
        case 0x0E: case 0x0F: case 0x10:
            h->linearity[tag - 0x0E] = ff_tget(&gb, type, 1);
            break;
        case 0x1C: case 0x1D: case 0x1E:
            h->black[tag - 0x1C] = ff_tget(&gb, type, 1);
            break;
        case 0x24: case 0x25: case 0x26:
            h->wb[tag - 0x24] = ff_tget(&gb, type, 1);
            break;
        case 0x2D: h->raw_format = ff_tget(&gb, type, 1); break;
        case 0x39:
            n = read_array_count(&gb, type, count, 26, 6);
            for (int j = 0; j < n; j++)
                c->gamma_shift[j] = bytestream2_get_le32(&gb);
            break;
        case 0x3A:
            n = read_array_count(&gb, type, count, 26, 6);
            for (int j = 0; j < n; j++) {
                bytestream2_skip(&gb, 2);
                c->gamma_point[j] = bytestream2_get_le16(&gb);
            }
            break;
        case 0x3B: c->datamax = ff_tget(&gb, type, 1) & 0xFFFF; break;
        case 0x3C: case 0x3D: case 0x3E: case 0x3F:
            c->initial[tag - 0x3C] = ff_tget(&gb, type, 1) & 0xFFFF;
            break;
        case 0x40:
            n = read_array_count(&gb, type, count, 70, 17);
            for (int k = 0; k < n; k++) {
                v = bytestream2_get_le16(&gb);
                int len = FFMIN(v, 16);
                v = bytestream2_get_le16(&gb);
                int code = FFMIN(v, 0xFFF) & ((1 << len) - 1);

                /* LibRaw takes the first match, drop the codes it never reaches */
                for (int j = 0; j < k && len; j++)
                    if (c->code_len[j] && c->code_len[j] <= len &&
                        code >> (len - c->code_len[j]) == c->code_val[j])
                        len = 0;
                c->code_len[k] = len;
                c->code_val[k] = code;
            }
            break;
        case 0x41:
            n = read_array_count(&gb, type, count, 36, 17);
            for (int j = 0; j < n; j++) {
                v = bytestream2_get_le16(&gb);
                c->code_shift[j] = FFMIN(v, 64) & 0x1F;
            }
            break;
        case 0x42:
            v = ff_tget(&gb, type, 1);
            h->nb_stripes = FFMIN(v, RW2_MAX_STRIPES);
            break;
        case 0x44: case 0x45: case 0x46:
            n = read_array_count(&gb, type, count, 50, RW2_MAX_STRIPES);
            for (int j = 0; j < n; j++) {
                v = bytestream2_get_le32(&gb);
                if (tag == 0x44)
                    h->stripes[j].offset = v;
                else if (tag == 0x45)
                    h->stripes[j].left = v;
                else
                    h->stripes[j].size_bits = v;
            }
            break;
        case 0x47: case 0x48:
            n = read_array_count(&gb, type, count, 26, RW2_MAX_STRIPES);
            for (int j = 0; j < n; j++) {
                v = bytestream2_get_le16(&gb);
                if (tag == 0x47)
                    h->stripes[j].width = v;
                else
                    h->stripes[j].height = v;
            }
            break;
        case 0x112: h->orientation = ff_tget(&gb, type, 1); break;
        }

next:
        bytestream2_seek(&gb, next, SEEK_SET);
    }

    return 0;
}

static int check_stripes(AVCodecContext *avctx, const RW2Header *h,
                         int payload_size)
{
    uint32_t left = 0;

    if (h->nb_stripes < 1)
        return AVERROR_INVALIDDATA;

    /* Stripes must tile the image exactly */
    for (int i = 0; i < h->nb_stripes; i++) {
        const RW2Stripe *st = &h->stripes[i];
        if (st->left != left || st->height != h->raw_height ||
            !st->width || (st->width & 1) || st->width > h->raw_width - left ||
            !st->size_bits || st->size_bits > INT_MAX - 7 ||
            st->offset > payload_size ||
            (st->size_bits + 7) >> 3 > payload_size - st->offset) {
            av_log(avctx, AV_LOG_ERROR, "Invalid stripe %d\n", i);
            return AVERROR_INVALIDDATA;
        }
        left += st->width;
    }

    if (left != h->raw_width)
        return AVERROR_INVALIDDATA;

    return 0;
}

static enum AVPixelFormat get_pixel_format(AVCodecContext *avctx,
                                           enum AVPixelFormat pix_fmt)
{
    enum AVPixelFormat pix_fmts[] = {
        pix_fmt,
        AV_PIX_FMT_NONE,
    };

    return ff_get_format(avctx, pix_fmts);
}

/* The TIFF and EXIF tags of the file. Panasonic's own tags, below 0x100 and
 * at 0x118 - 0x127, hold the raw parameters exported as side data and the
 * previews; the strips point into the file. */
static int export_exif(AVCodecContext *avctx, AVFrame *frame,
                       const uint8_t *data, int size)
{
    const RW2Header *h = &((RW2DecContext *)avctx->priv_data)->h;
    int64_t off = h->exif_off, keep = FFMIN(h->exif_end, size);

    if (!h->exif_count || off + 2 + (int64_t)h->exif_count*12 + 4 > keep)
        return 0;

    /* The metadata precedes the embedded previews, so attach it whole, pointing
     * IFD0 at the standard tags gathered during the header walk */
    uint8_t *buf = av_memdup(data, keep);
    if (!buf)
        return AVERROR(ENOMEM);

    AV_WL16(buf + 2, 42);                                   /* "IIU\0" -> TIFF */
    AV_WL16(buf + off, h->exif_count);
    memcpy(buf + off + 2, h->exif, h->exif_count * 12);
    AV_WL32(buf + off + 2 + h->exif_count*12, 0);           /* no next IFD */

    AVBufferRef *bref = av_buffer_create(buf, keep, NULL, NULL, 0);
    if (!bref) {
        av_free(buf);
        return AVERROR(ENOMEM);
    }

    return ff_frame_new_side_data_from_buf(avctx, frame, AV_FRAME_DATA_EXIF, &bref);
}

static int export_metadata(AVCodecContext *avctx, AVFrame *frame,
                           const uint8_t *hdr, int hdr_size)
{
    RW2DecContext *s = avctx->priv_data;
    const RW2Header *h = &s->h;
    int ret;

    /* Even crops keep the CFA phase */
    if (h->border[0] >= 0 && h->border[1] >= 0 &&
        h->border[0] < h->border[2] && h->border[2] <= h->raw_height &&
        h->border[1] < h->border[3] && h->border[3] <= h->raw_width) {
        frame->crop_top    = h->border[0] & ~1;
        frame->crop_left   = h->border[1] & ~1;
        frame->crop_bottom = (h->raw_height - h->border[2]) & ~1;
        frame->crop_right  = (h->raw_width  - h->border[3]) & ~1;
    }

    if (h->orientation > 1 && h->orientation <= 8) {
        AVFrameSideData *sd;
        ret = ff_frame_new_side_data(avctx, frame, AV_FRAME_DATA_DISPLAYMATRIX,
                                     sizeof(int32_t) * 9, &sd);
        if (ret < 0)
            return ret;
        if (sd)
            av_exif_orientation_to_matrix((int32_t *)sd->data, h->orientation);
    }

    /* Per-channel black levels are not representable, use green's */
    int white = FFMAX3(h->linearity[0], h->linearity[1], h->linearity[2]);
    if (!white)
        white = (1 << h->bpp) - 1;

    AVRawColorParams *rcp = av_raw_color_params_create_side_data(frame);
    if (!rcp)
        return AVERROR(ENOMEM);
    rcp->black_level = av_make_q(h->black[1], 65535);
    rcp->white_level = av_make_q(white, 65535);

    if (h->wb[1]) {
        rcp->type = AV_RAW_COLOR_PARAMS_RW2;
        rcp->codec.rw2.wb_red  = av_make_q(h->wb[0], h->wb[1]);
        rcp->codec.rw2.wb_blue = av_make_q(h->wb[2], h->wb[1]);
    }

    return export_exif(avctx, frame, hdr, hdr_size);
}

static int decode_frame(AVCodecContext *avctx, AVFrame *frame,
                        int *got_frame, AVPacket *avpkt)
{
    RW2DecContext *s = avctx->priv_data;
    RW2Header *h = &s->h;
    int ret;

    static const enum AVPixelFormat cfa_fmts[] = {
        AV_PIX_FMT_BAYER_RGGB16, AV_PIX_FMT_BAYER_GRBG16,
        AV_PIX_FMT_BAYER_GBRG16, AV_PIX_FMT_BAYER_BGGR16,
    };

    ret = parse_header(h, avpkt->data, avpkt->size);
    if (ret < 0)
        return ret;

    if (h->raw_format != 8) {
        if (!h->raw_format)
            return AVERROR_INVALIDDATA;
        avpriv_request_sample(avctx, "RawFormat %d", h->raw_format);
        return AVERROR_PATCHWELCOME;
    }

    if (h->cfa < 1 || h->cfa > 4 || h->bpp < 8 || h->bpp > 16 ||
        (h->raw_width & 1) || (h->raw_height & 1))
        return AVERROR_INVALIDDATA;

    ret = ff_set_dimensions(avctx, h->raw_width, h->raw_height);
    if (ret < 0)
        return ret;

    s->payload = avpkt->data;

    ret = check_stripes(avctx, h, avpkt->size);
    if (ret < 0)
        return ret;

    if (cfa_fmts[h->cfa - 1] != s->pix_fmt) {
        s->pix_fmt = cfa_fmts[h->cfa - 1];
        ret = get_pixel_format(avctx, s->pix_fmt);
        if (ret < 0)
            return ret;
        avctx->pix_fmt = ret;
    }
    avctx->bits_per_raw_sample = h->bpp;

    if (avctx->skip_frame >= AVDISCARD_ALL)
        return avpkt->size;

    ret = ff_thread_get_buffer(avctx, frame, 0);
    if (ret < 0)
        return ret;

    s->frame = frame;

    ret = ff_hwaccel_frame_priv_alloc(avctx, &s->hwaccel_picture_private);
    if (ret < 0)
        return ret;

    ff_thread_finish_setup(avctx);

    if (avctx->hwaccel) {
        const FFHWAccel *hwaccel = ffhwaccel(avctx->hwaccel);

        ret = hwaccel->start_frame(avctx, avpkt->buf, avpkt->data, avpkt->size);
        if (ret < 0)
            return ret;

        for (int i = 0; i < h->nb_stripes; i++) {
            const RW2Stripe *st = &h->stripes[i];
            ret = hwaccel->decode_slice(avctx, s->payload + st->offset,
                                        (st->size_bits + 7) >> 3);
            if (ret < 0)
                return ret;
        }

        ret = hwaccel->end_frame(avctx);
        if (ret < 0)
            return ret;

        av_refstruct_unref(&s->hwaccel_picture_private);
    } else {
        int rets[RW2_MAX_STRIPES];

        ret = build_tables(s);
        if (ret < 0)
            return ret;

        avctx->execute2(avctx, decode_stripe, frame, rets, h->nb_stripes);
        for (int i = 0; i < h->nb_stripes; i++) {
            if (rets[i] < 0) {
                av_log(avctx, AV_LOG_ERROR, "Error decoding stripe %d\n", i);
                return rets[i];
            }
        }
    }

    frame->pict_type = AV_PICTURE_TYPE_I;
    frame->flags    |= AV_FRAME_FLAG_KEY;

    ret = export_metadata(avctx, frame, avpkt->data, avpkt->size);
    if (ret < 0)
        return ret;

    *got_frame = 1;

    return avpkt->size;
}

#if HAVE_THREADS
static int update_thread_context(AVCodecContext *dst, const AVCodecContext *src)
{
    RW2DecContext *ssrc = src->priv_data;
    RW2DecContext *sdst = dst->priv_data;

    sdst->pix_fmt = ssrc->pix_fmt;

    return 0;
}
#endif

static av_cold int decode_init(AVCodecContext *avctx)
{
    RW2DecContext *s = avctx->priv_data;

    avctx->color_trc       = AVCOL_TRC_LINEAR;
    avctx->color_primaries = AVCOL_PRI_UNSPECIFIED;
    avctx->colorspace      = AVCOL_SPC_UNSPECIFIED;

    s->pix_fmt = AV_PIX_FMT_NONE;

    return 0;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    RW2DecContext *s = avctx->priv_data;
    av_refstruct_unref(&s->hwaccel_picture_private);
    ff_vlc_free(&s->vlc);
    return 0;
}

const FFCodec ff_rw2_decoder = {
    .p.name         = "rw2",
    CODEC_LONG_NAME("Panasonic RW2"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_RW2,
    .priv_data_size = sizeof(RW2DecContext),
    .init           = decode_init,
    .close          = decode_end,
    FF_CODEC_DECODE_CB(decode_frame),
    UPDATE_THREAD_CONTEXT(update_thread_context),
    .p.capabilities = AV_CODEC_CAP_DR1 |
                      AV_CODEC_CAP_FRAME_THREADS |
                      AV_CODEC_CAP_SLICE_THREADS,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP |
                      FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM,
};
