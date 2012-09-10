/*
 * Atrac 3 compatible decoder
 * Copyright (c) 2006-2008 Maxim Poliakovski
 * Copyright (c) 2006-2008 Benjamin Larsson
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
 * Atrac 3 compatible decoder.
 * This decoder handles Sony's ATRAC3 data.
 *
 * Container formats used to store atrac 3 data:
 * RealMedia (.rm), RIFF WAV (.wav, .at3), Sony OpenMG (.oma, .aa3).
 *
 * To use this decoder, a calling application must supply the extradata
 * bytes provided in the containers above.
 */

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "libavutil/float_dsp.h"
#include "libavutil/libm.h"
#include "avcodec.h"
#include "get_bits.h"
#include "bytestream.h"
#include "fft.h"
#include "fmtconvert.h"

#include "atrac.h"
#include "atrac3data.h"

#define JOINT_STEREO    0x12
#define STEREO          0x2

#define SAMPLES_PER_FRAME 1024
#define MDCT_SIZE          512

/* These structures are needed to store the parsed gain control data. */
typedef struct {
    int   num_gain_data;
    int   levcode[8];
    int   loccode[8];
} gain_info;

typedef struct {
    gain_info   gBlock[4];
} gain_block;

typedef struct {
    int     pos;
    int     numCoefs;
    float   coef[8];
} tonal_component;

typedef struct {
    int               bandsCoded;
    int               numComponents;
    tonal_component   components[64];
    float             prevFrame[SAMPLES_PER_FRAME];
    int               gcBlkSwitch;
    gain_block        gainBlock[2];

    DECLARE_ALIGNED(32, float, spectrum)[SAMPLES_PER_FRAME];
    DECLARE_ALIGNED(32, float, IMDCT_buf)[SAMPLES_PER_FRAME];

    float             delayBuf1[46]; ///<qmf delay buffers
    float             delayBuf2[46];
    float             delayBuf3[46];
} channel_unit;

typedef struct {
    AVFrame             frame;
    GetBitContext       gb;
    //@{
    /** stream data */
    int                 channels;
    int                 codingMode;
    int                 bit_rate;
    int                 sample_rate;
    int                 samples_per_channel;
    int                 samples_per_frame;

    int                 bits_per_frame;
    int                 bytes_per_frame;
    int                 pBs;
    channel_unit*       pUnits;
    //@}
    //@{
    /** joint-stereo related variables */
    int                 matrix_coeff_index_prev[4];
    int                 matrix_coeff_index_now[4];
    int                 matrix_coeff_index_next[4];
    int                 weighting_delay[6];
    //@}
    //@{
    /** data buffers */
    float              *outSamples[2];
    uint8_t*            decoded_bytes_buffer;
    float               tempBuf[1070];
    //@}
    //@{
    /** extradata */
    int                 atrac3version;
    int                 delay;
    int                 scrambled_stream;
    int                 frame_factor;
    //@}

    FFTContext          mdct_ctx;
    FmtConvertContext   fmt_conv;
    AVFloatDSPContext   fdsp;
} ATRAC3Context;

static DECLARE_ALIGNED(32, float, mdct_window)[MDCT_SIZE];
static VLC              spectral_coeff_tab[7];
static float            gain_tab1[16];
static float            gain_tab2[31];


/**
 * Regular 512 points IMDCT without overlapping, with the exception of the swapping of odd bands
 * caused by the reverse spectra of the QMF.
 *
 * @param pInput    float input
 * @param pOutput   float output
 * @param odd_band  1 if the band is an odd band
 */

static void IMLT(ATRAC3Context *q, float *pInput, float *pOutput, int odd_band)
{
    int     i;

    if (odd_band) {
        /**
        * Reverse the odd bands before IMDCT, this is an effect of the QMF transform
        * or it gives better compression to do it this way.
        * FIXME: It should be possible to handle this in imdct_calc
        * for that to happen a modification of the prerotation step of
        * all SIMD code and C code is needed.
        * Or fix the functions before so they generate a pre reversed spectrum.
        */

        for (i=0; i<128; i++)
            FFSWAP(float, pInput[i], pInput[255-i]);
    }

    q->mdct_ctx.imdct_calc(&q->mdct_ctx,pOutput,pInput);

    /* Perform windowing on the output. */
    q->fdsp.vector_fmul(pOutput, pOutput, mdct_window, MDCT_SIZE);

}


/**
 * Atrac 3 indata descrambling, only used for data coming from the rm container
 *
 * @param inbuffer  pointer to 8 bit array of indata
 * @param out       pointer to 8 bit array of outdata
 * @param bytes     amount of bytes
 */

