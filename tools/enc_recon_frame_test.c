/*
 * copyright (c) 2022 Anton Khirnov <anton@khirnov.net>
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

/* A test for AV_CODEC_FLAG_RECON_FRAME
 * TODO: dump reconstructed frames to disk */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "decode_simple.h"

#include "libavutil/adler32.h"
#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/error.h"
#include "libavutil/frame.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "libavformat/avformat.h"

#include "libavcodec/avcodec.h"
#include "libavcodec/codec.h"

#include "libswscale/swscale.h"

typedef struct FrameChecksum {
    int64_t  ts;
    uint32_t checksum[4];
} FrameChecksum;

typedef struct PrivData {
    AVCodecContext *enc;
    AVCodecContext *dec;

    int64_t pts_in;

    AVPacket *pkt;
    AVFrame  *frame, *frame_recon;

    struct SwsContext *scaler;

    FrameChecksum *checksums_decoded;
    size_t      nb_checksums_decoded;
    FrameChecksum *checksums_recon;
    size_t      nb_checksums_recon;
} PrivData;

static int frame_hash(FrameChecksum **pc, size_t *nb_c, int64_t ts,
                      const AVFrame *frame)
{
    FrameChecksum *c;
    int shift_h[4] = { 0 }, shift_v[4] = { 0 };

    c = av_realloc_array(*pc, *nb_c + 1, sizeof(*c));
    if (!c)
        return AVERROR(ENOMEM);
    *pc = c;
    (*nb_c)++;

    c += *nb_c - 1;
    memset(c, 0, sizeof(*c));

    av_pix_fmt_get_chroma_sub_sample(frame->format, &shift_h[1], &shift_v[1]);
    shift_h[2] = shift_h[1];
    shift_v[2] = shift_v[1];

    c->ts = ts;
    for (int p = 0; frame->data[p]; p++) {
        const uint8_t *data = frame->data[p];
        int linesize = av_image_get_linesize(frame->format, frame->width, p);
        uint32_t checksum = 0;

        av_assert0(linesize >= 0);

        for (int j = 0; j < frame->height >> shift_v[p]; j++) {
            checksum = av_adler32_update(checksum, data, linesize);
            data += frame->linesize[p];
        }

        c->checksum[p] = checksum;
    }

    return 0;
}

static int recon_frame_process(PrivData *pd, const AVPacket *pkt)
{
    AVFrame *f = pd->frame_recon;
    int ret;

    ret = avcodec_receive_frame(pd->enc, f);
    if (ret < 0) {
        fprintf(stderr, "Error retrieving a reconstructed frame\n");
        return ret;
    }

    // the encoder's internal format (in which the reconsturcted frames are
    // exported) may be different from the user-facing pixel format
    if (f->format != pd->enc->pix_fmt) {
        if (!pd->scaler) {
            pd->scaler = sws_getContext(f->width, f->height, f->format,
                                        f->width, f->height, pd->enc->pix_fmt,
                                        SWS_BITEXACT, NULL, NULL, NULL);
            if (!pd->scaler)
                return AVERROR(ENOMEM);
        }

        ret = sws_scale_frame(pd->scaler, pd->frame, f);
        if (ret < 0) {
            fprintf(stderr, "Error converting pixel formats\n");
            return ret;
        }

        av_frame_unref(f);
        f = pd->frame;
    }

    ret = frame_hash(&pd->checksums_recon, &pd->nb_checksums_recon,
                     pkt->pts, f);
    av_frame_unref(f);

    return 0;
}

