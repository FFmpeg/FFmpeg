/*
 * COOK compatible decoder
 * Copyright (c) 2003 Sascha Sommer
 * Copyright (c) 2005 Benjamin Larsson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/**
 * @file cook.c
 * Cook compatible decoder.
 * This decoder handles RealNetworks, RealAudio G2 data.
 * Cook is identified by the codec name cook in RM files.
 *
 * To use this decoder, a calling application must supply the extradata
 * bytes provided from the RM container; 8+ bytes for mono streams and
 * 16+ for stereo streams (maybe more).
 *
 * Codec technicalities (all this assume a buffer length of 1024):
 * Cook works with several different techniques to achieve its compression.
 * In the timedomain the buffer is divided into 8 pieces and quantized. If
 * two neighboring pieces have different quantization index a smooth
 * quantization curve is used to get a smooth overlap between the different
 * pieces.
 * To get to the transformdomain Cook uses a modulated lapped transform.
 * The transform domain has 50 subbands with 20 elements each. This
 * means only a maximum of 50*20=1000 coefficients are used out of the 1024
 * available.
 */

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#define ALT_BITSTREAM_READER
#include "avcodec.h"
#include "bitstream.h"
#include "dsputil.h"

#include "cookdata.h"

/* the different Cook versions */
#define MONO_COOK1      0x1000001
#define MONO_COOK2      0x1000002
#define JOINT_STEREO    0x1000003
#define MC_COOK         0x2000000   //multichannel Cook, not supported

#define SUBBAND_SIZE    20
//#define COOKDEBUG

typedef struct {
    int     size;
    int     qidx_table1[8];
    int     qidx_table2[8];
} COOKgain;

typedef struct __attribute__((__packed__)){
    /* codec data start */
    uint32_t cookversion;               //in network order, bigendian
    uint16_t samples_per_frame;         //amount of samples per frame per channel, bigendian
    uint16_t subbands;                  //amount of bands used in the frequency domain, bigendian
    /* Mono extradata ends here. */
    uint32_t unused;
    uint16_t js_subband_start;          //bigendian
    uint16_t js_vlc_bits;               //bigendian
    /* Stereo extradata ends here. */
} COOKextradata;


typedef struct {
    GetBitContext       gb;
    /* stream data */
    int                 nb_channels;
    int                 joint_stereo;
    int                 bit_rate;
    int                 sample_rate;
    int                 samples_per_channel;
    int                 samples_per_frame;
    int                 subbands;
    int                 log2_numvector_size;
    int                 numvector_size;                //1 << log2_numvector_size;
    int                 js_subband_start;
    int                 total_subbands;
    int                 num_vectors;
    int                 bits_per_subpacket;
    /* states */
    int                 random_state;

    /* transform data */
    FFTContext          fft_ctx;
    FFTSample           mlt_tmp[1024] __attribute__((aligned(16))); /* temporary storage for imlt */
    float*              mlt_window;
    float*              mlt_precos;
    float*              mlt_presin;
    float*              mlt_postcos;
    int                 fft_size;
    int                 fft_order;
    int                 mlt_size;       //modulated lapped transform size

    /* gain buffers */
    COOKgain*           gain_now_ptr;
    COOKgain*           gain_previous_ptr;
    COOKgain            gain_current;
    COOKgain            gain_now;
    COOKgain            gain_previous;
    COOKgain            gain_channel1[2];
    COOKgain            gain_channel2[2];

    /* VLC data */
    int                 js_vlc_bits;
    VLC                 envelope_quant_index[13];
    VLC                 sqvh[7];          //scalar quantization
    VLC                 ccpl;             //channel coupling

    /* generatable tables and related variables */
    int                 gain_size_factor;
    float               gain_table[23];
    float               pow2tab[127];
    float               rootpow2tab[127];

    /* data buffers */

    uint8_t*            decoded_bytes_buffer;
    float               mono_mdct_output[2048] __attribute__((aligned(16)));
    float*              previous_buffer_ptr[2];
    float               mono_previous_buffer1[1024];
    float               mono_previous_buffer2[1024];
    float*              decode_buf_ptr[4];
    float*              decode_buf_ptr2[2];
    float               decode_buffer_1[1024];
    float               decode_buffer_2[1024];
    float               decode_buffer_3[1024];
    float               decode_buffer_4[1024];
} COOKContext;

/* debug functions */

#ifdef COOKDEBUG
static void dump_float_table(float* table, int size, int delimiter) {
    int i=0;
    av_log(NULL,AV_LOG_ERROR,"\n[%d]: ",i);
    for (i=0 ; i<size ; i++) {
        av_log(NULL, AV_LOG_ERROR, "%5.1f, ", table[i]);
        if ((i+1)%delimiter == 0) av_log(NULL,AV_LOG_ERROR,"\n[%d]: ",i+1);
    }
}

static void dump_int_table(int* table, int size, int delimiter) {
    int i=0;
    av_log(NULL,AV_LOG_ERROR,"\n[%d]: ",i);
    for (i=0 ; i<size ; i++) {
        av_log(NULL, AV_LOG_ERROR, "%d, ", table[i]);
        if ((i+1)%delimiter == 0) av_log(NULL,AV_LOG_ERROR,"\n[%d]: ",i+1);
    }
}

static void dump_short_table(short* table, int size, int delimiter) {
    int i=0;
    av_log(NULL,AV_LOG_ERROR,"\n[%d]: ",i);
    for (i=0 ; i<size ; i++) {
        av_log(NULL, AV_LOG_ERROR, "%d, ", table[i]);
        if ((i+1)%delimiter == 0) av_log(NULL,AV_LOG_ERROR,"\n[%d]: ",i+1);
    }
}

#endif

/*************** init functions ***************/

/* table generator */
static void init_pow2table(COOKContext *q){
    int i;
    q->pow2tab[63] = 1.0;
    for (i=1 ; i<64 ; i++){
        q->pow2tab[63+i]=(float)((uint64_t)1<<i);
        q->pow2tab[63-i]=1.0/(float)((uint64_t)1<<i);
    }
}