static int decode_bytes(const uint8_t* inbuffer, uint8_t* out, int bytes){
    int i, off;
    uint32_t c;
    const uint32_t* buf;
    uint32_t* obuf = (uint32_t*) out;

    off = (intptr_t)inbuffer & 3;
    buf = (const uint32_t*) (inbuffer - off);
    c = av_be2ne32((0x537F6103 >> (off*8)) | (0x537F6103 << (32-(off*8))));
    bytes += 3 + off;
    for (i = 0; i < bytes/4; i++)
        obuf[i] = c ^ buf[i];

    if (off)
        av_log_ask_for_sample(NULL, "Offset of %d not handled.\n", off);

    return off;
}


static av_cold int init_atrac3_transforms(ATRAC3Context *q, int is_float) {
    float enc_window[256];
    int i;

    /* Generate the mdct window, for details see
     * http://wiki.multimedia.cx/index.php?title=RealAudio_atrc#Windows */
    for (i=0 ; i<256; i++)
        enc_window[i] = (sin(((i + 0.5) / 256.0 - 0.5) * M_PI) + 1.0) * 0.5;

    if (!mdct_window[0])
        for (i=0 ; i<256; i++) {
            mdct_window[i] = enc_window[i]/(enc_window[i]*enc_window[i] + enc_window[255-i]*enc_window[255-i]);
            mdct_window[511-i] = mdct_window[i];
        }

    /* Initialize the MDCT transform. */
    return ff_mdct_init(&q->mdct_ctx, 9, 1, is_float ? 1.0 / 32768 : 1.0);
}

/**
 * Atrac3 uninit, free all allocated memory
 */

static av_cold int atrac3_decode_close(AVCodecContext *avctx)
{
    ATRAC3Context *q = avctx->priv_data;

    av_free(q->pUnits);
    av_free(q->decoded_bytes_buffer);
    av_freep(&q->outSamples[0]);

    ff_mdct_end(&q->mdct_ctx);

    return 0;
}

/**
/ * Mantissa decoding
 *
 * @param gb            the GetBit context
 * @param selector      what table is the output values coded with
 * @param codingFlag    constant length coding or variable length coding
 * @param mantissas     mantissa output table
 * @param numCodes      amount of values to get
 */

static void readQuantSpectralCoeffs (GetBitContext *gb, int selector, int codingFlag, int* mantissas, int numCodes)
{
    int   numBits, cnt, code, huffSymb;

    if (selector == 1)
        numCodes /= 2;

    if (codingFlag != 0) {
        /* constant length coding (CLC) */
        numBits = CLCLengthTab[selector];

        if (selector > 1) {
            for (cnt = 0; cnt < numCodes; cnt++) {
                if (numBits)
                    code = get_sbits(gb, numBits);
                else
                    code = 0;
                mantissas[cnt] = code;
            }
        } else {
            for (cnt = 0; cnt < numCodes; cnt++) {
                if (numBits)
                    code = get_bits(gb, numBits); //numBits is always 4 in this case
                else
                    code = 0;
                mantissas[cnt*2] = seTab_0[code >> 2];
                mantissas[cnt*2+1] = seTab_0[code & 3];
            }
        }
    } else {
        /* variable length coding (VLC) */
        if (selector != 1) {
            for (cnt = 0; cnt < numCodes; cnt++) {
                huffSymb = get_vlc2(gb, spectral_coeff_tab[selector-1].table, spectral_coeff_tab[selector-1].bits, 3);
                huffSymb += 1;
                code = huffSymb >> 1;
                if (huffSymb & 1)
                    code = -code;
                mantissas[cnt] = code;
            }
        } else {
            for (cnt = 0; cnt < numCodes; cnt++) {
                huffSymb = get_vlc2(gb, spectral_coeff_tab[selector-1].table, spectral_coeff_tab[selector-1].bits, 3);
                mantissas[cnt*2] = decTable1[huffSymb*2];
                mantissas[cnt*2+1] = decTable1[huffSymb*2+1];
            }
        }
    }
}

/**
 * Restore the quantized band spectrum coefficients
 *
 * @param gb            the GetBit context
 * @param pOut          decoded band spectrum
 * @return outSubbands   subband counter, fix for broken specification/files
 */

