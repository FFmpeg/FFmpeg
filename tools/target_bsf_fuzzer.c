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

#include "config.h"
#include "libavutil/imgutils.h"

#include "libavcodec/avcodec.h"
#include "libavcodec/bsf.h"
#include "libavcodec/bytestream.h"
#include "libavcodec/internal.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static void error(const char *err)
{
    fprintf(stderr, "%s", err);
    exit(1);
}

static AVBitStreamFilter *f = NULL;

static const uint64_t FUZZ_TAG = 0x4741542D5A5A5546ULL;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    const uint64_t fuzz_tag = FUZZ_TAG;
    const uint8_t *last = data;
    const uint8_t *end = data + size;
    AVBSFContext *bsf = NULL;
    AVPacket in, out;
    uint64_t keyframes = 0;
    int res;

    if (!f) {
#ifdef FFMPEG_BSF
#define BSF_SYMBOL0(BSF) ff_##BSF##_bsf
#define BSF_SYMBOL(BSF) BSF_SYMBOL0(BSF)
        extern AVBitStreamFilter BSF_SYMBOL(FFMPEG_BSF);
        f = &BSF_SYMBOL(FFMPEG_BSF);
#else
        extern AVBitStreamFilter ff_null_bsf;
        f = &ff_null_bsf;
#endif
        av_log_set_level(AV_LOG_PANIC);
    }

    res = av_bsf_alloc(f, &bsf);
    if (res < 0)
        error("Failed memory allocation");

    if (size > 1024) {
        GetByteContext gbc;
        int extradata_size;
        size -= 1024;
        bytestream2_init(&gbc, data + size, 1024);
        bsf->par_in->width                      = bytestream2_get_le32(&gbc);
        bsf->par_in->height                     = bytestream2_get_le32(&gbc);
        bsf->par_in->bit_rate                   = bytestream2_get_le64(&gbc);
        bsf->par_in->bits_per_coded_sample      = bytestream2_get_le32(&gbc);

        if (f->codec_ids) {
            int i, id;
            for (i = 0; f->codec_ids[i] != AV_CODEC_ID_NONE; i++);
            id = f->codec_ids[bytestream2_get_byte(&gbc) % i];
            bsf->par_in->codec_id = id;
            bsf->par_in->codec_tag              = bytestream2_get_le32(&gbc);
        }

        extradata_size = bytestream2_get_le32(&gbc);

        bsf->par_in->sample_rate                = bytestream2_get_le32(&gbc);
        bsf->par_in->channels                   = (unsigned)bytestream2_get_le32(&gbc) % FF_SANE_NB_CHANNELS;
        bsf->par_in->block_align                = bytestream2_get_le32(&gbc);
        keyframes                               = bytestream2_get_le64(&gbc);

        if (extradata_size < size) {
            bsf->par_in->extradata = av_mallocz(extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
            if (bsf->par_in->extradata) {
                bsf->par_in->extradata_size = extradata_size;
                size -= bsf->par_in->extradata_size;
                memcpy(bsf->par_in->extradata, data + size, bsf->par_in->extradata_size);
            }
        }
        if (av_image_check_size(bsf->par_in->width, bsf->par_in->height, 0, bsf))
            bsf->par_in->width = bsf->par_in->height = 0;
    }

    res = av_bsf_init(bsf);
    if (res < 0) {
        av_bsf_free(&bsf);
        return 0; // Failure of av_bsf_init() does not imply that a issue was found
    }

    av_init_packet(&in);
    av_init_packet(&out);
    out.data = NULL;
    out.size = 0;
    while (data < end) {
        // Search for the TAG
        while (data + sizeof(fuzz_tag) < end) {
            if (data[0] == (fuzz_tag & 0xFF) && AV_RN64(data) == fuzz_tag)
                break;
            data++;
        }
        if (data + sizeof(fuzz_tag) > end)
            data = end;

        res = av_new_packet(&in, data - last);
        if (res < 0)
            error("Failed memory allocation");
        memcpy(in.data, last, data - last);
        in.flags = (keyframes & 1) * AV_PKT_FLAG_DISCARD + (!!(keyframes & 2)) * AV_PKT_FLAG_KEY;
        keyframes = (keyframes >> 2) + (keyframes<<62);
        data += sizeof(fuzz_tag);
        last = data;

        while (in.size) {
            res = av_bsf_send_packet(bsf, &in);
            if (res < 0 && res != AVERROR(EAGAIN))
                break;
            res = av_bsf_receive_packet(bsf, &out);
            if (res < 0)
                break;
            av_packet_unref(&out);
        }
        av_packet_unref(&in);
    }

    res = av_bsf_send_packet(bsf, NULL);
    while (!res) {
        res = av_bsf_receive_packet(bsf, &out);
        if (res < 0)
            break;
        av_packet_unref(&out);
    }

    av_bsf_free(&bsf);
    return 0;
}
