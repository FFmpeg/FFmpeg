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

#include "libavutil/crc.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "rangecoder.h"
#include "ffv1.h"

static int read_quant_table(RangeCoder *c, int16_t *quant_table, int scale)
{
    int v;
    int i = 0;
    uint8_t state[CONTEXT_SIZE];

    memset(state, 128, sizeof(state));

    for (v = 0; i < 128; v++) {
        unsigned len = ff_ffv1_get_symbol(c, state, 0) + 1U;

        if (len > 128 - i || !len)
            return AVERROR_INVALIDDATA;

        while (len--) {
            quant_table[i] = scale * v;
            i++;
        }
    }

    for (i = 1; i < 128; i++)
        quant_table[256 - i] = -quant_table[i];
    quant_table[128] = -quant_table[127];

    return 2 * v - 1;
}

int ff_ffv1_read_quant_tables(RangeCoder *c,
                              int16_t quant_table[MAX_CONTEXT_INPUTS][256])
{
    int i;
    int context_count = 1;

    for (i = 0; i < 5; i++) {
        int ret = read_quant_table(c, quant_table[i], context_count);
        if (ret < 0)
            return ret;
        context_count *= ret;
        if (context_count > 32768U) {
            return AVERROR_INVALIDDATA;
        }
    }
    return (context_count + 1) / 2;
}