static int decodeSpectrum (GetBitContext *gb, float *pOut)
{
    int   numSubbands, codingMode, cnt, first, last, subbWidth, *pIn;
    int   subband_vlc_index[32], SF_idxs[32];
    int   mantissas[128];
    float SF;

    numSubbands = get_bits(gb, 5); // number of coded subbands
    codingMode = get_bits1(gb); // coding Mode: 0 - VLC/ 1-CLC

    /* Get the VLC selector table for the subbands, 0 means not coded. */
    for (cnt = 0; cnt <= numSubbands; cnt++)
        subband_vlc_index[cnt] = get_bits(gb, 3);

    /* Read the scale factor indexes from the stream. */
    for (cnt = 0; cnt <= numSubbands; cnt++) {
        if (subband_vlc_index[cnt] != 0)
            SF_idxs[cnt] = get_bits(gb, 6);
    }

    for (cnt = 0; cnt <= numSubbands; cnt++) {
        first = subbandTab[cnt];
        last = subbandTab[cnt+1];

        subbWidth = last - first;

        if (subband_vlc_index[cnt] != 0) {
            /* Decode spectral coefficients for this subband. */
            /* TODO: This can be done faster is several blocks share the
             * same VLC selector (subband_vlc_index) */
            readQuantSpectralCoeffs (gb, subband_vlc_index[cnt], codingMode, mantissas, subbWidth);

            /* Decode the scale factor for this subband. */
            SF = ff_atrac_sf_table[SF_idxs[cnt]] * iMaxQuant[subband_vlc_index[cnt]];

            /* Inverse quantize the coefficients. */
            for (pIn=mantissas ; first<last; first++, pIn++)
                pOut[first] = *pIn * SF;
        } else {
            /* This subband was not coded, so zero the entire subband. */
            memset(pOut+first, 0, subbWidth*sizeof(float));
        }
    }

    /* Clear the subbands that were not coded. */
    first = subbandTab[cnt];
    memset(pOut+first, 0, (SAMPLES_PER_FRAME - first) * sizeof(float));
    return numSubbands;
}

/**
 * Restore the quantized tonal components
 *
 * @param gb            the GetBit context
 * @param pComponent    tone component
 * @param numBands      amount of coded bands
 */

static int decodeTonalComponents (GetBitContext *gb, tonal_component *pComponent, int numBands)
{
    int i,j,k,cnt;
    int   components, coding_mode_selector, coding_mode, coded_values_per_component;
    int   sfIndx, coded_values, max_coded_values, quant_step_index, coded_components;
    int   band_flags[4], mantissa[8];
    float  *pCoef;
    float  scalefactor;
    int   component_count = 0;

    components = get_bits(gb,5);

    /* no tonal components */
    if (components == 0)
        return 0;

    coding_mode_selector = get_bits(gb,2);
    if (coding_mode_selector == 2)
        return AVERROR_INVALIDDATA;

    coding_mode = coding_mode_selector & 1;

    for (i = 0; i < components; i++) {
        for (cnt = 0; cnt <= numBands; cnt++)
            band_flags[cnt] = get_bits1(gb);

        coded_values_per_component = get_bits(gb,3);

        quant_step_index = get_bits(gb,3);
        if (quant_step_index <= 1)
            return AVERROR_INVALIDDATA;

        if (coding_mode_selector == 3)
            coding_mode = get_bits1(gb);

        for (j = 0; j < (numBands + 1) * 4; j++) {
            if (band_flags[j >> 2] == 0)
                continue;

            coded_components = get_bits(gb,3);

            for (k=0; k<coded_components; k++) {
                sfIndx = get_bits(gb,6);
                if (component_count >= 64)
                    return AVERROR_INVALIDDATA;
                pComponent[component_count].pos = j * 64 + (get_bits(gb,6));
                max_coded_values = SAMPLES_PER_FRAME - pComponent[component_count].pos;
                coded_values = coded_values_per_component + 1;
                coded_values = FFMIN(max_coded_values,coded_values);

                scalefactor = ff_atrac_sf_table[sfIndx] * iMaxQuant[quant_step_index];

                readQuantSpectralCoeffs(gb, quant_step_index, coding_mode, mantissa, coded_values);

                pComponent[component_count].numCoefs = coded_values;

                /* inverse quant */
                pCoef = pComponent[component_count].coef;
                for (cnt = 0; cnt < coded_values; cnt++)
                    pCoef[cnt] = mantissa[cnt] * scalefactor;

                component_count++;
            }
        }
    }

    return component_count;
}

/**
 * Decode gain parameters for the coded bands
 *
 * @param gb            the GetBit context
 * @param pGb           the gainblock for the current band
 * @param numBands      amount of coded bands
 */

static int decodeGainControl (GetBitContext *gb, gain_block *pGb, int numBands)
{
    int   i, cf, numData;
    int   *pLevel, *pLoc;

    gain_info   *pGain = pGb->gBlock;

    for (i=0 ; i<=numBands; i++)
    {
        numData = get_bits(gb,3);
        pGain[i].num_gain_data = numData;
        pLevel = pGain[i].levcode;
        pLoc = pGain[i].loccode;

        for (cf = 0; cf < numData; cf++){
            pLevel[cf]= get_bits(gb,4);
            pLoc  [cf]= get_bits(gb,5);
            if(cf && pLoc[cf] <= pLoc[cf-1])
                return AVERROR_INVALIDDATA;
        }
    }

    /* Clear the unused blocks. */
    for (; i<4 ; i++)
        pGain[i].num_gain_data = 0;

    return 0;
}