/* table generator */
static void init_rootpow2table(COOKContext *q){
    int i;
    q->rootpow2tab[63] = 1.0;
    for (i=1 ; i<64 ; i++){
        q->rootpow2tab[63+i]=sqrt((float)((uint64_t)1<<i));
        q->rootpow2tab[63-i]=sqrt(1.0/(float)((uint64_t)1<<i));
    }
}

/* table generator */
static void init_gain_table(COOKContext *q) {
    int i;
    q->gain_size_factor = q->samples_per_channel/8;
    for (i=0 ; i<23 ; i++) {
        q->gain_table[i] = pow((double)q->pow2tab[i+52] ,
                               (1.0/(double)q->gain_size_factor));
    }
}


static int init_cook_vlc_tables(COOKContext *q) {
    int i, result;

    result = 0;
    for (i=0 ; i<13 ; i++) {
        result &= init_vlc (&q->envelope_quant_index[i], 9, 24,
            envelope_quant_index_huffbits[i], 1, 1,
            envelope_quant_index_huffcodes[i], 2, 2, 0);
    }
    av_log(NULL,AV_LOG_DEBUG,"sqvh VLC init\n");
    for (i=0 ; i<7 ; i++) {
        result &= init_vlc (&q->sqvh[i], vhvlcsize_tab[i], vhsize_tab[i],
            cvh_huffbits[i], 1, 1,
            cvh_huffcodes[i], 2, 2, 0);
    }

    if (q->nb_channels==2 && q->joint_stereo==1){
        result &= init_vlc (&q->ccpl, 6, (1<<q->js_vlc_bits)-1,
            ccpl_huffbits[q->js_vlc_bits-2], 1, 1,
            ccpl_huffcodes[q->js_vlc_bits-2], 2, 2, 0);
        av_log(NULL,AV_LOG_DEBUG,"Joint-stereo VLC used.\n");
    }

    av_log(NULL,AV_LOG_DEBUG,"VLC tables initialized.\n");
    return result;
}

static int init_cook_mlt(COOKContext *q) {
    int j;
    float alpha;

    /* Allocate the buffers, could be replaced with a static [512]
       array if needed. */
    q->mlt_size = q->samples_per_channel;
    q->mlt_window = av_malloc(sizeof(float)*q->mlt_size);
    q->mlt_precos = av_malloc(sizeof(float)*q->mlt_size/2);
    q->mlt_presin = av_malloc(sizeof(float)*q->mlt_size/2);
    q->mlt_postcos = av_malloc(sizeof(float)*q->mlt_size/2);

    /* Initialize the MLT window: simple sine window. */
    alpha = M_PI / (2.0 * (float)q->mlt_size);
    for(j=0 ; j<q->mlt_size ; j++) {
        q->mlt_window[j] = sin((j + 512.0/(float)q->mlt_size) * alpha);
    }

    /* pre/post twiddle factors */
    for (j=0 ; j<q->mlt_size/2 ; j++){
        q->mlt_precos[j] = cos( ((j+0.25)*M_PI)/q->mlt_size);
        q->mlt_presin[j] = sin( ((j+0.25)*M_PI)/q->mlt_size);
        q->mlt_postcos[j] = (float)sqrt(2.0/(float)q->mlt_size)*cos( ((float)j*M_PI) /q->mlt_size); //sqrt(2/MLT_size) = scalefactor
    }

    /* Initialize the FFT. */
    ff_fft_init(&q->fft_ctx, av_log2(q->mlt_size)-1, 0);
    av_log(NULL,AV_LOG_DEBUG,"FFT initialized, order = %d.\n",
           av_log2(q->samples_per_channel)-1);

    return (int)(q->mlt_window && q->mlt_precos && q->mlt_presin && q->mlt_postcos);
}

/*************** init functions end ***********/

/**
 * Cook indata decoding, every 32 bits are XORed with 0x37c511f2.
 * Why? No idea, some checksum/error detection method maybe.
 * Nice way to waste CPU cycles.
 *
 * @param in        pointer to 32bit array of indata
 * @param bits      amount of bits
 * @param out       pointer to 32bit array of outdata
 */

static inline void decode_bytes(uint8_t* inbuffer, uint8_t* out, int bytes){
    int i;
    uint32_t* buf = (uint32_t*) inbuffer;
    uint32_t* obuf = (uint32_t*) out;
    /* FIXME: 64 bit platforms would be able to do 64 bits at a time.
     * I'm too lazy though, should be something like
     * for(i=0 ; i<bitamount/64 ; i++)
     *     (int64_t)out[i] = 0x37c511f237c511f2^be2me_64(int64_t)in[i]);
     * Buffer alignment needs to be checked. */


    for(i=0 ; i<bytes/4 ; i++){
#ifdef WORDS_BIGENDIAN
        obuf[i] = 0x37c511f2^buf[i];
#else
        obuf[i] = 0xf211c537^buf[i];
#endif
    }
}

/**
 * Cook uninit
 */

static int cook_decode_close(AVCodecContext *avctx)
{
    int i;
    COOKContext *q = avctx->priv_data;
    av_log(NULL,AV_LOG_DEBUG, "Deallocating memory.\n");

    /* Free allocated memory buffers. */
    av_free(q->mlt_window);
    av_free(q->mlt_precos);
    av_free(q->mlt_presin);
    av_free(q->mlt_postcos);
    av_free(q->decoded_bytes_buffer);

    /* Free the transform. */
    ff_fft_end(&q->fft_ctx);

    /* Free the VLC tables. */
    for (i=0 ; i<13 ; i++) {
        free_vlc(&q->envelope_quant_index[i]);
    }
    for (i=0 ; i<7 ; i++) {
        free_vlc(&q->sqvh[i]);
    }
    if(q->nb_channels==2 && q->joint_stereo==1 ){
        free_vlc(&q->ccpl);
    }

    av_log(NULL,AV_LOG_DEBUG,"Memory deallocated.\n");

    return 0;
}

/**
 * Fill the COOKgain structure for the timedomain quantization.
 *
 * @param q                 pointer to the COOKContext
 * @param gaininfo          pointer to the COOKgain
 */

