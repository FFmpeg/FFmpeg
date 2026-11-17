/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdio.h>
#include <string.h>

#include "libavutil/bprint.h"
#include "libavutil/macros.h"
#include "libavutil/mem.h"
#include "libavcodec/codec_par.h"
#include "libavcodec/defs.h"
#include "libavformat/avformat.h"

/* Extradata of a 160x120 encoding: libx264 High profile, libx265 Main
 * profile. */
static const uint8_t h264_annexb[] = {
    0x00, 0x00, 0x00, 0x01, 0x67, 0x64, 0x00, 0x0b, 0xac, 0xd9, 0x42, 0x84,
    0x7e, 0x5c, 0x04, 0x40, 0x00, 0x00, 0x03, 0x00, 0x40, 0x00, 0x00, 0x0c,
    0x83, 0xc5, 0x0a, 0x65, 0x80,
};

static const uint8_t h264_avcc[] = {
    0x01, 0x64, 0x00, 0x0b, 0xff, 0xe1,
};

static const uint8_t hevc_annexb[] = {
    0x00, 0x00, 0x00, 0x01, 0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03,
    0x00, 0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x1e, 0xa0, 0x14,
    0x20, 0x79, 0x65, 0x95, 0x9a, 0x49, 0x32, 0xbc, 0x05, 0xa0, 0x20, 0x00,
    0x00, 0x03, 0x00, 0x20, 0x00, 0x00, 0x03, 0x03, 0x21, 0x00,
};

static const uint8_t hevc_annexb_3byte_start_code[] = {
    0x00, 0x00, 0x01, 0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00,
    0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x1e, 0xa0, 0x14, 0x20,
    0x79, 0x65, 0x95, 0x9a, 0x49, 0x32, 0xbc, 0x05, 0xa0, 0x20, 0x00, 0x00,
    0x03, 0x00, 0x20, 0x00, 0x00, 0x03, 0x03, 0x21, 0x00,
};

static const uint8_t hevc_hvcc[] = {
    0x01, 0x01, 0x60, 0x00, 0x00, 0x00, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x1e, 0xf0, 0x00, 0xfc, 0xfd, 0xf8, 0xf8, 0x00, 0x00, 0x0f, 0x00,
};

static const uint8_t hevc_hvcc_version0[] = {
    0x00, 0x01, 0x60, 0x00, 0x00, 0x00, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x1e, 0xf0, 0x00, 0xfc, 0xfd, 0xf8, 0xf8, 0x00, 0x00, 0x0f, 0x00,
};

static const uint8_t hevc_hvcc_zeroed[] = {
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xf0, 0x00, 0xfc, 0xfd, 0xf8, 0xf8, 0x00, 0x00, 0x0f, 0x00,
};

static const uint8_t hevc_hvcc_profile_space[] = {
    0x01, 0x41, 0x60, 0x00, 0x00, 0x00, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x1e, 0xf0, 0x00, 0xfc, 0xfd, 0xf8, 0xf8, 0x00, 0x00, 0x0f, 0x00,
};

static const uint8_t hevc_hvcc_constraints[] = {
    0x01, 0x01, 0x60, 0x00, 0x00, 0x00, 0x90, 0x00, 0x10, 0x00, 0x00, 0x00,
    0x1e, 0xf0, 0x00, 0xfc, 0xfd, 0xf8, 0xf8, 0x00, 0x00, 0x0f, 0x00,
};

static const uint8_t hevc_hvcc_constraints_full[] = {
    0x01, 0x01, 0x60, 0x00, 0x00, 0x00, 0x90, 0x10, 0x20, 0x30, 0x40, 0x50,
    0x1e, 0xf0, 0x00, 0xfc, 0xfd, 0xf8, 0xf8, 0x00, 0x00, 0x0f, 0x00,
};

static const uint8_t hevc_annexb_leading_zero[] = {
    0x00, 0x00, 0x00, 0x00, 0x01, 0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00,
    0x03, 0x00, 0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x1e, 0xa0,
    0x14, 0x20, 0x79, 0x65, 0x95, 0x9a, 0x49, 0x32, 0xbc, 0x05, 0xa0, 0x20,
    0x00, 0x00, 0x03, 0x00, 0x20, 0x00, 0x00, 0x03, 0x03, 0x21, 0x00,
};

/* hevc_annexb with the maximum of 32 long-term reference pictures */
static const uint8_t hevc_annexb_32_long_term_refs[] = {
    0x00, 0x00, 0x00, 0x01, 0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03,
    0x00, 0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x1e, 0xa0, 0x14,
    0x20, 0x79, 0x65, 0x95, 0x9a, 0x49, 0x32, 0xc1, 0x08, 0x00, 0x04, 0x04,
    0x03, 0x02, 0x01, 0x40, 0xc0, 0x70, 0x40, 0x24, 0x14, 0x0b, 0x06, 0x03,
    0x41, 0xc0, 0xf0, 0x80, 0x44, 0x24, 0x13, 0x0a, 0x05, 0x42, 0xc1, 0x70,
    0xc0, 0x64, 0x34, 0x1b, 0x0e, 0x07, 0x43, 0xc1, 0xf7, 0x80, 0xb4, 0x04,
    0x00, 0x00, 0x03, 0x00, 0x04, 0x00, 0x00, 0x03, 0x00, 0x64, 0x20,
};

typedef struct TestCase {
    const char *name;
    enum AVCodecID codec_id;
    uint32_t codec_tag;
    int profile;
    const uint8_t *extradata;
    int extradata_size;
} TestCase;