/**
 * Apply gain parameters and perform the MDCT overlapping part
 *
 * @param pIn           input float buffer
 * @param pPrev         previous float buffer to perform overlap against
 * @param pOut          output float buffer
 * @param pGain1        current band gain info
 * @param pGain2        next band gain info
 */

static void gainCompensateAndOverlap (float *pIn, float *pPrev, float *pOut, gain_info *pGain1, gain_info *pGain2)
{
    /* gain compensation function */
    float  gain1, gain2, gain_inc;
    int   cnt, numdata, nsample, startLoc, endLoc;


    if (pGain2->num_gain_data == 0)
        gain1 = 1.0;
    else
        gain1 = gain_tab1[pGain2->levcode[0]];

    if (pGain1->num_gain_data == 0) {
        for (cnt = 0; cnt < 256; cnt++)
            pOut[cnt] = pIn[cnt] * gain1 + pPrev[cnt];
    } else {
        numdata = pGain1->num_gain_data;
        pGain1->loccode[numdata] = 32;
        pGain1->levcode[numdata] = 4;

        nsample = 0; // current sample = 0

        for (cnt = 0; cnt < numdata; cnt++) {
            startLoc = pGain1->loccode[cnt] * 8;
            endLoc = startLoc + 8;

            gain2 = gain_tab1[pGain1->levcode[cnt]];
            gain_inc = gain_tab2[(pGain1->levcode[cnt+1] - pGain1->levcode[cnt])+15];

            /* interpolate */
            for (; nsample < startLoc; nsample++)
                pOut[nsample] = (pIn[nsample] * gain1 + pPrev[nsample]) * gain2;

            /* interpolation is done over eight samples */
            for (; nsample < endLoc; nsample++) {
                pOut[nsample] = (pIn[nsample] * gain1 + pPrev[nsample]) * gain2;
                gain2 *= gain_inc;
            }
        }

        for (; nsample < 256; nsample++)
            pOut[nsample] = (pIn[nsample] * gain1) + pPrev[nsample];
    }

    /* Delay for the overlapping part. */
    memcpy(pPrev, &pIn[256], 256*sizeof(float));
}

/**
 * Combine the tonal band spectrum and regular band spectrum
 * Return position of the last tonal coefficient
 *
 * @param pSpectrum     output spectrum buffer
 * @param numComponents amount of tonal components
 * @param pComponent    tonal components for this band
 */

static int addTonalComponents (float *pSpectrum, int numComponents, tonal_component *pComponent)
{
    int   cnt, i, lastPos = -1;
    float   *pIn, *pOut;

    for (cnt = 0; cnt < numComponents; cnt++){
        lastPos = FFMAX(pComponent[cnt].pos + pComponent[cnt].numCoefs, lastPos);
        pIn = pComponent[cnt].coef;
        pOut = &(pSpectrum[pComponent[cnt].pos]);

        for (i=0 ; i<pComponent[cnt].numCoefs ; i++)
            pOut[i] += pIn[i];
    }

    return lastPos;
}


#define INTERPOLATE(old,new,nsample) ((old) + (nsample)*0.125*((new)-(old)))

static void reverseMatrixing(float *su1, float *su2, int *pPrevCode, int *pCurrCode)
{
    int    i, band, nsample, s1, s2;
    float    c1, c2;
    float    mc1_l, mc1_r, mc2_l, mc2_r;

    for (i=0,band = 0; band < 4*256; band+=256,i++) {
        s1 = pPrevCode[i];
        s2 = pCurrCode[i];
        nsample = 0;

        if (s1 != s2) {
            /* Selector value changed, interpolation needed. */
            mc1_l = matrixCoeffs[s1*2];
            mc1_r = matrixCoeffs[s1*2+1];
            mc2_l = matrixCoeffs[s2*2];
            mc2_r = matrixCoeffs[s2*2+1];

            /* Interpolation is done over the first eight samples. */
            for(; nsample < 8; nsample++) {
                c1 = su1[band+nsample];
                c2 = su2[band+nsample];
                c2 = c1 * INTERPOLATE(mc1_l,mc2_l,nsample) + c2 * INTERPOLATE(mc1_r,mc2_r,nsample);
                su1[band+nsample] = c2;
                su2[band+nsample] = c1 * 2.0 - c2;
            }
        }

        /* Apply the matrix without interpolation. */
        switch (s2) {
            case 0:     /* M/S decoding */
                for (; nsample < 256; nsample++) {
                    c1 = su1[band+nsample];
                    c2 = su2[band+nsample];
                    su1[band+nsample] = c2 * 2.0;
                    su2[band+nsample] = (c1 - c2) * 2.0;
                }
                break;

            case 1:
                for (; nsample < 256; nsample++) {
                    c1 = su1[band+nsample];
                    c2 = su2[band+nsample];
                    su1[band+nsample] = (c1 + c2) * 2.0;
                    su2[band+nsample] = c2 * -2.0;
                }
                break;
            case 2:
            case 3:
                for (; nsample < 256; nsample++) {
                    c1 = su1[band+nsample];
                    c2 = su2[band+nsample];
                    su1[band+nsample] = c1 + c2;
                    su2[band+nsample] = c1 - c2;
                }
                break;
            default:
                av_assert1(0);
        }
    }
}