static void decode_gain_info(GetBitContext *gb, COOKgain* gaininfo) {
    int i;

    while (get_bits1(gb)) {}

    gaininfo->size = get_bits_count(gb) - 1;     //amount of elements*2 to update

    if (get_bits_count(gb) - 1 <= 0) return;

    for (i=0 ; i<gaininfo->size ; i++){
        gaininfo->qidx_table1[i] = get_bits(gb,3);
        if (get_bits1(gb)) {
            gaininfo->qidx_table2[i] = get_bits(gb,4) - 7;  //convert to signed
        } else {
            gaininfo->qidx_table2[i] = -1;
        }
    }
}

/**
 * Create the quant index table needed for the envelope.
 *
 * @param q                 pointer to the COOKContext
 * @param quant_index_table pointer to the array
 */

static void decode_envelope(COOKContext *q, int* quant_index_table) {
    int i,j, vlc_index;
    int bitbias;

    bitbias = get_bits_count(&q->gb);
    quant_index_table[0]= get_bits(&q->gb,6) - 6;       //This is used later in categorize

    for (i=1 ; i < q->total_subbands ; i++){
        vlc_index=i;
        if (i >= q->js_subband_start * 2) {
            vlc_index-=q->js_subband_start;
        } else {
            vlc_index/=2;
            if(vlc_index < 1) vlc_index = 1;
        }
        if (vlc_index>13) vlc_index = 13;           //the VLC tables >13 are identical to No. 13

        j = get_vlc2(&q->gb, q->envelope_quant_index[vlc_index-1].table,
                     q->envelope_quant_index[vlc_index-1].bits,2);
        quant_index_table[i] = quant_index_table[i-1] + j - 12;    //differential encoding
    }
}

/**
 * Create the quant value table.
 *
 * @param q                 pointer to the COOKContext
 * @param quant_value_table pointer to the array
 */

static void inline dequant_envelope(COOKContext *q, int* quant_index_table,
                                    float* quant_value_table){

    int i;
    for(i=0 ; i < q->total_subbands ; i++){
        quant_value_table[i] = q->rootpow2tab[quant_index_table[i]+63];
    }
}

/**
 * Calculate the category and category_index vector.
 *
 * @param q                     pointer to the COOKContext
 * @param quant_index_table     pointer to the array
 * @param category              pointer to the category array
 * @param category_index        pointer to the category_index array
 */

static void categorize(COOKContext *q, int* quant_index_table,
                       int* category, int* category_index){
    int exp_idx, bias, tmpbias, bits_left, num_bits, index, v, i, j;
    int exp_index2[102];
    int exp_index1[102];

    int tmp_categorize_array1[128];
    int tmp_categorize_array1_idx=0;
    int tmp_categorize_array2[128];
    int tmp_categorize_array2_idx=0;
    int category_index_size=0;

    bits_left =  q->bits_per_subpacket - get_bits_count(&q->gb);

    if(bits_left > q->samples_per_channel) {
        bits_left = q->samples_per_channel +
                    ((bits_left - q->samples_per_channel)*5)/8;
        //av_log(NULL, AV_LOG_ERROR, "bits_left = %d\n",bits_left);
    }

    memset(&exp_index1,0,102*sizeof(int));
    memset(&exp_index2,0,102*sizeof(int));
    memset(&tmp_categorize_array1,0,128*sizeof(int));
    memset(&tmp_categorize_array2,0,128*sizeof(int));

    bias=-32;

    /* Estimate bias. */
    for (i=32 ; i>0 ; i=i/2){
        num_bits = 0;
        index = 0;
        for (j=q->total_subbands ; j>0 ; j--){
            exp_idx = (i - quant_index_table[index] + bias) / 2;
            if (exp_idx<0){
                exp_idx=0;
            } else if(exp_idx >7) {
                exp_idx=7;
            }
            index++;
            num_bits+=expbits_tab[exp_idx];
        }
        if(num_bits >= bits_left - 32){
            bias+=i;
        }
    }

    /* Calculate total number of bits. */
    num_bits=0;
    for (i=0 ; i<q->total_subbands ; i++) {
        exp_idx = (bias - quant_index_table[i]) / 2;
        if (exp_idx<0) {
            exp_idx=0;
        } else if(exp_idx >7) {
            exp_idx=7;
        }
        num_bits += expbits_tab[exp_idx];
        exp_index1[i] = exp_idx;
        exp_index2[i] = exp_idx;
    }
    tmpbias = bias = num_bits;

    for (j = 1 ; j < q->numvector_size ; j++) {
        if (tmpbias + bias > 2*bits_left) {  /* ---> */
            int max = -999999;
            index=-1;
            for (i=0 ; i<q->total_subbands ; i++){
                if (exp_index1[i] < 7) {
                    v = (-2*exp_index1[i]) - quant_index_table[i] - 32;
                    if ( v >= max) {
                        max = v;
                        index = i;
                    }
                }
            }
            if(index==-1)break;
            tmp_categorize_array1[tmp_categorize_array1_idx++] = index;
            tmpbias -= expbits_tab[exp_index1[index]] -
                       expbits_tab[exp_index1[index]+1];
            ++exp_index1[index];
        } else {  /* <--- */
            int min = 999999;
            index=-1;
            for (i=0 ; i<q->total_subbands ; i++){
                if(exp_index2[i] > 0){
                    v = (-2*exp_index2[i])-quant_index_table[i];
                    if ( v < min) {
                        min = v;
                        index = i;
                    }
                }
            }
            if(index == -1)break;
            tmp_categorize_array2[tmp_categorize_array2_idx++] = index;
            tmpbias -= expbits_tab[exp_index2[index]] -
                       expbits_tab[exp_index2[index]-1];
            --exp_index2[index];
        }
    }

    for(i=0 ; i<q->total_subbands ; i++)
        category[i] = exp_index2[i];

    /* Concatenate the two arrays. */
    for(i=tmp_categorize_array2_idx-1 ; i >= 0; i--)
        category_index[category_index_size++] =  tmp_categorize_array2[i];

    for(i=0;i<tmp_categorize_array1_idx;i++)
        category_index[category_index_size++ ] =  tmp_categorize_array1[i];

    /* FIXME: mc_sich_ra8_20.rm triggers this, not sure with what we
       should fill the remaining bytes. */
    for(i=category_index_size;i<q->numvector_size;i++)
        category_index[i]=0;

}


