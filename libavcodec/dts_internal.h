/*
 * dts_internal.h
 * Copyright (C) 2004 Gildas Bazin <gbazin@videolan.org>
 * Copyright (C) 2000-2003 Michel Lespinasse <walken@zoy.org>
 * Copyright (C) 1999-2000 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * This file is part of dtsdec, a free DTS Coherent Acoustics stream decoder.
 * See http://www.videolan.org/dtsdec.html for updates.
 *
 * dtsdec is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * dtsdec is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define DTS_SUBFRAMES_MAX (16)
#define DTS_PRIM_CHANNELS_MAX (5)
#define DTS_SUBBANDS (32)
#define DTS_ABITS_MAX (32) /* Should be 28 */
#define DTS_SUBSUBFAMES_MAX (4)
#define DTS_LFE_MAX (3)

struct dts_state_s {

    /* Frame header */
    int frame_type;             /* type of the current frame */
    int samples_deficit;        /* deficit sample count */
    int crc_present;            /* crc is present in the bitstream */
    int sample_blocks;          /* number of PCM sample blocks */
    int frame_size;             /* primary frame byte size */
    int amode;                  /* audio channels arrangement */
    int sample_rate;            /* audio sampling rate */
    int bit_rate;               /* transmission bit rate */

    int downmix;                /* embedded downmix enabled */
    int dynrange;               /* embedded dynamic range flag */
    int timestamp;              /* embedded time stamp flag */
    int aux_data;               /* auxiliary data flag */
    int hdcd;                   /* source material is mastered in HDCD */
    int ext_descr;              /* extension audio descriptor flag */
    int ext_coding;             /* extended coding flag */
    int aspf;                   /* audio sync word insertion flag */
    int lfe;                    /* low frequency effects flag */
    int predictor_history;      /* predictor history flag */
    int header_crc;             /* header crc check bytes */
    int multirate_inter;        /* multirate interpolator switch */
    int version;                /* encoder software revision */
    int copy_history;           /* copy history */
    int source_pcm_res;         /* source pcm resolution */
    int front_sum;              /* front sum/difference flag */
    int surround_sum;           /* surround sum/difference flag */
    int dialog_norm;            /* dialog normalisation parameter */

    /* Primary audio coding header */
    int subframes;              /* number of subframes */
    int prim_channels;          /* number of primary audio channels */
    /* subband activity count */
    int subband_activity[DTS_PRIM_CHANNELS_MAX];
    /* high frequency vq start subband */
    int vq_start_subband[DTS_PRIM_CHANNELS_MAX];
    /* joint intensity coding index */
    int joint_intensity[DTS_PRIM_CHANNELS_MAX];
    /* transient mode code book */
    int transient_huffman[DTS_PRIM_CHANNELS_MAX];
    /* scale factor code book */
    int scalefactor_huffman[DTS_PRIM_CHANNELS_MAX];
    /* bit allocation quantizer select */
    int bitalloc_huffman[DTS_PRIM_CHANNELS_MAX];
    /* quantization index codebook select */
    int quant_index_huffman[DTS_PRIM_CHANNELS_MAX][DTS_ABITS_MAX];
    /* scale factor adjustment */
    float scalefactor_adj[DTS_PRIM_CHANNELS_MAX][DTS_ABITS_MAX];

    /* Primary audio coding side information */
    int subsubframes;           /* number of subsubframes */
    int partial_samples;        /* partial subsubframe samples count */
    /* prediction mode (ADPCM used or not) */
    int prediction_mode[DTS_PRIM_CHANNELS_MAX][DTS_SUBBANDS];
    /* prediction VQ coefs */
    int prediction_vq[DTS_PRIM_CHANNELS_MAX][DTS_SUBBANDS];
    /* bit allocation index */
    int bitalloc[DTS_PRIM_CHANNELS_MAX][DTS_SUBBANDS];
    /* transition mode (transients) */
    int transition_mode[DTS_PRIM_CHANNELS_MAX][DTS_SUBBANDS];
    /* scale factors (2 if transient)*/
    int scale_factor[DTS_PRIM_CHANNELS_MAX][DTS_SUBBANDS][2];
    /* joint subband scale factors codebook */
    int joint_huff[DTS_PRIM_CHANNELS_MAX];
    /* joint subband scale factors */
    int joint_scale_factor[DTS_PRIM_CHANNELS_MAX][DTS_SUBBANDS];
    /* stereo downmix coefficients */
    int downmix_coef[DTS_PRIM_CHANNELS_MAX][2];
    /* dynamic range coefficient */
    int dynrange_coef;