static void getChannelWeights (int indx, int flag, float ch[2]){

    if (indx == 7) {
        ch[0] = 1.0;
        ch[1] = 1.0;
    } else {
        ch[0] = (float)(indx & 7) / 7.0;
        ch[1] = sqrt(2 - ch[0]*ch[0]);
        if(flag)
            FFSWAP(float, ch[0], ch[1]);
    }
}

static void channelWeighting (float *su1, float *su2, int *p3)
{
    int   band, nsample;
    /* w[x][y] y=0 is left y=1 is right */
    float w[2][2];

    if (p3[1] != 7 || p3[3] != 7){
        getChannelWeights(p3[1], p3[0], w[0]);
        getChannelWeights(p3[3], p3[2], w[1]);

        for(band = 1; band < 4; band++) {
            /* scale the channels by the weights */
            for(nsample = 0; nsample < 8; nsample++) {
                su1[band*256+nsample] *= INTERPOLATE(w[0][0], w[0][1], nsample);
                su2[band*256+nsample] *= INTERPOLATE(w[1][0], w[1][1], nsample);
            }

            for(; nsample < 256; nsample++) {
                su1[band*256+nsample] *= w[1][0];
                su2[band*256+nsample] *= w[1][1];
            }
        }
    }
}


/**
 * Decode a Sound Unit
 *
 * @param gb            the GetBit context
 * @param pSnd          the channel unit to be used
 * @param pOut          the decoded samples before IQMF in float representation
 * @param channelNum    channel number
 * @param codingMode    the coding mode (JOINT_STEREO or regular stereo/mono)
 */


static int decodeChannelSoundUnit (ATRAC3Context *q, GetBitContext *gb, channel_unit *pSnd, float *pOut, int channelNum, int codingMode)
{
    int   band, result=0, numSubbands, lastTonal, numBands;

    if (codingMode == JOINT_STEREO && channelNum == 1) {
        if (get_bits(gb,2) != 3) {
            av_log(NULL,AV_LOG_ERROR,"JS mono Sound Unit id != 3.\n");
            return AVERROR_INVALIDDATA;
        }
    } else {
        if (get_bits(gb,6) != 0x28) {
            av_log(NULL,AV_LOG_ERROR,"Sound Unit id != 0x28.\n");
            return AVERROR_INVALIDDATA;
        }
    }

    /* number of coded QMF bands */
    pSnd->bandsCoded = get_bits(gb,2);

    result = decodeGainControl (gb, &(pSnd->gainBlock[pSnd->gcBlkSwitch]), pSnd->bandsCoded);
    if (result) return result;

    pSnd->numComponents = decodeTonalComponents (gb, pSnd->components, pSnd->bandsCoded);
    if (pSnd->numComponents == -1) return -1;

    numSubbands = decodeSpectrum (gb, pSnd->spectrum);

    /* Merge the decoded spectrum and tonal components. */
    lastTonal = addTonalComponents (pSnd->spectrum, pSnd->numComponents, pSnd->components);


    /* calculate number of used MLT/QMF bands according to the amount of coded spectral lines */
    numBands = (subbandTab[numSubbands] - 1) >> 8;
    if (lastTonal >= 0)
        numBands = FFMAX((lastTonal + 256) >> 8, numBands);


    /* Reconstruct time domain samples. */
    for (band=0; band<4; band++) {
        /* Perform the IMDCT step without overlapping. */
        if (band <= numBands) {
            IMLT(q, &(pSnd->spectrum[band*256]), pSnd->IMDCT_buf, band&1);
        } else
            memset(pSnd->IMDCT_buf, 0, 512 * sizeof(float));

        /* gain compensation and overlapping */
        gainCompensateAndOverlap(pSnd->IMDCT_buf, &pSnd->prevFrame[band * 256],
                                 &pOut[band * 256],
                                 &pSnd->gainBlock[1 - pSnd->gcBlkSwitch].gBlock[band],
                                 &pSnd->gainBlock[    pSnd->gcBlkSwitch].gBlock[band]);
    }

    /* Swap the gain control buffers for the next frame. */
    pSnd->gcBlkSwitch ^= 1;

    return 0;
}

/**
 * Frame handling
 *
 * @param q             Atrac3 private context
 * @param databuf       the input data
 */