/**
 * Expand the category vector.
 *
 * @param q                     pointer to the COOKContext
 * @param category              pointer to the category array
 * @param category_index        pointer to the category_index array
 */

static void inline expand_category(COOKContext *q, int* category,
                                   int* category_index){
    int i;
    for(i=0 ; i<q->num_vectors ; i++){
        ++category[category_index[i]];
    }
}

/**
 * The real requantization of the mltcoefs
 *
 * @param q                     pointer to the COOKContext
 * @param index                 index
 * @param band                  current subband
 * @param quant_value_table     pointer to the array
 * @param subband_coef_index    array of indexes to quant_centroid_tab
 * @param subband_coef_noise    use random noise instead of predetermined value
 * @param mlt_buffer            pointer to the mlt buffer
 */


static void scalar_dequant(COOKContext *q, int index, int band,
                           float* quant_value_table, int* subband_coef_index,
                           int* subband_coef_noise, float* mlt_buffer){
    int i;
    float f1;

    for(i=0 ; i<SUBBAND_SIZE ; i++) {
        if (subband_coef_index[i]) {
            if (subband_coef_noise[i]) {
                f1 = -quant_centroid_tab[index][subband_coef_index[i]];
            } else {
                f1 = quant_centroid_tab[index][subband_coef_index[i]];
            }
        } else {
            /* noise coding if subband_coef_noise[i] == 0 */
            q->random_state = q->random_state * 214013 + 2531011;    //typical RNG numbers
            f1 = randsign[(q->random_state/0x1000000)&1] * dither_tab[index]; //>>31
        }
        mlt_buffer[band*20+ i] = f1 * quant_value_table[band];
    }
}
/**
 * Unpack the subband_coef_index and subband_coef_noise vectors.
 *
 * @param q                     pointer to the COOKContext
 * @param category              pointer to the category array
 * @param subband_coef_index    array of indexes to quant_centroid_tab
 * @param subband_coef_noise    use random noise instead of predetermined value
 */

static int unpack_SQVH(COOKContext *q, int category, int* subband_coef_index,
                       int* subband_coef_noise) {
    int i,j;
    int vlc, vd ,tmp, result;
    int ub;
    int cb;

    vd = vd_tab[category];
    result = 0;
    for(i=0 ; i<vpr_tab[category] ; i++){
        ub = get_bits_count(&q->gb);
        vlc = get_vlc2(&q->gb, q->sqvh[category].table, q->sqvh[category].bits, 3);
        cb = get_bits_count(&q->gb);
        if (q->bits_per_subpacket < get_bits_count(&q->gb)){
            vlc = 0;
            result = 1;
        }
        for(j=vd-1 ; j>=0 ; j--){
            tmp = (vlc * invradix_tab[category])/0x100000;
            subband_coef_index[vd*i+j] = vlc - tmp * (kmax_tab[category]+1);
            vlc = tmp;
        }
        for(j=0 ; j<vd ; j++){
            if (subband_coef_index[i*vd + j]) {
                if(get_bits_count(&q->gb) < q->bits_per_subpacket){
                    subband_coef_noise[i*vd+j] = get_bits1(&q->gb);
                } else {
                    result=1;
                    subband_coef_noise[i*vd+j]=0;
                }
            } else {
                subband_coef_noise[i*vd+j]=0;
            }
        }
    }
    return result;
}


/**
 * Fill the mlt_buffer with mlt coefficients.
 *
 * @param q                 pointer to the COOKContext
 * @param category          pointer to the category array
 * @param quant_value_table pointer to the array
 * @param mlt_buffer        pointer to mlt coefficients
 */


static void decode_vectors(COOKContext* q, int* category,
                           float* quant_value_table, float* mlt_buffer){
    /* A zero in this table means that the subband coefficient is
       random noise coded. */
    int subband_coef_noise[SUBBAND_SIZE];
    /* A zero in this table means that the subband coefficient is a
       positive multiplicator. */
    int subband_coef_index[SUBBAND_SIZE];
    int band, j;
    int index=0;

    for(band=0 ; band<q->total_subbands ; band++){
        index = category[band];
        if(category[band] < 7){
            if(unpack_SQVH(q, category[band], subband_coef_index, subband_coef_noise)){
                index=7;
                for(j=0 ; j<q->total_subbands ; j++) category[band+j]=7;
            }
        }
        if(index==7) {
            memset(subband_coef_index, 0, sizeof(subband_coef_index));
            memset(subband_coef_noise, 0, sizeof(subband_coef_noise));
        }
        scalar_dequant(q, index, band, quant_value_table, subband_coef_index,
                       subband_coef_noise, mlt_buffer);
    }

    if(q->total_subbands*SUBBAND_SIZE >= q->samples_per_channel){
        return;
    }
}


/**
 * function for decoding mono data
 *
 * @param q                 pointer to the COOKContext
 * @param mlt_buffer1       pointer to left channel mlt coefficients
 * @param mlt_buffer2       pointer to right channel mlt coefficients
 */

static void mono_decode(COOKContext *q, float* mlt_buffer) {

    int category_index[128];
    float quant_value_table[102];
    int quant_index_table[102];
    int category[128];

    memset(&category, 0, 128*sizeof(int));
    memset(&quant_value_table, 0, 102*sizeof(int));
    memset(&category_index, 0, 128*sizeof(int));

    decode_envelope(q, quant_index_table);
    q->num_vectors = get_bits(&q->gb,q->log2_numvector_size);
    dequant_envelope(q, quant_index_table, quant_value_table);
    categorize(q, quant_index_table, category, category_index);
    expand_category(q, category, category_index);
    decode_vectors(q, category, quant_value_table, mlt_buffer);
}