int ff_ffv1_read_extra_header(FFV1Context *f)
{
    RangeCoder c;
    uint8_t state[CONTEXT_SIZE];
    int ret;
    uint8_t state2[32][CONTEXT_SIZE];
    unsigned crc = 0;

    memset(state2, 128, sizeof(state2));
    memset(state, 128, sizeof(state));

    ff_init_range_decoder(&c, f->avctx->extradata, f->avctx->extradata_size);
    ff_build_rac_states(&c, 0.05 * (1LL << 32), 256 - 8);

    f->version = ff_ffv1_get_symbol(&c, state, 0);
    if (f->version < 2) {
        av_log(f->avctx, AV_LOG_ERROR, "Invalid version in global header\n");
        return AVERROR_INVALIDDATA;
    }
    if (f->version > 4) {
        av_log(f->avctx, AV_LOG_ERROR, "unsupported version %d\n",
            f->version);
        return AVERROR_PATCHWELCOME;
    }
    f->combined_version = f->version << 16;
    if (f->version > 2) {
        c.bytestream_end -= 4;
        f->micro_version = ff_ffv1_get_symbol(&c, state, 0);
        if (f->micro_version < 0 || f->micro_version > 65535)
            return AVERROR_INVALIDDATA;
        f->combined_version += f->micro_version;
    }
    f->ac = ff_ffv1_get_symbol(&c, state, 0);

    if (f->ac == AC_RANGE_CUSTOM_TAB) {
        for (int i = 1; i < 256; i++)
            f->state_transition[i] = ff_ffv1_get_symbol(&c, state, 1) + c.one_state[i];
    } else {
        RangeCoder rc;
        ff_build_rac_states(&rc, 0.05 * (1LL << 32), 256 - 8);
        for (int i = 1; i < 256; i++)
            f->state_transition[i] = rc.one_state[i];
    }

    f->colorspace                 = ff_ffv1_get_symbol(&c, state, 0); //YUV cs type
    f->avctx->bits_per_raw_sample = ff_ffv1_get_symbol(&c, state, 0);
    f->chroma_planes              = get_rac(&c, state);
    f->chroma_h_shift             = ff_ffv1_get_symbol(&c, state, 0);
    f->chroma_v_shift             = ff_ffv1_get_symbol(&c, state, 0);
    f->transparency               = get_rac(&c, state);
    f->plane_count                = 1 + (f->chroma_planes || f->version<4) + f->transparency;
    f->num_h_slices               = 1 + ff_ffv1_get_symbol(&c, state, 0);
    f->num_v_slices               = 1 + ff_ffv1_get_symbol(&c, state, 0);

    if (f->chroma_h_shift > 4U || f->chroma_v_shift > 4U) {
        av_log(f->avctx, AV_LOG_ERROR, "chroma shift parameters %d %d are invalid\n",
               f->chroma_h_shift, f->chroma_v_shift);
        return AVERROR_INVALIDDATA;
    }

    if (f->num_h_slices > (unsigned)f->width  || !f->num_h_slices ||
        f->num_v_slices > (unsigned)f->height || !f->num_v_slices
       ) {
        av_log(f->avctx, AV_LOG_ERROR, "slice count invalid\n");
        return AVERROR_INVALIDDATA;
    }

    if (f->num_h_slices > MAX_SLICES / f->num_v_slices) {
        av_log(f->avctx, AV_LOG_ERROR, "slice count unsupported\n");
        return AVERROR_PATCHWELCOME;
    }

    f->quant_table_count = ff_ffv1_get_symbol(&c, state, 0);
    if (f->quant_table_count > (unsigned)MAX_QUANT_TABLES || !f->quant_table_count) {
        av_log(f->avctx, AV_LOG_ERROR, "quant table count %d is invalid\n", f->quant_table_count);
        f->quant_table_count = 0;
        return AVERROR_INVALIDDATA;
    }

    for (int i = 0; i < f->quant_table_count; i++) {
        f->context_count[i] = ff_ffv1_read_quant_tables(&c, f->quant_tables[i]);
        if (f->context_count[i] < 0) {
            av_log(f->avctx, AV_LOG_ERROR, "read_quant_table error\n");
            return AVERROR_INVALIDDATA;
        }
    }
    if ((ret = ff_ffv1_allocate_initial_states(f)) < 0)
        return ret;

    for (int i = 0; i < f->quant_table_count; i++)
        if (get_rac(&c, state)) {
            for (int j = 0; j < f->context_count[i]; j++)
                for (int k = 0; k < CONTEXT_SIZE; k++) {
                    int pred = j ? f->initial_states[i][j - 1][k] : 128;
                    f->initial_states[i][j][k] =
                        (pred + ff_ffv1_get_symbol(&c, state2[k], 1)) & 0xFF;
                }
        }

    if (f->version > 2) {
        f->ec = ff_ffv1_get_symbol(&c, state, 0);
        if (f->ec >= 2)
            f->crcref = 0x7a8c4079;
        if (f->combined_version >= 0x30003)
            f->intra = ff_ffv1_get_symbol(&c, state, 0);
        if (f->combined_version >= 0x40004)
            f->flt = ff_ffv1_get_symbol(&c, state, 0);
    }

    if (f->version > 2) {
        unsigned v;
        v = av_crc(av_crc_get_table(AV_CRC_32_IEEE), f->crcref,
                   f->avctx->extradata, f->avctx->extradata_size);
        if (v != f->crcref || f->avctx->extradata_size < 4) {
            av_log(f->avctx, AV_LOG_ERROR, "CRC mismatch %X!\n", v);
            return AVERROR_INVALIDDATA;
        }
        crc = AV_RB32(f->avctx->extradata + f->avctx->extradata_size - 4);
    }

    if (f->avctx->debug & FF_DEBUG_PICT_INFO)
        av_log(f->avctx, AV_LOG_DEBUG,
               "global: ver:%d.%d, coder:%d, colorspace: %d bpr:%d chroma:%d(%d:%d), alpha:%d slices:%dx%d qtabs:%d ec:%d intra:%d CRC:0x%08X\n",
               f->version, f->micro_version,
               f->ac,
               f->colorspace,
               f->avctx->bits_per_raw_sample,
               f->chroma_planes, f->chroma_h_shift, f->chroma_v_shift,
               f->transparency,
               f->num_h_slices, f->num_v_slices,
               f->quant_table_count,
               f->ec,
               f->intra,
               crc
              );
    return 0;
}