static int decodeFrame(ATRAC3Context *q, const uint8_t* databuf,
                       float **out_samples)
{
    int   result, i;
    float   *p1, *p2, *p3, *p4;
    uint8_t *ptr1;

    if (q->codingMode == JOINT_STEREO) {

        /* channel coupling mode */
        /* decode Sound Unit 1 */
        init_get_bits(&q->gb,databuf,q->bits_per_frame);

        result = decodeChannelSoundUnit(q,&q->gb, q->pUnits, out_samples[0], 0, JOINT_STEREO);
        if (result != 0)
            return result;

        /* Framedata of the su2 in the joint-stereo mode is encoded in
         * reverse byte order so we need to swap it first. */
        if (databuf == q->decoded_bytes_buffer) {
            uint8_t *ptr2 = q->decoded_bytes_buffer+q->bytes_per_frame-1;
            ptr1 = q->decoded_bytes_buffer;
            for (i = 0; i < (q->bytes_per_frame/2); i++, ptr1++, ptr2--) {
                FFSWAP(uint8_t,*ptr1,*ptr2);
            }
        } else {
            const uint8_t *ptr2 = databuf+q->bytes_per_frame-1;
            for (i = 0; i < q->bytes_per_frame; i++)
                q->decoded_bytes_buffer[i] = *ptr2--;
        }

        /* Skip the sync codes (0xF8). */
        ptr1 = q->decoded_bytes_buffer;
        for (i = 4; *ptr1 == 0xF8; i++, ptr1++) {
            if (i >= q->bytes_per_frame)
                return AVERROR_INVALIDDATA;
        }


        /* set the bitstream reader at the start of the second Sound Unit*/
        init_get_bits(&q->gb,ptr1,q->bits_per_frame);

        /* Fill the Weighting coeffs delay buffer */
        memmove(q->weighting_delay,&(q->weighting_delay[2]),4*sizeof(int));
        q->weighting_delay[4] = get_bits1(&q->gb);
        q->weighting_delay[5] = get_bits(&q->gb,3);

        for (i = 0; i < 4; i++) {
            q->matrix_coeff_index_prev[i] = q->matrix_coeff_index_now[i];
            q->matrix_coeff_index_now[i] = q->matrix_coeff_index_next[i];
            q->matrix_coeff_index_next[i] = get_bits(&q->gb,2);
        }

        /* Decode Sound Unit 2. */
        result = decodeChannelSoundUnit(q,&q->gb, &q->pUnits[1], out_samples[1], 1, JOINT_STEREO);
        if (result != 0)
            return result;

        /* Reconstruct the channel coefficients. */
        reverseMatrixing(out_samples[0], out_samples[1], q->matrix_coeff_index_prev, q->matrix_coeff_index_now);

        channelWeighting(out_samples[0], out_samples[1], q->weighting_delay);

    } else {
        /* normal stereo mode or mono */
        /* Decode the channel sound units. */
        for (i=0 ; i<q->channels ; i++) {

            /* Set the bitstream reader at the start of a channel sound unit. */
            init_get_bits(&q->gb,
                          databuf + i * q->bytes_per_frame / q->channels,
                          q->bits_per_frame / q->channels);

            result = decodeChannelSoundUnit(q,&q->gb, &q->pUnits[i], out_samples[i], i, q->codingMode);
            if (result != 0)
                return result;
        }
    }

    /* Apply the iQMF synthesis filter. */
    for (i=0 ; i<q->channels ; i++) {
        p1 = out_samples[i];
        p2= p1+256;
        p3= p2+256;
        p4= p3+256;
        ff_atrac_iqmf (p1, p2, 256, p1, q->pUnits[i].delayBuf1, q->tempBuf);
        ff_atrac_iqmf (p4, p3, 256, p3, q->pUnits[i].delayBuf2, q->tempBuf);
        ff_atrac_iqmf (p1, p3, 512, p1, q->pUnits[i].delayBuf3, q->tempBuf);
    }

    return 0;
}


/**
 * Atrac frame decoding
 *
 * @param avctx     pointer to the AVCodecContext
 */