/**
 * The modulated lapped transform, this takes transform coefficients
 * and transforms them into timedomain samples. This is done through
 * an FFT-based algorithm with pre- and postrotation steps.
 * A window and reorder step is also included.
 *
 * @param q                 pointer to the COOKContext
 * @param inbuffer          pointer to the mltcoefficients
 * @param outbuffer         pointer to the timedomain buffer
 * @param mlt_tmp           pointer to temporary storage space
 */

static void cook_imlt(COOKContext *q, float* inbuffer, float* outbuffer,
                      float* mlt_tmp){
    int i;

    /* prerotation */
    for(i=0 ; i<q->mlt_size ; i+=2){
        outbuffer[i] = (q->mlt_presin[i/2] * inbuffer[q->mlt_size-1-i]) +
                       (q->mlt_precos[i/2] * inbuffer[i]);
        outbuffer[i+1] = (q->mlt_precos[i/2] * inbuffer[q->mlt_size-1-i]) -
                         (q->mlt_presin[i/2] * inbuffer[i]);
    }

    /* FFT */
    ff_fft_permute(&q->fft_ctx, (FFTComplex *) outbuffer);
    ff_fft_calc (&q->fft_ctx, (FFTComplex *) outbuffer);

    /* postrotation */
    for(i=0 ; i<q->mlt_size ; i+=2){
        mlt_tmp[i] =               (q->mlt_postcos[(q->mlt_size-1-i)/2] * outbuffer[i+1]) +
                                   (q->mlt_postcos[i/2] * outbuffer[i]);
        mlt_tmp[q->mlt_size-1-i] = (q->mlt_postcos[(q->mlt_size-1-i)/2] * outbuffer[i]) -
                                   (q->mlt_postcos[i/2] * outbuffer[i+1]);
    }

    /* window and reorder */
    for(i=0 ; i<q->mlt_size/2 ; i++){
        outbuffer[i] = mlt_tmp[q->mlt_size/2-1-i] * q->mlt_window[i];
        outbuffer[q->mlt_size-1-i]= mlt_tmp[q->mlt_size/2-1-i] *
                                    q->mlt_window[q->mlt_size-1-i];
        outbuffer[q->mlt_size+i]= mlt_tmp[q->mlt_size/2+i] *
                                  q->mlt_window[q->mlt_size-1-i];
        outbuffer[2*q->mlt_size-1-i]= -(mlt_tmp[q->mlt_size/2+i] *
                                      q->mlt_window[i]);
    }
}


/**
 * the actual requantization of the timedomain samples
 *
 * @param q                 pointer to the COOKContext
 * @param buffer            pointer to the timedomain buffer
 * @param gain_index        index for the block multiplier
 * @param gain_index_next   index for the next block multiplier
 */

static void interpolate(COOKContext *q, float* buffer,
                        int gain_index, int gain_index_next){
    int i;
    float fc1, fc2;
    fc1 = q->pow2tab[gain_index+63];

    if(gain_index == gain_index_next){              //static gain
        for(i=0 ; i<q->gain_size_factor ; i++){
            buffer[i]*=fc1;
        }
        return;
    } else {                                        //smooth gain
        fc2 = q->gain_table[11 + (gain_index_next-gain_index)];
        for(i=0 ; i<q->gain_size_factor ; i++){
            buffer[i]*=fc1;
            fc1*=fc2;
        }
        return;
    }
}

/**
 * timedomain requantization of the timedomain samples
 *
 * @param q                 pointer to the COOKContext
 * @param buffer            pointer to the timedomain buffer
 * @param gain_now          current gain structure
 * @param gain_previous     previous gain structure
 */

static void gain_window(COOKContext *q, float* buffer, COOKgain* gain_now,
                        COOKgain* gain_previous){
    int i, index;
    int gain_index[9];
    int tmp_gain_index;

    gain_index[8]=0;
    index = gain_previous->size;
    for (i=7 ; i>=0 ; i--) {
        if(index && gain_previous->qidx_table1[index-1]==i) {
            gain_index[i] = gain_previous->qidx_table2[index-1];
            index--;
        } else {
            gain_index[i]=gain_index[i+1];
        }
    }
    /* This is applied to the to be previous data buffer. */
    for(i=0;i<8;i++){
        interpolate(q, &buffer[q->samples_per_channel+q->gain_size_factor*i],
                    gain_index[i], gain_index[i+1]);
    }

    tmp_gain_index = gain_index[0];
    index = gain_now->size;
    for (i=7 ; i>=0 ; i--) {
        if(index && gain_now->qidx_table1[index-1]==i) {
            gain_index[i]= gain_now->qidx_table2[index-1];
            index--;
        } else {
            gain_index[i]=gain_index[i+1];
        }
    }

    /* This is applied to the to be current block. */
    for(i=0;i<8;i++){
        interpolate(q, &buffer[i*q->gain_size_factor],
                    tmp_gain_index+gain_index[i],
                    tmp_gain_index+gain_index[i+1]);
    }
}


/**
 * mlt overlapping and buffer management
 *
 * @param q                 pointer to the COOKContext
 * @param buffer            pointer to the timedomain buffer
 * @param gain_now          current gain structure
 * @param gain_previous     previous gain structure
 * @param previous_buffer   pointer to the previous buffer to be used for overlapping
 *
 */

static void gain_compensate(COOKContext *q, float* buffer, COOKgain* gain_now,
                            COOKgain* gain_previous, float* previous_buffer) {
    int i;
    if((gain_now->size  || gain_previous->size)) {
        gain_window(q, buffer, gain_now, gain_previous);
    }

    /* Overlap with the previous block. */
    for(i=0 ; i<q->samples_per_channel ; i++) buffer[i]+=previous_buffer[i];

    /* Save away the current to be previous block. */
    memcpy(previous_buffer, buffer+q->samples_per_channel,
           sizeof(float)*q->samples_per_channel);
}


/**
 * function for getting the jointstereo coupling information
 *
 * @param q                 pointer to the COOKContext
 * @param decouple_tab      decoupling array
 *
 */