int ff_ffv1_parse_header(FFV1Context *f, RangeCoder *c, uint8_t *state)
{
    if (f->version < 2) {
        int chroma_planes, chroma_h_shift, chroma_v_shift, transparency, colorspace, bits_per_raw_sample;
        unsigned v= ff_ffv1_get_symbol(c, state, 0);
        if (v >= 2) {
            av_log(f->avctx, AV_LOG_ERROR, "invalid version %d in ver01 header\n", v);
            return AVERROR_INVALIDDATA;
        }
        f->version = v;
        f->ac = ff_ffv1_get_symbol(c, state, 0);

        if (f->ac == AC_RANGE_CUSTOM_TAB) {
            for (int i = 1; i < 256; i++) {
                int st = ff_ffv1_get_symbol(c, state, 1) + c->one_state[i];
                if (st < 1 || st > 255) {
                    av_log(f->avctx, AV_LOG_ERROR, "invalid state transition %d\n", st);
                    return AVERROR_INVALIDDATA;
                }
                f->state_transition[i] = st;
            }
        } else {
            RangeCoder rc;
            ff_build_rac_states(&rc, 0.05 * (1LL << 32), 256 - 8);
            for (int i = 1; i < 256; i++)
                f->state_transition[i] = rc.one_state[i];
        }

        colorspace          = ff_ffv1_get_symbol(c, state, 0); //YUV cs type
        bits_per_raw_sample = f->version > 0 ? ff_ffv1_get_symbol(c, state, 0) : f->avctx->bits_per_raw_sample;
        chroma_planes       = get_rac(c, state);
        chroma_h_shift      = ff_ffv1_get_symbol(c, state, 0);
        chroma_v_shift      = ff_ffv1_get_symbol(c, state, 0);
        transparency        = get_rac(c, state);
        if (colorspace == 0 && f->avctx->skip_alpha)
            transparency = 0;

        if (f->plane_count) {
            if (colorspace          != f->colorspace                 ||
                bits_per_raw_sample != f->avctx->bits_per_raw_sample ||
                chroma_planes       != f->chroma_planes              ||
                chroma_h_shift      != f->chroma_h_shift             ||
                chroma_v_shift      != f->chroma_v_shift             ||
                transparency        != f->transparency) {
                av_log(f->avctx, AV_LOG_ERROR, "Invalid change of global parameters\n");
                return AVERROR_INVALIDDATA;
            }
        }

        if (chroma_h_shift > 4U || chroma_v_shift > 4U) {
            av_log(f->avctx, AV_LOG_ERROR, "chroma shift parameters %d %d are invalid\n",
                   chroma_h_shift, chroma_v_shift);
            return AVERROR_INVALIDDATA;
        }

        f->colorspace                 = colorspace;
        f->avctx->bits_per_raw_sample = bits_per_raw_sample;
        f->chroma_planes              = chroma_planes;
        f->chroma_h_shift             = chroma_h_shift;
        f->chroma_v_shift             = chroma_v_shift;
        f->transparency               = transparency;

        f->plane_count    = 2 + f->transparency;
    }

    if (f->colorspace == 0) {
        if (!f->transparency && !f->chroma_planes) {
            if (f->avctx->bits_per_raw_sample <= 8)
                f->pix_fmt = AV_PIX_FMT_GRAY8;
            else if (f->avctx->bits_per_raw_sample == 9) {
                f->packed_at_lsb = 1;
                f->pix_fmt = AV_PIX_FMT_GRAY9;
            } else if (f->avctx->bits_per_raw_sample == 10) {
                f->packed_at_lsb = 1;
                f->pix_fmt = AV_PIX_FMT_GRAY10;
            } else if (f->avctx->bits_per_raw_sample == 12) {
                f->packed_at_lsb = 1;
                f->pix_fmt = AV_PIX_FMT_GRAY12;
            } else if (f->avctx->bits_per_raw_sample == 14) {
                f->packed_at_lsb = 1;
                f->pix_fmt = AV_PIX_FMT_GRAY14;
            } else if (f->avctx->bits_per_raw_sample == 16) {
                f->packed_at_lsb = 1;
                if (f->flt) {
                    f->pix_fmt = AV_PIX_FMT_GRAYF16;
                } else
                    f->pix_fmt = AV_PIX_FMT_GRAY16;
            } else if (f->avctx->bits_per_raw_sample < 16) {
                f->pix_fmt = AV_PIX_FMT_GRAY16;
            } else
                return AVERROR(ENOSYS);
        } else if (f->transparency && !f->chroma_planes) {
            if (f->avctx->bits_per_raw_sample <= 8 && !f->flt) {
                f->pix_fmt = AV_PIX_FMT_YA8;
            } else if (f->avctx->bits_per_raw_sample == 16 && f->flt) {
                f->pix_fmt = AV_PIX_FMT_YAF16;
            } else
                return AVERROR(ENOSYS);
        } else if (f->avctx->bits_per_raw_sample<=8 && !f->transparency) {
            switch(16 * f->chroma_h_shift + f->chroma_v_shift) {
            case 0x00: f->pix_fmt = AV_PIX_FMT_YUV444P; break;
            case 0x01: f->pix_fmt = AV_PIX_FMT_YUV440P; break;
            case 0x10: f->pix_fmt = AV_PIX_FMT_YUV422P; break;
            case 0x11: f->pix_fmt = AV_PIX_FMT_YUV420P; break;
            case 0x20: f->pix_fmt = AV_PIX_FMT_YUV411P; break;
            case 0x22: f->pix_fmt = AV_PIX_FMT_YUV410P; break;
            }
        } else if (f->avctx->bits_per_raw_sample <= 8 && f->transparency) {
            switch(16*f->chroma_h_shift + f->chroma_v_shift) {
            case 0x00: f->pix_fmt = AV_PIX_FMT_YUVA444P; break;
            case 0x10: f->pix_fmt = AV_PIX_FMT_YUVA422P; break;
            case 0x11: f->pix_fmt = AV_PIX_FMT_YUVA420P; break;
            }
        } else if (f->avctx->bits_per_raw_sample == 9 && !f->transparency) {
            f->packed_at_lsb = 1;
            switch(16 * f->chroma_h_shift + f->chroma_v_shift) {
            case 0x00: f->pix_fmt = AV_PIX_FMT_YUV444P9; break;
            case 0x10: f->pix_fmt = AV_PIX_FMT_YUV422P9; break;
            case 0x11: f->pix_fmt = AV_PIX_FMT_YUV420P9; break;
            }
        } else if (f->avctx->bits_per_raw_sample == 9 && f->transparency) {
            f->packed_at_lsb = 1;
            switch(16 * f->chroma_h_shift + f->chroma_v_shift) {
            case 0x00: f->pix_fmt = AV_PIX_FMT_YUVA444P9; break;
            case 0x10: f->pix_fmt = AV_PIX_FMT_YUVA422P9; break;
            case 0x11: f->pix_fmt = AV_PIX_FMT_YUVA420P9; break;
            }
        } else if (f->avctx->bits_per_raw_sample == 10 && !f->transparency) {
            f->packed_at_lsb = 1;
            switch(16 * f->chroma_h_shift + f->chroma_v_shift) {
            case 0x00: f->pix_fmt = AV_PIX_FMT_YUV444P10; break;
            case 0x01: f->pix_fmt = AV_PIX_FMT_YUV440P10; break;
            case 0x10: f->pix_fmt = AV_PIX_FMT_YUV422P10; break;
            case 0x11: f->pix_fmt = AV_PIX_FMT_YUV420P10; break;
            }
        } else if (f->avctx->bits_per_raw_sample == 10 && f->transparency) {
            f->packed_at_lsb = 1;
            switch(16 * f->chroma_h_shift + f->chroma_v_shift) {
            case 0x00: f->pix_fmt = AV_PIX_FMT_YUVA444P10; break;
            case 0x10: f->pix_fmt = AV_PIX_FMT_YUVA422P10; break;
            case 0x11: f->pix_fmt = AV_PIX_FMT_YUVA420P10; break;
            }
        } else if (f->avctx->bits_per_raw_sample == 12 && !f->transparency) {
            f->packed_at_lsb = 1;
            switch(16 * f->chroma_h_shift + f->chroma_v_shift) {
            case 0x00: f->pix_fmt = AV_PIX_FMT_YUV444P12; break;
            case 0x01: f->pix_fmt = AV_PIX_FMT_YUV440P12; break;
            case 0x10: f->pix_fmt = AV_PIX_FMT_YUV422P12; break;
            case 0x11: f->pix_fmt = AV_PIX_FMT_YUV420P12; break;
            }
        } else if (f->avctx->bits_per_raw_sample == 12 && f->transparency) {
            f->packed_at_lsb = 1;
            switch(16 * f->chroma_h_shift + f->chroma_v_shift) {
            case 0x00: f->pix_fmt = AV_PIX_FMT_YUVA444P12; break;
            case 0x10: f->pix_fmt = AV_PIX_FMT_YUVA422P12; break;
            }
        } else if (f->avctx->bits_per_raw_sample == 14 && !f->transparency) {
            f->packed_at_lsb = 1;
            switch(16 * f->chroma_h_shift + f->chroma_v_shift) {
            case 0x00: f->pix_fmt = AV_PIX_FMT_YUV444P14; break;
            case 0x10: f->pix_fmt = AV_PIX_FMT_YUV422P14; break;
            case 0x11: f->pix_fmt = AV_PIX_FMT_YUV420P14; break;
            }
        } else if (f->avctx->bits_per_raw_sample == 16 && !f->transparency){
            f->packed_at_lsb = 1;
            switch(16 * f->chroma_h_shift + f->chroma_v_shift) {
            case 0x00: f->pix_fmt = AV_PIX_FMT_YUV444P16; break;
            case 0x10: f->pix_fmt = AV_PIX_FMT_YUV422P16; break;
            case 0x11: f->pix_fmt = AV_PIX_FMT_YUV420P16; break;
            }
        } else if (f->avctx->bits_per_raw_sample == 16 && f->transparency){
            f->packed_at_lsb = 1;
            switch(16 * f->chroma_h_shift + f->chroma_v_shift) {
            case 0x00: f->pix_fmt = AV_PIX_FMT_YUVA444P16; break;
            case 0x10: f->pix_fmt = AV_PIX_FMT_YUVA422P16; break;
            case 0x11: f->pix_fmt = AV_PIX_FMT_YUVA420P16; break;
            }
        }
    } else if (f->colorspace == 1) {
        if (f->chroma_h_shift || f->chroma_v_shift) {
            av_log(f->avctx, AV_LOG_ERROR,
                   "chroma subsampling not supported in this colorspace\n");
            return AVERROR(ENOSYS);
        }
        if (     f->avctx->bits_per_raw_sample <=  8 && !f->transparency)
            f->pix_fmt = AV_PIX_FMT_0RGB32;
        else if (f->avctx->bits_per_raw_sample <=  8 && f->transparency)
            f->pix_fmt = AV_PIX_FMT_RGB32;
        else if (f->avctx->bits_per_raw_sample ==  9 && !f->transparency)
            f->pix_fmt = AV_PIX_FMT_GBRP9;
        else if (f->avctx->bits_per_raw_sample == 10 && !f->transparency)
            f->pix_fmt = AV_PIX_FMT_GBRP10;
        else if (f->avctx->bits_per_raw_sample == 10 && f->transparency)
            f->pix_fmt = AV_PIX_FMT_GBRAP10;
        else if (f->avctx->bits_per_raw_sample == 12 && !f->transparency)
            f->pix_fmt = AV_PIX_FMT_GBRP12;
        else if (f->avctx->bits_per_raw_sample == 12 && f->transparency)
            f->pix_fmt = AV_PIX_FMT_GBRAP12;
        else if (f->avctx->bits_per_raw_sample == 14 && !f->transparency)
            f->pix_fmt = AV_PIX_FMT_GBRP14;
        else if (f->avctx->bits_per_raw_sample == 14 && f->transparency)
            f->pix_fmt = AV_PIX_FMT_GBRAP14;
        else if (f->avctx->bits_per_raw_sample == 16 && !f->transparency) {
            if (f->flt) {
                f->pix_fmt = AV_PIX_FMT_GBRPF16;
            } else
                f->pix_fmt = AV_PIX_FMT_GBRP16;
            f->use32bit = 1;
        } else if (f->avctx->bits_per_raw_sample == 16 && f->transparency) {
            if (f->flt) {
                f->pix_fmt = AV_PIX_FMT_GBRAPF16;
            } else
                f->pix_fmt = AV_PIX_FMT_GBRAP16;
            f->use32bit = 1;
        } else if (f->avctx->bits_per_raw_sample == 32 && !f->transparency) {
            if (f->flt) {
                f->pix_fmt = AV_PIX_FMT_GBRPF32;
            }
            f->use32bit = 1;
        } else if (f->avctx->bits_per_raw_sample == 32 && f->transparency) {
            if (f->flt) {
                f->pix_fmt = AV_PIX_FMT_GBRAPF32;
            }
            f->use32bit = 1;
        }
    } else {
        av_log(f->avctx, AV_LOG_ERROR, "colorspace not supported\n");
        return AVERROR(ENOSYS);
    }
    if (f->pix_fmt == AV_PIX_FMT_NONE) {
        av_log(f->avctx, AV_LOG_ERROR, "format not supported\n");
        return AVERROR(ENOSYS);
    }

    return 0;
}