static int atrac3_decode_frame(AVCodecContext *avctx, void *data,
                               int *got_frame_ptr, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    ATRAC3Context *q = avctx->priv_data;
    int result;
    const uint8_t* databuf;
    float   *samples_flt;
    int16_t *samples_s16;

    if (buf_size < avctx->block_align) {
        av_log(avctx, AV_LOG_ERROR,
               "Frame too small (%d bytes). Truncated file?\n", buf_size);
        return AVERROR_INVALIDDATA;
    }

    /* get output buffer */
    q->frame.nb_samples = SAMPLES_PER_FRAME;
    if ((result = avctx->get_buffer(avctx, &q->frame)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return result;
    }
    samples_flt = (float   *)q->frame.data[0];
    samples_s16 = (int16_t *)q->frame.data[0];

    /* Check if we need to descramble and what buffer to pass on. */
    if (q->scrambled_stream) {
        decode_bytes(buf, q->decoded_bytes_buffer, avctx->block_align);
        databuf = q->decoded_bytes_buffer;
    } else {
        databuf = buf;
    }

    if (q->channels == 1 && avctx->sample_fmt == AV_SAMPLE_FMT_FLT)
        result = decodeFrame(q, databuf, &samples_flt);
    else
        result = decodeFrame(q, databuf, q->outSamples);

    if (result != 0) {
        av_log(NULL,AV_LOG_ERROR,"Frame decoding error!\n");
        return result;
    }

    /* interleave */
    if (q->channels == 2 && avctx->sample_fmt == AV_SAMPLE_FMT_FLT) {
        q->fmt_conv.float_interleave(samples_flt,
                                     (const float **)q->outSamples,
                                     SAMPLES_PER_FRAME, 2);
    } else if (avctx->sample_fmt == AV_SAMPLE_FMT_S16) {
        q->fmt_conv.float_to_int16_interleave(samples_s16,
                                              (const float **)q->outSamples,
                                              SAMPLES_PER_FRAME, q->channels);
    }

    *got_frame_ptr   = 1;
    *(AVFrame *)data = q->frame;

    return avctx->block_align;
}


/**
 * Atrac3 initialization
 *
 * @param avctx     pointer to the AVCodecContext
 */

static av_cold int atrac3_decode_init(AVCodecContext *avctx)
{
    int i, ret;
    const uint8_t *edata_ptr = avctx->extradata;
    ATRAC3Context *q = avctx->priv_data;
    static VLC_TYPE atrac3_vlc_table[4096][2];
    static int vlcs_initialized = 0;

    /* Take data from the AVCodecContext (RM container). */
    q->sample_rate = avctx->sample_rate;
    q->channels = avctx->channels;
    q->bit_rate = avctx->bit_rate;
    q->bits_per_frame = avctx->block_align * 8;
    q->bytes_per_frame = avctx->block_align;

    /* Take care of the codec-specific extradata. */
    if (avctx->extradata_size == 14) {
        /* Parse the extradata, WAV format */
        av_log(avctx,AV_LOG_DEBUG,"[0-1] %d\n",bytestream_get_le16(&edata_ptr));  //Unknown value always 1
        q->samples_per_channel = bytestream_get_le32(&edata_ptr);
        q->codingMode = bytestream_get_le16(&edata_ptr);
        av_log(avctx,AV_LOG_DEBUG,"[8-9] %d\n",bytestream_get_le16(&edata_ptr));  //Dupe of coding mode
        q->frame_factor = bytestream_get_le16(&edata_ptr);  //Unknown always 1
        av_log(avctx,AV_LOG_DEBUG,"[12-13] %d\n",bytestream_get_le16(&edata_ptr));  //Unknown always 0

        /* setup */
        q->samples_per_frame = SAMPLES_PER_FRAME * q->channels;
        q->atrac3version = 4;
        q->delay = 0x88E;
        if (q->codingMode)
            q->codingMode = JOINT_STEREO;
        else
            q->codingMode = STEREO;

        q->scrambled_stream = 0;

        if ((q->bytes_per_frame == 96*q->channels*q->frame_factor) || (q->bytes_per_frame == 152*q->channels*q->frame_factor) || (q->bytes_per_frame == 192*q->channels*q->frame_factor)) {
        } else {
            av_log(avctx,AV_LOG_ERROR,"Unknown frame/channel/frame_factor configuration %d/%d/%d\n", q->bytes_per_frame, q->channels, q->frame_factor);
            return AVERROR_INVALIDDATA;
        }

    } else if (avctx->extradata_size == 10) {
        /* Parse the extradata, RM format. */
        q->atrac3version = bytestream_get_be32(&edata_ptr);
        q->samples_per_frame = bytestream_get_be16(&edata_ptr);
        q->delay = bytestream_get_be16(&edata_ptr);
        q->codingMode = bytestream_get_be16(&edata_ptr);

        q->samples_per_channel = q->samples_per_frame / q->channels;
        q->scrambled_stream = 1;

    } else {
        av_log(NULL,AV_LOG_ERROR,"Unknown extradata size %d.\n",avctx->extradata_size);
    }
    /* Check the extradata. */

    if (q->atrac3version != 4) {
        av_log(avctx,AV_LOG_ERROR,"Version %d != 4.\n",q->atrac3version);
        return AVERROR_INVALIDDATA;
    }

    if (q->samples_per_frame != SAMPLES_PER_FRAME && q->samples_per_frame != SAMPLES_PER_FRAME*2) {
        av_log(avctx,AV_LOG_ERROR,"Unknown amount of samples per frame %d.\n",q->samples_per_frame);
        return AVERROR_INVALIDDATA;
    }

    if (q->delay != 0x88E) {
        av_log(avctx,AV_LOG_ERROR,"Unknown amount of delay %x != 0x88E.\n",q->delay);
        return AVERROR_INVALIDDATA;
    }

    if (q->codingMode == STEREO) {
        av_log(avctx,AV_LOG_DEBUG,"Normal stereo detected.\n");
    } else if (q->codingMode == JOINT_STEREO) {
        av_log(avctx,AV_LOG_DEBUG,"Joint stereo detected.\n");
    } else {
        av_log(avctx,AV_LOG_ERROR,"Unknown channel coding mode %x!\n",q->codingMode);
        return AVERROR_INVALIDDATA;
    }

    if (avctx->channels <= 0 || avctx->channels > 2 /*|| ((avctx->channels * 1024) != q->samples_per_frame)*/) {
        av_log(avctx,AV_LOG_ERROR,"Channel configuration error!\n");
        return AVERROR(EINVAL);
    }


    if(avctx->block_align >= UINT_MAX/2)
        return AVERROR(EINVAL);

    /* Pad the data buffer with FF_INPUT_BUFFER_PADDING_SIZE,
     * this is for the bitstream reader. */
    if ((q->decoded_bytes_buffer = av_mallocz((avctx->block_align+(4-avctx->block_align%4) + FF_INPUT_BUFFER_PADDING_SIZE)))  == NULL)
        return AVERROR(ENOMEM);


    /* Initialize the VLC tables. */
    if (!vlcs_initialized) {
        for (i=0 ; i<7 ; i++) {
            spectral_coeff_tab[i].table = &atrac3_vlc_table[atrac3_vlc_offs[i]];
            spectral_coeff_tab[i].table_allocated = atrac3_vlc_offs[i + 1] - atrac3_vlc_offs[i];
            init_vlc (&spectral_coeff_tab[i], 9, huff_tab_sizes[i],
                huff_bits[i], 1, 1,
                huff_codes[i], 1, 1, INIT_VLC_USE_NEW_STATIC);
        }
        vlcs_initialized = 1;
    }

    if (avctx->request_sample_fmt == AV_SAMPLE_FMT_FLT)
        avctx->sample_fmt = AV_SAMPLE_FMT_FLT;
    else
        avctx->sample_fmt = AV_SAMPLE_FMT_S16;

    if ((ret = init_atrac3_transforms(q, avctx->sample_fmt == AV_SAMPLE_FMT_FLT))) {
        av_log(avctx, AV_LOG_ERROR, "Error initializing MDCT\n");
        av_freep(&q->decoded_bytes_buffer);
        return ret;
    }

    ff_atrac_generate_tables();

    /* Generate gain tables. */
    for (i=0 ; i<16 ; i++)
        gain_tab1[i] = exp2f (4 - i);

    for (i=-15 ; i<16 ; i++)
        gain_tab2[i+15] = exp2f (i * -0.125);

    /* init the joint-stereo decoding data */
    q->weighting_delay[0] = 0;
    q->weighting_delay[1] = 7;
    q->weighting_delay[2] = 0;
    q->weighting_delay[3] = 7;
    q->weighting_delay[4] = 0;
    q->weighting_delay[5] = 7;

    for (i=0; i<4; i++) {
        q->matrix_coeff_index_prev[i] = 3;
        q->matrix_coeff_index_now[i] = 3;
        q->matrix_coeff_index_next[i] = 3;
    }

    avpriv_float_dsp_init(&q->fdsp, avctx->flags & CODEC_FLAG_BITEXACT);
    ff_fmt_convert_init(&q->fmt_conv, avctx);

    q->pUnits = av_mallocz(sizeof(channel_unit)*q->channels);
    if (!q->pUnits) {
        atrac3_decode_close(avctx);
        return AVERROR(ENOMEM);
    }

    if (avctx->channels > 1 || avctx->sample_fmt == AV_SAMPLE_FMT_S16) {
        q->outSamples[0] = av_mallocz(SAMPLES_PER_FRAME * avctx->channels * sizeof(*q->outSamples[0]));
        q->outSamples[1] = q->outSamples[0] + SAMPLES_PER_FRAME;
        if (!q->outSamples[0]) {
            atrac3_decode_close(avctx);
            return AVERROR(ENOMEM);
        }
    }

    avcodec_get_frame_defaults(&q->frame);
    avctx->coded_frame = &q->frame;

    return 0;
}


AVCodec ff_atrac3_decoder =
{
    .name           = "atrac3",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_ATRAC3,
    .priv_data_size = sizeof(ATRAC3Context),
    .init           = atrac3_decode_init,
    .close          = atrac3_decode_close,
    .decode         = atrac3_decode_frame,
    .capabilities   = CODEC_CAP_SUBFRAMES | CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("Atrac 3 (Adaptive TRansform Acoustic Coding 3)"),
};