static void decouple_info(COOKContext *q, int* decouple_tab){
    int length, i;

    if(get_bits1(&q->gb)) {
        if(cplband[q->js_subband_start] > cplband[q->subbands-1]) return;

        length = cplband[q->subbands-1] - cplband[q->js_subband_start] + 1;
        for (i=0 ; i<length ; i++) {
            decouple_tab[cplband[q->js_subband_start] + i] = get_vlc2(&q->gb, q->ccpl.table, q->ccpl.bits, 2);
        }
        return;
    }

    if(cplband[q->js_subband_start] > cplband[q->subbands-1]) return;

    length = cplband[q->subbands-1] - cplband[q->js_subband_start] + 1;
    for (i=0 ; i<length ; i++) {
       decouple_tab[cplband[q->js_subband_start] + i] = get_bits(&q->gb, q->js_vlc_bits);
    }
    return;
}


/**
 * function for decoding joint stereo data
 *
 * @param q                 pointer to the COOKContext
 * @param mlt_buffer1       pointer to left channel mlt coefficients
 * @param mlt_buffer2       pointer to right channel mlt coefficients
 */

static void joint_decode(COOKContext *q, float* mlt_buffer1,
                         float* mlt_buffer2) {
    int i,j;
    int decouple_tab[SUBBAND_SIZE];
    float decode_buffer[1060];
    int idx, cpl_tmp,tmp_idx;
    float f1,f2;
    float* cplscale;

    memset(decouple_tab, 0, sizeof(decouple_tab));
    memset(decode_buffer, 0, sizeof(decode_buffer));

    /* Make sure the buffers are zeroed out. */
    memset(mlt_buffer1,0, 1024*sizeof(float));
    memset(mlt_buffer2,0, 1024*sizeof(float));
    decouple_info(q, decouple_tab);
    mono_decode(q, decode_buffer);

    /* The two channels are stored interleaved in decode_buffer. */
    for (i=0 ; i<q->js_subband_start ; i++) {
        for (j=0 ; j<SUBBAND_SIZE ; j++) {
            mlt_buffer1[i*20+j] = decode_buffer[i*40+j];
            mlt_buffer2[i*20+j] = decode_buffer[i*40+20+j];
        }
    }

    /* When we reach js_subband_start (the higher frequencies)
       the coefficients are stored in a coupling scheme. */
    idx = (1 << q->js_vlc_bits) - 1;
    for (i=q->js_subband_start ; i<q->subbands ; i++) {
        cpl_tmp = cplband[i];
        idx -=decouple_tab[cpl_tmp];
        cplscale = (float*)cplscales[q->js_vlc_bits-2];  //choose decoupler table
        f1 = cplscale[decouple_tab[cpl_tmp]];
        f2 = cplscale[idx-1];
        for (j=0 ; j<SUBBAND_SIZE ; j++) {
            tmp_idx = ((q->js_subband_start + i)*20)+j;
            mlt_buffer1[20*i + j] = f1 * decode_buffer[tmp_idx];
            mlt_buffer2[20*i + j] = f2 * decode_buffer[tmp_idx];
        }
        idx = (1 << q->js_vlc_bits) - 1;
    }
}

/**
 * Cook subpacket decoding. This function returns one decoded subpacket,
 * usually 1024 samples per channel.
 *
 * @param q                 pointer to the COOKContext
 * @param inbuffer          pointer to the inbuffer
 * @param sub_packet_size   subpacket size
 * @param outbuffer         pointer to the outbuffer
 */