static int process_frame(DecodeContext *dc, AVFrame *frame)
{
    PrivData *pd = dc->opaque;
    int ret;

    if (!avcodec_is_open(pd->enc)) {
        if (!frame) {
            fprintf(stderr, "No input frames were decoded\n");
            return AVERROR_INVALIDDATA;
        }

        pd->enc->width        = frame->width;
        pd->enc->height       = frame->height;
        pd->enc->pix_fmt      = frame->format;
        pd->enc->thread_count = dc->decoder->thread_count;
        pd->enc->thread_type  = dc->decoder->thread_type;

        // real timestamps do not matter for this test, so we just
        // pretend the input is 25fps CFR to avoid any timestamp issues
        pd->enc->time_base    = (AVRational){ 1, 25 };

        ret = avcodec_open2(pd->enc, NULL, NULL);
        if (ret < 0) {
            fprintf(stderr, "Error opening the encoder\n");
            return ret;
        }
    }

    if (frame) {
        frame->pts       = pd->pts_in++;

        // avoid forcing coded frame type
        frame->pict_type = AV_PICTURE_TYPE_NONE;
    }

    ret = avcodec_send_frame(pd->enc, frame);
    if (ret == AVERROR_EOF && !frame)
        return 0;
    if (ret < 0) {
        fprintf(stderr, "Error submitting a frame for encoding\n");
        return ret;
    }

    while (1) {
        AVPacket *pkt = pd->pkt;

        ret = avcodec_receive_packet(pd->enc, pkt);
        if (ret == AVERROR(EAGAIN))
            break;
        else if (ret == AVERROR_EOF)
            pkt = NULL;
        else if (ret < 0) {
            fprintf(stderr, "Error receiving a frame from the encoder\n");
            return ret;
        }

        if (pkt) {
            ret = recon_frame_process(pd, pkt);
            if (ret < 0)
                return ret;
        }

        if (!avcodec_is_open(pd->dec)) {
            if (!pkt) {
                fprintf(stderr, "No packets were received from the encoder\n");
                return AVERROR(EINVAL);
            }

            pd->dec->width        = pd->enc->width;
            pd->dec->height       = pd->enc->height;
            pd->dec->pix_fmt      = pd->enc->pix_fmt;
            pd->dec->thread_count = dc->decoder->thread_count;
            pd->dec->thread_type  = dc->decoder->thread_type;
            if (pd->enc->extradata_size) {
                pd->dec->extradata = av_memdup(pd->enc->extradata,
                                               pd->enc->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
                if (!pd->dec->extradata)
                    return AVERROR(ENOMEM);
            }

            ret = avcodec_open2(pd->dec, NULL, NULL);
            if (ret < 0) {
                fprintf(stderr, "Error opening the decoder\n");
                return ret;
            }
        }

        ret = avcodec_send_packet(pd->dec, pkt);
        if (ret < 0) {
            fprintf(stderr, "Error sending a packet to decoder\n");
            return ret;
        }

        while (1) {
            ret = avcodec_receive_frame(pd->dec, pd->frame);
            if (ret == AVERROR(EAGAIN))
                break;
            else if (ret == AVERROR_EOF)
                return 0;
            else if (ret < 0) {
                fprintf(stderr, "Error receving a frame from decoder\n");
                return ret;
            }

            ret = frame_hash(&pd->checksums_decoded, &pd->nb_checksums_decoded,
                             pd->frame->pts, pd->frame);
            av_frame_unref(pd->frame);
            if (ret < 0)
                return ret;
        }

    }

    return 0;
}

static int frame_checksum_compare(const void *a, const void *b)
{
    const FrameChecksum *ca = a;
    const FrameChecksum *cb = b;
    if (ca->ts == cb->ts)
        return 0;
    return FFSIGN(ca->ts - cb->ts);
}

int main(int argc, char **argv)
{
    PrivData      pd;
    DecodeContext dc;

    const char *filename, *enc_name, *enc_opts, *thread_type = NULL, *nb_threads = NULL;
    const AVCodec *enc, *dec;
    int ret = 0, max_frames = 0;

    if (argc < 4) {
        fprintf(stderr,
                "Usage: %s <input file> <encoder> <encoder options> "
                "[<max frame count> [<thread count> <thread type>]\n",
                argv[0]);
        return 0;
    }

    filename     = argv[1];
    enc_name     = argv[2];
    enc_opts     = argv[3];
    if (argc >= 5)
        max_frames = strtol(argv[4], NULL, 0);
    if (argc >= 6)
        nb_threads = argv[5];
    if (argc >= 7)
        thread_type = argv[6];

    memset(&dc, 0, sizeof(dc));
    memset(&pd, 0, sizeof(pd));

    enc = avcodec_find_encoder_by_name(enc_name);
    if (!enc) {
        fprintf(stderr, "No such encoder: %s\n", enc_name);
        return 1;
    }
    if (!(enc->capabilities & AV_CODEC_CAP_ENCODER_RECON_FRAME)) {
        fprintf(stderr, "Encoder '%s' cannot output reconstructed frames\n",
                enc->name);
        return 1;
    }

    dec = avcodec_find_decoder(enc->id);
    if (!dec) {
        fprintf(stderr, "No decoder for: %s\n", avcodec_get_name(enc->id));
        return 1;
    }

    pd.enc = avcodec_alloc_context3(enc);
    if (!pd.enc) {
        fprintf(stderr, "Error allocating encoder\n");
        return 1;
    }

    ret = av_set_options_string(pd.enc, enc_opts, "=", ",");
    if (ret < 0) {
        fprintf(stderr, "Error setting encoder options\n");
        goto fail;
    }
    pd.enc->flags |= AV_CODEC_FLAG_RECON_FRAME | AV_CODEC_FLAG_BITEXACT;

    pd.dec = avcodec_alloc_context3(dec);
    if (!pd.dec) {
        fprintf(stderr, "Error allocating decoder\n");
        goto fail;
    }

    pd.dec->flags           |= AV_CODEC_FLAG_BITEXACT;
    pd.dec->err_recognition |= AV_EF_CRCCHECK;

    pd.frame       = av_frame_alloc();
    pd.frame_recon = av_frame_alloc();
    pd.pkt         = av_packet_alloc();
    if (!pd.frame ||!pd.frame_recon || !pd.pkt) {
        ret = 1;
        goto fail;
    }

    ret = ds_open(&dc, filename, 0);
    if (ret < 0) {
        fprintf(stderr, "Error opening the file\n");
        goto fail;
    }

    dc.process_frame = process_frame;
    dc.opaque        = &pd;
    dc.max_frames    = max_frames;

    ret  = av_dict_set(&dc.decoder_opts, "threads",          nb_threads,    0);
    ret |= av_dict_set(&dc.decoder_opts, "thread_type",      thread_type,   0);

    ret = ds_run(&dc);
    if (ret < 0)
        goto fail;

    if (pd.nb_checksums_decoded != pd.nb_checksums_recon) {
        fprintf(stderr, "Mismatching frame counts: recon=%zu decoded=%zu\n",
                pd.nb_checksums_recon, pd.nb_checksums_decoded);
        ret = 1;
        goto fail;
    }

    // reconstructed frames are in coded order, sort them by pts into presentation order
    qsort(pd.checksums_recon, pd.nb_checksums_recon, sizeof(*pd.checksums_recon),
          frame_checksum_compare);

    for (size_t i = 0; i < pd.nb_checksums_decoded; i++) {
        const FrameChecksum *d = &pd.checksums_decoded[i];
        const FrameChecksum *r = &pd.checksums_recon[i];

        for (int p = 0; p < FF_ARRAY_ELEMS(d->checksum); p++)
            if (d->checksum[p] != r->checksum[p]) {
                fprintf(stderr, "Checksum mismatch in frame ts=%"PRId64", plane %d\n",
                        d->ts, p);
                ret = 1;
                goto fail;
            }
    }
    fprintf(stderr, "All %zu encoded frames match\n", pd.nb_checksums_decoded);

fail:
    avcodec_free_context(&pd.enc);
    avcodec_free_context(&pd.dec);
    av_freep(&pd.checksums_decoded);
    av_freep(&pd.checksums_recon);
    av_frame_free(&pd.frame);
    av_frame_free(&pd.frame_recon);
    av_packet_free(&pd.pkt);
    ds_free(&dc);
    return !!ret;
}