static const TestCase cases[] = {
    { "h264-annexb",          AV_CODEC_ID_H264, 0,
      AV_PROFILE_UNKNOWN, h264_annexb, sizeof(h264_annexb) },
    { "h264-annexb-avc3",     AV_CODEC_ID_H264, MKTAG('a', 'v', 'c', '3'),
      AV_PROFILE_UNKNOWN, h264_annexb, sizeof(h264_annexb) },
    { "h264-avcc",            AV_CODEC_ID_H264, MKTAG('a', 'v', 'c', '1'),
      AV_PROFILE_UNKNOWN, h264_avcc,   sizeof(h264_avcc)   },
    { "hevc-annexb-hvc1",     AV_CODEC_ID_HEVC, MKTAG('h', 'v', 'c', '1'),
      AV_PROFILE_UNKNOWN, hevc_annexb, sizeof(hevc_annexb) },
    { "hevc-annexb-hev1",     AV_CODEC_ID_HEVC, MKTAG('h', 'e', 'v', '1'),
      AV_PROFILE_UNKNOWN, hevc_annexb, sizeof(hevc_annexb) },
    { "hevc-annexb-3byte-start-code", AV_CODEC_ID_HEVC,
      MKTAG('h', 'v', 'c', '1'), AV_PROFILE_UNKNOWN,
      hevc_annexb_3byte_start_code, sizeof(hevc_annexb_3byte_start_code) },
    { "hevc-annexb-untagged", AV_CODEC_ID_HEVC, 0,
      AV_PROFILE_UNKNOWN, hevc_annexb, sizeof(hevc_annexb) },
    { "hevc-hvcc-hvc1",       AV_CODEC_ID_HEVC, MKTAG('h', 'v', 'c', '1'),
      AV_PROFILE_UNKNOWN, hevc_hvcc,   sizeof(hevc_hvcc)   },
    { "hevc-hvcc-hev1",       AV_CODEC_ID_HEVC, MKTAG('h', 'e', 'v', '1'),
      AV_PROFILE_UNKNOWN, hevc_hvcc,   sizeof(hevc_hvcc)   },
    { "hevc-hvcc-version0",   AV_CODEC_ID_HEVC, MKTAG('h', 'v', 'c', '1'),
      AV_PROFILE_UNKNOWN, hevc_hvcc_version0, sizeof(hevc_hvcc_version0) },
    { "hevc-hvcc-zeroed",     AV_CODEC_ID_HEVC, MKTAG('h', 'v', 'c', '1'),
      AV_PROFILE_HEVC_MAIN, hevc_hvcc_zeroed, sizeof(hevc_hvcc_zeroed) },
    { "hevc-hvcc-profile-space", AV_CODEC_ID_HEVC, MKTAG('h', 'v', 'c', '1'),
      AV_PROFILE_UNKNOWN, hevc_hvcc_profile_space,
      sizeof(hevc_hvcc_profile_space) },
    { "hevc-hvcc-constraints", AV_CODEC_ID_HEVC, MKTAG('h', 'v', 'c', '1'),
      AV_PROFILE_UNKNOWN, hevc_hvcc_constraints,
      sizeof(hevc_hvcc_constraints) },
    { "hevc-hvcc-constraints-full", AV_CODEC_ID_HEVC,
      MKTAG('h', 'v', 'c', '1'), AV_PROFILE_UNKNOWN,
      hevc_hvcc_constraints_full, sizeof(hevc_hvcc_constraints_full) },
    { "hevc-annexb-32-long-term-refs", AV_CODEC_ID_HEVC,
      MKTAG('h', 'v', 'c', '1'), AV_PROFILE_UNKNOWN,
      hevc_annexb_32_long_term_refs, sizeof(hevc_annexb_32_long_term_refs) },
    { "hevc-annexb-leading-zero", AV_CODEC_ID_HEVC, MKTAG('h', 'v', 'c', '1'),
      AV_PROFILE_UNKNOWN, hevc_annexb_leading_zero,
      sizeof(hevc_annexb_leading_zero) },
    { "aac-lc",              AV_CODEC_ID_AAC,  0,
      AV_PROFILE_AAC_LOW, NULL,        0                   },
};

static int run_case(const TestCase *test)
{
    AVCodecParameters *par = avcodec_parameters_alloc();
    AVBPrint out;
    int ret;

    if (!par)
        return AVERROR(ENOMEM);
    par->codec_id  = test->codec_id;
    par->codec_tag = test->codec_tag;
    par->profile   = test->profile;
    if (test->extradata_size) {
        par->extradata = av_mallocz(test->extradata_size +
                                    AV_INPUT_BUFFER_PADDING_SIZE);
        if (!par->extradata) {
            avcodec_parameters_free(&par);
            return AVERROR(ENOMEM);
        }
        memcpy(par->extradata, test->extradata, test->extradata_size);
        par->extradata_size = test->extradata_size;
    }

    av_bprint_init(&out, 0, AV_BPRINT_SIZE_UNLIMITED);
    ret = av_mime_codec_str(par, (AVRational){ 1, 0 }, &out);
    printf("%s: %s\n", test->name, ret < 0 ? "error" : out.str);
    av_bprint_finalize(&out, NULL);
    avcodec_parameters_free(&par);

    return ret == AVERROR(ENOMEM) ? ret : 0;
}

int main(void)
{
    for (size_t i = 0; i < FF_ARRAY_ELEMS(cases); i++)
        if (run_case(&cases[i]) < 0)
            return 1;
    return 0;
}