static int decode_subpacket(COOKContext *q, uint8_t *inbuffer,
                            int sub_packet_size, int16_t *outbuffer) {
    int i,j;
    int value;
    float* tmp_ptr;

    /* packet dump */
//    for (i=0 ; i<sub_packet_size ; i++) {
//        av_log(NULL, AV_LOG_ERROR, "%02x", inbuffer[i]);
//    }
//    av_log(NULL, AV_LOG_ERROR, "\n");

    decode_bytes(inbuffer, q->decoded_bytes_buffer, sub_packet_size);
    init_get_bits(&q->gb, q->decoded_bytes_buffer, sub_packet_size*8);
    decode_gain_info(&q->gb, &q->gain_current);

    if(q->nb_channels==2 && q->joint_stereo==1){
        joint_decode(q, q->decode_buf_ptr[0], q->decode_buf_ptr[2]);

        /* Swap buffer pointers. */
        tmp_ptr = q->decode_buf_ptr[1];
        q->decode_buf_ptr[1] = q->decode_buf_ptr[0];
        q->decode_buf_ptr[0] = tmp_ptr;
        tmp_ptr = q->decode_buf_ptr[3];
        q->decode_buf_ptr[3] = q->decode_buf_ptr[2];
        q->decode_buf_ptr[2] = tmp_ptr;

        /* FIXME: Rethink the gainbuffer handling, maybe a rename?
           now/previous swap */
        q->gain_now_ptr = &q->gain_now;
        q->gain_previous_ptr = &q->gain_previous;
        for (i=0 ; i<q->nb_channels ; i++){

            cook_imlt(q, q->decode_buf_ptr[i*2], q->mono_mdct_output, q->mlt_tmp);
            gain_compensate(q, q->mono_mdct_output, q->gain_now_ptr,
                            q->gain_previous_ptr, q->previous_buffer_ptr[0]);

            /* Swap out the previous buffer. */
            tmp_ptr = q->previous_buffer_ptr[0];
            q->previous_buffer_ptr[0] = q->previous_buffer_ptr[1];
            q->previous_buffer_ptr[1] = tmp_ptr;

            /* Clip and convert the floats to 16 bits. */
            for (j=0 ; j<q->samples_per_frame ; j++){
                value = lrintf(q->mono_mdct_output[j]);
                if(value < -32768) value = -32768;
                else if(value > 32767) value = 32767;
                outbuffer[2*j+i] = value;
            }
        }

        memcpy(&q->gain_now, &q->gain_previous, sizeof(COOKgain));
        memcpy(&q->gain_previous, &q->gain_current, sizeof(COOKgain));

    } else if (q->nb_channels==2 && q->joint_stereo==0) {
            /* channel 0 */
            mono_decode(q, q->decode_buf_ptr2[0]);

            tmp_ptr = q->decode_buf_ptr2[0];
            q->decode_buf_ptr2[0] = q->decode_buf_ptr2[1];
            q->decode_buf_ptr2[1] = tmp_ptr;

            memcpy(&q->gain_channel1[0], &q->gain_current ,sizeof(COOKgain));
            q->gain_now_ptr = &q->gain_channel1[0];
            q->gain_previous_ptr = &q->gain_channel1[1];

            cook_imlt(q, q->decode_buf_ptr2[0], q->mono_mdct_output,q->mlt_tmp);
            gain_compensate(q, q->mono_mdct_output, q->gain_now_ptr,
                            q->gain_previous_ptr, q->mono_previous_buffer1);

            memcpy(&q->gain_channel1[1], &q->gain_channel1[0],sizeof(COOKgain));


            for (j=0 ; j<q->samples_per_frame ; j++){
                value = lrintf(q->mono_mdct_output[j]);
                if(value < -32768) value = -32768;
                else if(value > 32767) value = 32767;
                outbuffer[2*j+1] = value;
            }

            /* channel 1 */
            //av_log(NULL,AV_LOG_ERROR,"bits = %d\n",get_bits_count(&q->gb));
            init_get_bits(&q->gb, q->decoded_bytes_buffer, sub_packet_size*8+q->bits_per_subpacket);

            q->gain_now_ptr = &q->gain_channel2[0];
            q->gain_previous_ptr = &q->gain_channel2[1];

            decode_gain_info(&q->gb, &q->gain_channel2[0]);
            mono_decode(q, q->decode_buf_ptr[0]);

            tmp_ptr = q->decode_buf_ptr[0];
            q->decode_buf_ptr[0] = q->decode_buf_ptr[1];
            q->decode_buf_ptr[1] = tmp_ptr;

            cook_imlt(q, q->decode_buf_ptr[0], q->mono_mdct_output,q->mlt_tmp);
            gain_compensate(q, q->mono_mdct_output, q->gain_now_ptr,
                            q->gain_previous_ptr, q->mono_previous_buffer2);

            /* Swap out the previous buffer. */
            tmp_ptr = q->previous_buffer_ptr[0];
            q->previous_buffer_ptr[0] = q->previous_buffer_ptr[1];
            q->previous_buffer_ptr[1] = tmp_ptr;

            memcpy(&q->gain_channel2[1], &q->gain_channel2[0] ,sizeof(COOKgain));

            for (j=0 ; j<q->samples_per_frame ; j++){
                value = lrintf(q->mono_mdct_output[j]);
                if(value < -32768) value = -32768;
                else if(value > 32767) value = 32767;
                outbuffer[2*j] = value;
            }

    } else {
        mono_decode(q, q->decode_buf_ptr[0]);

        /* Swap buffer pointers. */
        tmp_ptr = q->decode_buf_ptr[1];
        q->decode_buf_ptr[1] = q->decode_buf_ptr[0];
        q->decode_buf_ptr[0] = tmp_ptr;

        /* FIXME: Rethink the gainbuffer handling, maybe a rename?
           now/previous swap */
        q->gain_now_ptr = &q->gain_now;
        q->gain_previous_ptr = &q->gain_previous;

        cook_imlt(q, q->decode_buf_ptr[0], q->mono_mdct_output,q->mlt_tmp);
        gain_compensate(q, q->mono_mdct_output, q->gain_now_ptr,
                        q->gain_previous_ptr, q->mono_previous_buffer1);

        /* Clip and convert the floats to 16 bits */
        for (j=0 ; j<q->samples_per_frame ; j++){
            value = lrintf(q->mono_mdct_output[j]);
            if(value < -32768) value = -32768;
            else if(value > 32767) value = 32767;
            outbuffer[j] = value;
        }
        memcpy(&q->gain_now, &q->gain_previous, sizeof(COOKgain));
        memcpy(&q->gain_previous, &q->gain_current, sizeof(COOKgain));
    }
    return q->samples_per_frame * sizeof(int16_t);
}


/**
 * Cook frame decoding
 *
 * @param avctx     pointer to the AVCodecContext
 */

static int cook_decode_frame(AVCodecContext *avctx,
            void *data, int *data_size,
            uint8_t *buf, int buf_size) {
    COOKContext *q = avctx->priv_data;

    if (buf_size < avctx->block_align)
        return buf_size;

    *data_size = decode_subpacket(q, buf, avctx->block_align, data);

    return avctx->block_align;
}

#ifdef COOKDEBUG
static void dump_cook_context(COOKContext *q, COOKextradata *e)
{
    //int i=0;
#define PRINT(a,b) av_log(NULL,AV_LOG_ERROR," %s = %d\n", a, b);
    av_log(NULL,AV_LOG_ERROR,"COOKextradata\n");
    av_log(NULL,AV_LOG_ERROR,"cookversion=%x\n",e->cookversion);
    if (e->cookversion > MONO_COOK2) {
        PRINT("js_subband_start",e->js_subband_start);
        PRINT("js_vlc_bits",e->js_vlc_bits);
    }
    av_log(NULL,AV_LOG_ERROR,"COOKContext\n");
    PRINT("nb_channels",q->nb_channels);
    PRINT("bit_rate",q->bit_rate);
    PRINT("sample_rate",q->sample_rate);
    PRINT("samples_per_channel",q->samples_per_channel);
    PRINT("samples_per_frame",q->samples_per_frame);
    PRINT("subbands",q->subbands);
    PRINT("random_state",q->random_state);
    PRINT("mlt_size",q->mlt_size);
    PRINT("js_subband_start",q->js_subband_start);
    PRINT("log2_numvector_size",q->log2_numvector_size);
    PRINT("numvector_size",q->numvector_size);
    PRINT("total_subbands",q->total_subbands);
}
#endif

/**
 * Cook initialization
 *
 * @param avctx     pointer to the AVCodecContext
 */