    /* VQ encoded high frequency subbands */
    int high_freq_vq[DTS_PRIM_CHANNELS_MAX][DTS_SUBBANDS];

    /* Low frequency effect data */
    double lfe_data[2*DTS_SUBSUBFAMES_MAX*DTS_LFE_MAX * 2 /*history*/];
    int lfe_scale_factor;

    /* Subband samples history (for ADPCM) */
    double subband_samples_hist[DTS_PRIM_CHANNELS_MAX][DTS_SUBBANDS][4];
    double subband_fir_hist[DTS_PRIM_CHANNELS_MAX][512];
    double subband_fir_noidea[DTS_PRIM_CHANNELS_MAX][64];

    /* Audio output */
    level_t clev;            /* centre channel mix level */
    level_t slev;            /* surround channels mix level */

    int output;              /* type of output */
    level_t level;           /* output level */
    sample_t bias;           /* output bias */

    sample_t * samples;      /* pointer to the internal audio samples buffer */
    int downmixed;

    int dynrnge;             /* apply dynamic range */
    level_t dynrng;          /* dynamic range */
    void * dynrngdata;       /* dynamic range callback funtion and data */
    level_t (* dynrngcall) (level_t range, void * dynrngdata);

    /* Bitstream handling */
    uint32_t * buffer_start;
    uint32_t bits_left;
    uint32_t current_word;
    int      word_mode;         /* 16/14 bits word format (1 -> 16, 0 -> 14) */
    int      bigendian_mode;    /* endianness (1 -> be, 0 -> le) */

    /* Current position in DTS frame */
    int current_subframe;
    int current_subsubframe;

    /* Pre-calculated cosine modulation coefs for the QMF */
    double cos_mod[544];

    /* Debug flag */
    int debug_flag;
};

#define LEVEL_PLUS6DB 2.0
#define LEVEL_PLUS3DB 1.4142135623730951
#define LEVEL_3DB 0.7071067811865476
#define LEVEL_45DB 0.5946035575013605
#define LEVEL_6DB 0.5

int dts_downmix_init (int input, int flags, level_t * level,
                      level_t clev, level_t slev);
int dts_downmix_coeff (level_t * coeff, int acmod, int output, level_t level,
                       level_t clev, level_t slev);
void dts_downmix (sample_t * samples, int acmod, int output, sample_t bias,
                  level_t clev, level_t slev);
void dts_upmix (sample_t * samples, int acmod, int output);

#define ROUND(x) ((int)((x) + ((x) > 0 ? 0.5 : -0.5)))

#ifndef LIBDTS_FIXED

typedef sample_t quantizer_t;
#define SAMPLE(x) (x)
#define LEVEL(x) (x)
#define MUL(a,b) ((a) * (b))
#define MUL_L(a,b) ((a) * (b))
#define MUL_C(a,b) ((a) * (b))
#define DIV(a,b) ((a) / (b))
#define BIAS(x) ((x) + bias)

#else /* LIBDTS_FIXED */

typedef int16_t quantizer_t;
#define SAMPLE(x) (sample_t)((x) * (1 << 30))
#define LEVEL(x) (level_t)((x) * (1 << 26))

#if 0
#define MUL(a,b) ((int)(((int64_t)(a) * (b) + (1 << 29)) >> 30))
#define MUL_L(a,b) ((int)(((int64_t)(a) * (b) + (1 << 25)) >> 26))
#elif 1
#define MUL(a,b) \
({ int32_t _ta=(a), _tb=(b), _tc; \
   _tc=(_ta & 0xffff)*(_tb >> 16)+(_ta >> 16)*(_tb & 0xffff); (int32_t)(((_tc >> 14))+ (((_ta >> 16)*(_tb >> 16)) << 2 )); })
#define MUL_L(a,b) \
({ int32_t _ta=(a), _tb=(b), _tc; \
   _tc=(_ta & 0xffff)*(_tb >> 16)+(_ta >> 16)*(_tb & 0xffff); (int32_t)((_tc >> 10) + (((_ta >> 16)*(_tb >> 16)) << 6)); })
#else
#define MUL(a,b) (((a) >> 15) * ((b) >> 15))
#define MUL_L(a,b) (((a) >> 13) * ((b) >> 13))
#endif

#define MUL_C(a,b) MUL_L (a, LEVEL (b))
#define DIV(a,b) ((((int64_t)LEVEL (a)) << 26) / (b))
#define BIAS(x) (x)

#endif