static int cook_decode_init(AVCodecContext *avctx)
{
    COOKextradata *e = avctx->extradata;
    COOKContext *q = avctx->priv_data;

    /* Take care of the codec specific extradata. */
    if (avctx->extradata_size <= 0) {
        av_log(NULL,AV_LOG_ERROR,"Necessary extradata missing!\n");
        return -1;
    } else {
        /* 8 for mono, 16 for stereo, ? for multichannel
           Swap to right endianness so we don't need to care later on. */
        av_log(NULL,AV_LOG_DEBUG,"codecdata_length=%d\n",avctx->extradata_size);
        if (avctx->extradata_size >= 8){
            e->cookversion = be2me_32(e->cookversion);
            e->samples_per_frame = be2me_16(e->samples_per_frame);
            e->subbands = be2me_16(e->subbands);
        }
        if (avctx->extradata_size >= 16){
            e->js_subband_start = be2me_16(e->js_subband_start);
            e->js_vlc_bits = be2me_16(e->js_vlc_bits);
        }
    }

    /* Take data from the AVCodecContext (RM container). */
    q->sample_rate = avctx->sample_rate;
    q->nb_channels = avctx->channels;
    q->bit_rate = avctx->bit_rate;

    /* Initialize state. */
    q->random_state = 1;

    /* Initialize extradata related variables. */
    q->samples_per_channel = e->samples_per_frame / q->nb_channels;
    q->samples_per_frame = e->samples_per_frame;
    q->subbands = e->subbands;
    q->bits_per_subpacket = avctx->block_align * 8;

    /* Initialize default data states. */
    q->js_subband_start = 0;
    q->log2_numvector_size = 5;
    q->total_subbands = q->subbands;

    /* Initialize version-dependent variables */
    av_log(NULL,AV_LOG_DEBUG,"e->cookversion=%x\n",e->cookversion);
    switch (e->cookversion) {
        case MONO_COOK1:
            if (q->nb_channels != 1) {
                av_log(NULL,AV_LOG_ERROR,"Container channels != 1, report sample!\n");
                return -1;
            }
            av_log(NULL,AV_LOG_DEBUG,"MONO_COOK1\n");
            break;
        case MONO_COOK2:
            if (q->nb_channels != 1) {
                q->joint_stereo = 0;
                q->bits_per_subpacket = q->bits_per_subpacket/2;
            }
            av_log(NULL,AV_LOG_DEBUG,"MONO_COOK2\n");
            break;
        case JOINT_STEREO:
            if (q->nb_channels != 2) {
                av_log(NULL,AV_LOG_ERROR,"Container channels != 2, report sample!\n");
                return -1;
            }
            av_log(NULL,AV_LOG_DEBUG,"JOINT_STEREO\n");
            if (avctx->extradata_size >= 16){
                q->total_subbands = q->subbands + e->js_subband_start;
                q->js_subband_start = e->js_subband_start;
                q->joint_stereo = 1;
                q->js_vlc_bits = e->js_vlc_bits;
            }
            if (q->samples_per_channel > 256) {
                q->log2_numvector_size  = 6;
            }
            if (q->samples_per_channel > 512) {
                q->log2_numvector_size  = 7;
            }
            break;
        case MC_COOK:
            av_log(NULL,AV_LOG_ERROR,"MC_COOK not supported!\n");
            return -1;
            break;
        default:
            av_log(NULL,AV_LOG_ERROR,"Unknown Cook version, report sample!\n");
            return -1;
            break;
    }

    /* Initialize variable relations */
    q->mlt_size = q->samples_per_channel;
    q->numvector_size = (1 << q->log2_numvector_size);

    /* Generate tables */
    init_rootpow2table(q);
    init_pow2table(q);
    init_gain_table(q);

    if (init_cook_vlc_tables(q) != 0)
        return -1;


    if(avctx->block_align >= UINT_MAX/2)
        return -1;

    /* Pad the databuffer with FF_INPUT_BUFFER_PADDING_SIZE,
       this is for the bitstreamreader. */
    if ((q->decoded_bytes_buffer = av_mallocz((avctx->block_align+(4-avctx->block_align%4) + FF_INPUT_BUFFER_PADDING_SIZE)*sizeof(uint8_t)))  == NULL)
        return -1;

    q->decode_buf_ptr[0] = q->decode_buffer_1;
    q->decode_buf_ptr[1] = q->decode_buffer_2;
    q->decode_buf_ptr[2] = q->decode_buffer_3;
    q->decode_buf_ptr[3] = q->decode_buffer_4;

    q->decode_buf_ptr2[0] = q->decode_buffer_3;
    q->decode_buf_ptr2[1] = q->decode_buffer_4;

    q->previous_buffer_ptr[0] = q->mono_previous_buffer1;
    q->previous_buffer_ptr[1] = q->mono_previous_buffer2;

    /* Initialize transform. */
    if ( init_cook_mlt(q) == 0 )
        return -1;

    /* Try to catch some obviously faulty streams, othervise it might be exploitable */
    if (q->total_subbands > 53) {
        av_log(NULL,AV_LOG_ERROR,"total_subbands > 53, report sample!\n");
        return -1;
    }
    if (q->subbands > 50) {
        av_log(NULL,AV_LOG_ERROR,"subbands > 50, report sample!\n");
        return -1;
    }
    if ((q->samples_per_channel == 256) || (q->samples_per_channel == 512) || (q->samples_per_channel == 1024)) {
    } else {
        av_log(NULL,AV_LOG_ERROR,"unknown amount of samples_per_channel = %d, report sample!\n",q->samples_per_channel);
        return -1;
    }

#ifdef COOKDEBUG
    dump_cook_context(q,e);
#endif
    return 0;
}


AVCodec cook_decoder =
{
    .name = "cook",
    .type = CODEC_TYPE_AUDIO,
    .id = CODEC_ID_COOK,
    .priv_data_size = sizeof(COOKContext),
    .init = cook_decode_init,
    .close = cook_decode_close,
    .decode = cook_decode_frame,
};
