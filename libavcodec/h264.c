/*
 * H.26L/H.264/AVC/JVT/14496-10/... encoder/decoder
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
 
/**
 * @file h264.c
 * H.264 / AVC / MPEG4 part10 codec.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "common.h"
#include "dsputil.h"
#include "avcodec.h"
#include "mpegvideo.h"
#include "h264data.h"
#include "golomb.h"

#include "cabac.h"

#undef NDEBUG
#include <assert.h>

#define interlaced_dct interlaced_dct_is_a_bad_name
#define mb_intra mb_intra_isnt_initalized_see_mb_type

#define LUMA_DC_BLOCK_INDEX   25
#define CHROMA_DC_BLOCK_INDEX 26

#define CHROMA_DC_COEFF_TOKEN_VLC_BITS 8
#define COEFF_TOKEN_VLC_BITS           8
#define TOTAL_ZEROS_VLC_BITS           9
#define CHROMA_DC_TOTAL_ZEROS_VLC_BITS 3
#define RUN_VLC_BITS                   3
#define RUN7_VLC_BITS                  6

#define MAX_SPS_COUNT 32
#define MAX_PPS_COUNT 256

#define MAX_MMCO_COUNT 66

/**
 * Sequence parameter set
 */
typedef struct SPS{
    
    int profile_idc;
    int level_idc;
    int log2_max_frame_num;            ///< log2_max_frame_num_minus4 + 4
    int poc_type;                      ///< pic_order_cnt_type
    int log2_max_poc_lsb;              ///< log2_max_pic_order_cnt_lsb_minus4
    int delta_pic_order_always_zero_flag;
    int offset_for_non_ref_pic;
    int offset_for_top_to_bottom_field;
    int poc_cycle_length;              ///< num_ref_frames_in_pic_order_cnt_cycle
    int ref_frame_count;               ///< num_ref_frames
    int gaps_in_frame_num_allowed_flag;
    int mb_width;                      ///< frame_width_in_mbs_minus1 + 1
    int mb_height;                     ///< frame_height_in_mbs_minus1 + 1
    int frame_mbs_only_flag;
    int mb_aff;                        ///<mb_adaptive_frame_field_flag
    int direct_8x8_inference_flag;
    int crop;                   ///< frame_cropping_flag
    int crop_left;              ///< frame_cropping_rect_left_offset
    int crop_right;             ///< frame_cropping_rect_right_offset
    int crop_top;               ///< frame_cropping_rect_top_offset
    int crop_bottom;            ///< frame_cropping_rect_bottom_offset
    int vui_parameters_present_flag;
    AVRational sar;
    int timing_info_present_flag;
    uint32_t num_units_in_tick;
    uint32_t time_scale;
    int fixed_frame_rate_flag;
    short offset_for_ref_frame[256]; //FIXME dyn aloc?
}SPS;

/**
 * Picture parameter set
 */
typedef struct PPS{
    int sps_id;
    int cabac;                  ///< entropy_coding_mode_flag
    int pic_order_present;      ///< pic_order_present_flag
    int slice_group_count;      ///< num_slice_groups_minus1 + 1
    int mb_slice_group_map_type;
    int ref_count[2];           ///< num_ref_idx_l0/1_active_minus1 + 1
    int weighted_pred;          ///< weighted_pred_flag
    int weighted_bipred_idc;
    int init_qp;                ///< pic_init_qp_minus26 + 26
    int init_qs;                ///< pic_init_qs_minus26 + 26
    int chroma_qp_index_offset;
    int deblocking_filter_parameters_present; ///< deblocking_filter_parameters_present_flag
    int constrained_intra_pred; ///< constrained_intra_pred_flag
    int redundant_pic_cnt_present; ///< redundant_pic_cnt_present_flag
}PPS;

/**
 * Memory management control operation opcode.
 */
typedef enum MMCOOpcode{
    MMCO_END=0,
    MMCO_SHORT2UNUSED,
    MMCO_LONG2UNUSED,
    MMCO_SHORT2LONG,
    MMCO_SET_MAX_LONG,
    MMCO_RESET, 
    MMCO_LONG,
} MMCOOpcode;

/**
 * Memory management control operation.
 */
typedef struct MMCO{
    MMCOOpcode opcode;
    int short_frame_num;
    int long_index;
} MMCO;

/**
 * H264Context
 */
typedef struct H264Context{
    MpegEncContext s;
    int nal_ref_idc;	
    int nal_unit_type;
#define NAL_SLICE		1
#define NAL_DPA			2
#define NAL_DPB			3
#define NAL_DPC			4
#define NAL_IDR_SLICE		5
#define NAL_SEI			6
#define NAL_SPS			7
#define NAL_PPS			8
#define NAL_PICTURE_DELIMITER	9
#define NAL_FILTER_DATA		10
    uint8_t *rbsp_buffer;
    int rbsp_buffer_size;

    /**
      * Used to parse AVC variant of h264
      */
    int is_avc; ///< this flag is != 0 if codec is avc1
    int got_avcC; ///< flag used to parse avcC data only once
    int nal_length_size; ///< Number of bytes used for nal length (1, 2 or 4)

    int chroma_qp; //QPc

    int prev_mb_skiped; //FIXME remove (IMHO not used)

    //prediction stuff
    int chroma_pred_mode;
    int intra16x16_pred_mode;
    
    int8_t intra4x4_pred_mode_cache[5*8];
    int8_t (*intra4x4_pred_mode)[8];
    void (*pred4x4  [9+3])(uint8_t *src, uint8_t *topright, int stride);//FIXME move to dsp?
    void (*pred8x8  [4+3])(uint8_t *src, int stride);
    void (*pred16x16[4+3])(uint8_t *src, int stride);
    unsigned int topleft_samples_available;
    unsigned int top_samples_available;
    unsigned int topright_samples_available;
    unsigned int left_samples_available;
    uint8_t (*top_border)[16+2*8];
    uint8_t left_border[17+2*9];

    /**
     * non zero coeff count cache.
     * is 64 if not available.
     */
    uint8_t non_zero_count_cache[6*8];
    uint8_t (*non_zero_count)[16];

    /**
     * Motion vector cache.
     */
    int16_t mv_cache[2][5*8][2];
    int8_t ref_cache[2][5*8];
#define LIST_NOT_USED -1 //FIXME rename?
#define PART_NOT_AVAILABLE -2
    
    /**
     * is 1 if the specific list MV&references are set to 0,0,-2.
     */
    int mv_cache_clean[2];

    int block_offset[16+8];
    int chroma_subblock_offset[16]; //FIXME remove
    
    uint16_t *mb2b_xy; //FIXME are these 4 a good idea?
    uint16_t *mb2b8_xy;
    int b_stride; //FIXME use s->b4_stride
    int b8_stride;

    int halfpel_flag;
    int thirdpel_flag;

    int unknown_svq3_flag;
    int next_slice_index;

    SPS sps_buffer[MAX_SPS_COUNT];
    SPS sps; ///< current sps
    
    PPS pps_buffer[MAX_PPS_COUNT];
    /**
     * current pps
     */
    PPS pps; //FIXME move tp Picture perhaps? (->no) do we need that?

    int slice_num;
    uint8_t *slice_table_base;
    uint8_t *slice_table;      ///< slice_table_base + mb_stride + 1
    int slice_type;
    int slice_type_fixed;
    
    //interlacing specific flags
    int mb_field_decoding_flag;
    
    int sub_mb_type[4];
    
    //POC stuff
    int poc_lsb;
    int poc_msb;
    int delta_poc_bottom;
    int delta_poc[2];
    int frame_num;
    int prev_poc_msb;             ///< poc_msb of the last reference pic for POC type 0
    int prev_poc_lsb;             ///< poc_lsb of the last reference pic for POC type 0
    int frame_num_offset;         ///< for POC type 2
    int prev_frame_num_offset;    ///< for POC type 2
    int prev_frame_num;           ///< frame_num of the last pic for POC type 1/2

    /**
     * frame_num for frames or 2*frame_num for field pics.
     */
    int curr_pic_num;
    
    /**
     * max_frame_num or 2*max_frame_num for field pics.
     */
    int max_pic_num;

    //Weighted pred stuff
    int use_weight;
    int use_weight_chroma;
    int luma_log2_weight_denom;
    int chroma_log2_weight_denom;
    int luma_weight[2][16];
    int luma_offset[2][16];
    int chroma_weight[2][16][2];
    int chroma_offset[2][16][2];
    int implicit_weight[16][16];
   
    //deblock
    int deblocking_filter;         ///< disable_deblocking_filter_idc with 1<->0 
    int slice_alpha_c0_offset;
    int slice_beta_offset;
     
    int redundant_pic_count;
    
    int direct_spatial_mv_pred;
    int dist_scale_factor[16];

    /**
     * num_ref_idx_l0/1_active_minus1 + 1
     */
    int ref_count[2];// FIXME split for AFF
    Picture *short_ref[16];
    Picture *long_ref[16];
    Picture default_ref_list[2][32];
    Picture ref_list[2][32]; //FIXME size?
    Picture field_ref_list[2][32]; //FIXME size?
    Picture *delayed_pic[16]; //FIXME size?
    
    /**
     * memory management control operations buffer.
     */
    MMCO mmco[MAX_MMCO_COUNT];
    int mmco_index;
    
    int long_ref_count;  ///< number of actual long term references
    int short_ref_count; ///< number of actual short term references
    
    //data partitioning
    GetBitContext intra_gb;
    GetBitContext inter_gb;
    GetBitContext *intra_gb_ptr;
    GetBitContext *inter_gb_ptr;
    
    DCTELEM mb[16*24] __align8;

    /**
     * Cabac
     */
    CABACContext cabac;
    uint8_t      cabac_state[399];
    int          cabac_init_idc;

    /* 0x100 -> non null luma_dc, 0x80/0x40 -> non null chroma_dc (cb/cr), 0x?0 -> chroma_cbp(0,1,2), 0x0? luma_cbp */
    uint16_t     *cbp_table;
    int top_cbp;
    int left_cbp;
    /* chroma_pred_mode for i4x4 or i16x16, else 0 */
    uint8_t     *chroma_pred_mode_table;
    int         last_qscale_diff;
    int16_t     (*mvd_table[2])[2];
    int16_t     mvd_cache[2][5*8][2];
    uint8_t     *direct_table;
    uint8_t     direct_cache[5*8];

}H264Context;

static VLC coeff_token_vlc[4];
static VLC chroma_dc_coeff_token_vlc;

static VLC total_zeros_vlc[15];
static VLC chroma_dc_total_zeros_vlc[3];

static VLC run_vlc[6];
static VLC run7_vlc;

static void svq3_luma_dc_dequant_idct_c(DCTELEM *block, int qp);
static void svq3_add_idct_c(uint8_t *dst, DCTELEM *block, int stride, int qp, int dc);
static void filter_mb( H264Context *h, int mb_x, int mb_y, uint8_t *img_y, uint8_t *img_cb, uint8_t *img_cr);

static inline uint32_t pack16to32(int a, int b){
#ifdef WORDS_BIGENDIAN
   return (b&0xFFFF) + (a<<16);
#else
   return (a&0xFFFF) + (b<<16);
#endif
}

/**
 * fill a rectangle.
 * @param h height of the rectangle, should be a constant
 * @param w width of the rectangle, should be a constant
 * @param size the size of val (1 or 4), should be a constant
 */
static inline void fill_rectangle(void *vp, int w, int h, int stride, uint32_t val, int size){ //FIXME ensure this IS inlined
    uint8_t *p= (uint8_t*)vp;
    assert(size==1 || size==4);
    
    w      *= size;
    stride *= size;
    
//FIXME check what gcc generates for 64 bit on x86 and possible write a 32 bit ver of it
    if(w==2 && h==2){
        *(uint16_t*)(p + 0)=
        *(uint16_t*)(p + stride)= size==4 ? val : val*0x0101;
    }else if(w==2 && h==4){
        *(uint16_t*)(p + 0*stride)=
        *(uint16_t*)(p + 1*stride)=
        *(uint16_t*)(p + 2*stride)=
        *(uint16_t*)(p + 3*stride)= size==4 ? val : val*0x0101;
    }else if(w==4 && h==1){
        *(uint32_t*)(p + 0*stride)= size==4 ? val : val*0x01010101;
    }else if(w==4 && h==2){
        *(uint32_t*)(p + 0*stride)=
        *(uint32_t*)(p + 1*stride)= size==4 ? val : val*0x01010101;
    }else if(w==4 && h==4){
        *(uint32_t*)(p + 0*stride)=
        *(uint32_t*)(p + 1*stride)=
        *(uint32_t*)(p + 2*stride)=
        *(uint32_t*)(p + 3*stride)= size==4 ? val : val*0x01010101;
    }else if(w==8 && h==1){
        *(uint32_t*)(p + 0)=
        *(uint32_t*)(p + 4)= size==4 ? val : val*0x01010101;
    }else if(w==8 && h==2){
        *(uint32_t*)(p + 0 + 0*stride)=
        *(uint32_t*)(p + 4 + 0*stride)=
        *(uint32_t*)(p + 0 + 1*stride)=
        *(uint32_t*)(p + 4 + 1*stride)=  size==4 ? val : val*0x01010101;
    }else if(w==8 && h==4){
        *(uint64_t*)(p + 0*stride)=
        *(uint64_t*)(p + 1*stride)=
        *(uint64_t*)(p + 2*stride)=
        *(uint64_t*)(p + 3*stride)= size==4 ? val*0x0100000001ULL : val*0x0101010101010101ULL;
    }else if(w==16 && h==2){
        *(uint64_t*)(p + 0+0*stride)=
        *(uint64_t*)(p + 8+0*stride)=
        *(uint64_t*)(p + 0+1*stride)=
        *(uint64_t*)(p + 8+1*stride)= size==4 ? val*0x0100000001ULL : val*0x0101010101010101ULL;
    }else if(w==16 && h==4){
        *(uint64_t*)(p + 0+0*stride)=
        *(uint64_t*)(p + 8+0*stride)=
        *(uint64_t*)(p + 0+1*stride)=
        *(uint64_t*)(p + 8+1*stride)=
        *(uint64_t*)(p + 0+2*stride)=
        *(uint64_t*)(p + 8+2*stride)=
        *(uint64_t*)(p + 0+3*stride)=
        *(uint64_t*)(p + 8+3*stride)= size==4 ? val*0x0100000001ULL : val*0x0101010101010101ULL;
    }else
        assert(0);
}

static inline void fill_caches(H264Context *h, int mb_type){
    MpegEncContext * const s = &h->s;
    const int mb_xy= s->mb_x + s->mb_y*s->mb_stride;
    int topleft_xy, top_xy, topright_xy, left_xy[2];
    int topleft_type, top_type, topright_type, left_type[2];
    int left_block[4];
    int i;

    //wow what a mess, why didnt they simplify the interlacing&intra stuff, i cant imagine that these complex rules are worth it 
    
    if(h->sps.mb_aff){
    //FIXME
        topleft_xy = 0; /* avoid warning */
        top_xy = 0; /* avoid warning */
        topright_xy = 0; /* avoid warning */
    }else{
        topleft_xy = mb_xy-1 - s->mb_stride;
        top_xy     = mb_xy   - s->mb_stride;
        topright_xy= mb_xy+1 - s->mb_stride;
        left_xy[0]   = mb_xy-1;
        left_xy[1]   = mb_xy-1;
        left_block[0]= 0;
        left_block[1]= 1;
        left_block[2]= 2;
        left_block[3]= 3;
    }

    topleft_type = h->slice_table[topleft_xy ] == h->slice_num ? s->current_picture.mb_type[topleft_xy] : 0;
    top_type     = h->slice_table[top_xy     ] == h->slice_num ? s->current_picture.mb_type[top_xy]     : 0;
    topright_type= h->slice_table[topright_xy] == h->slice_num ? s->current_picture.mb_type[topright_xy]: 0;
    left_type[0] = h->slice_table[left_xy[0] ] == h->slice_num ? s->current_picture.mb_type[left_xy[0]] : 0;
    left_type[1] = h->slice_table[left_xy[1] ] == h->slice_num ? s->current_picture.mb_type[left_xy[1]] : 0;

    if(IS_INTRA(mb_type)){
        h->topleft_samples_available= 
        h->top_samples_available= 
        h->left_samples_available= 0xFFFF;
        h->topright_samples_available= 0xEEEA;

        if(!IS_INTRA(top_type) && (top_type==0 || h->pps.constrained_intra_pred)){
            h->topleft_samples_available= 0xB3FF;
            h->top_samples_available= 0x33FF;
            h->topright_samples_available= 0x26EA;
        }
        for(i=0; i<2; i++){
            if(!IS_INTRA(left_type[i]) && (left_type[i]==0 || h->pps.constrained_intra_pred)){
                h->topleft_samples_available&= 0xDF5F;
                h->left_samples_available&= 0x5F5F;
            }
        }
        
        if(!IS_INTRA(topleft_type) && (topleft_type==0 || h->pps.constrained_intra_pred))
            h->topleft_samples_available&= 0x7FFF;
        
        if(!IS_INTRA(topright_type) && (topright_type==0 || h->pps.constrained_intra_pred))
            h->topright_samples_available&= 0xFBFF;
    
        if(IS_INTRA4x4(mb_type)){
            if(IS_INTRA4x4(top_type)){
                h->intra4x4_pred_mode_cache[4+8*0]= h->intra4x4_pred_mode[top_xy][4];
                h->intra4x4_pred_mode_cache[5+8*0]= h->intra4x4_pred_mode[top_xy][5];
                h->intra4x4_pred_mode_cache[6+8*0]= h->intra4x4_pred_mode[top_xy][6];
                h->intra4x4_pred_mode_cache[7+8*0]= h->intra4x4_pred_mode[top_xy][3];
            }else{
                int pred;
                if(IS_INTRA16x16(top_type) || (IS_INTER(top_type) && !h->pps.constrained_intra_pred))
                    pred= 2;
                else{
                    pred= -1;
                }
                h->intra4x4_pred_mode_cache[4+8*0]=
                h->intra4x4_pred_mode_cache[5+8*0]=
                h->intra4x4_pred_mode_cache[6+8*0]=
                h->intra4x4_pred_mode_cache[7+8*0]= pred;
            }
            for(i=0; i<2; i++){
                if(IS_INTRA4x4(left_type[i])){
                    h->intra4x4_pred_mode_cache[3+8*1 + 2*8*i]= h->intra4x4_pred_mode[left_xy[i]][left_block[0+2*i]];
                    h->intra4x4_pred_mode_cache[3+8*2 + 2*8*i]= h->intra4x4_pred_mode[left_xy[i]][left_block[1+2*i]];
                }else{
                    int pred;
                    if(IS_INTRA16x16(left_type[i]) || (IS_INTER(left_type[i]) && !h->pps.constrained_intra_pred))
                        pred= 2;
                    else{
                        pred= -1;
                    }
                    h->intra4x4_pred_mode_cache[3+8*1 + 2*8*i]=
                    h->intra4x4_pred_mode_cache[3+8*2 + 2*8*i]= pred;
                }
            }
        }
    }
    
    
/*
0 . T T. T T T T 
1 L . .L . . . . 
2 L . .L . . . . 
3 . T TL . . . . 
4 L . .L . . . . 
5 L . .. . . . . 
*/
//FIXME constraint_intra_pred & partitioning & nnz (lets hope this is just a typo in the spec)
    if(top_type){
        h->non_zero_count_cache[4+8*0]= h->non_zero_count[top_xy][0];
        h->non_zero_count_cache[5+8*0]= h->non_zero_count[top_xy][1];
        h->non_zero_count_cache[6+8*0]= h->non_zero_count[top_xy][2];
        h->non_zero_count_cache[7+8*0]= h->non_zero_count[top_xy][3];
    
        h->non_zero_count_cache[1+8*0]= h->non_zero_count[top_xy][7];
        h->non_zero_count_cache[2+8*0]= h->non_zero_count[top_xy][8];
    
        h->non_zero_count_cache[1+8*3]= h->non_zero_count[top_xy][10];
        h->non_zero_count_cache[2+8*3]= h->non_zero_count[top_xy][11];
        
        h->top_cbp= h->cbp_table[top_xy];
    }else{
        h->non_zero_count_cache[4+8*0]=      
        h->non_zero_count_cache[5+8*0]=
        h->non_zero_count_cache[6+8*0]=
        h->non_zero_count_cache[7+8*0]=
    
        h->non_zero_count_cache[1+8*0]=
        h->non_zero_count_cache[2+8*0]=
    
        h->non_zero_count_cache[1+8*3]=
        h->non_zero_count_cache[2+8*3]= h->pps.cabac && !IS_INTRA(mb_type) ? 0 : 64;
        
        if(IS_INTRA(mb_type)) h->top_cbp= 0x1C0;
        else                  h->top_cbp= 0;
    }
    
    if(left_type[0]){
        h->non_zero_count_cache[3+8*1]= h->non_zero_count[left_xy[0]][6];
        h->non_zero_count_cache[3+8*2]= h->non_zero_count[left_xy[0]][5];
        h->non_zero_count_cache[0+8*1]= h->non_zero_count[left_xy[0]][9]; //FIXME left_block
        h->non_zero_count_cache[0+8*4]= h->non_zero_count[left_xy[0]][12];
        h->left_cbp= h->cbp_table[left_xy[0]]; //FIXME interlacing
    }else{
        h->non_zero_count_cache[3+8*1]= 
        h->non_zero_count_cache[3+8*2]= 
        h->non_zero_count_cache[0+8*1]= 
        h->non_zero_count_cache[0+8*4]= h->pps.cabac && !IS_INTRA(mb_type) ? 0 : 64;
        
        if(IS_INTRA(mb_type)) h->left_cbp= 0x1C0;//FIXME interlacing
        else                  h->left_cbp= 0;
    }
    
    if(left_type[1]){
        h->non_zero_count_cache[3+8*3]= h->non_zero_count[left_xy[1]][4];
        h->non_zero_count_cache[3+8*4]= h->non_zero_count[left_xy[1]][3];
        h->non_zero_count_cache[0+8*2]= h->non_zero_count[left_xy[1]][8];
        h->non_zero_count_cache[0+8*5]= h->non_zero_count[left_xy[1]][11];
    }else{
        h->non_zero_count_cache[3+8*3]= 
        h->non_zero_count_cache[3+8*4]= 
        h->non_zero_count_cache[0+8*2]= 
        h->non_zero_count_cache[0+8*5]= h->pps.cabac && !IS_INTRA(mb_type) ? 0 : 64;
    }
    
#if 1
    //FIXME direct mb can skip much of this
    if(IS_INTER(mb_type) || (IS_DIRECT(mb_type) && h->direct_spatial_mv_pred)){
        int list;
        for(list=0; list<2; list++){
            if((!IS_8X8(mb_type)) && !USES_LIST(mb_type, list) && !IS_DIRECT(mb_type)){
                /*if(!h->mv_cache_clean[list]){
                    memset(h->mv_cache [list],  0, 8*5*2*sizeof(int16_t)); //FIXME clean only input? clean at all?
                    memset(h->ref_cache[list], PART_NOT_AVAILABLE, 8*5*sizeof(int8_t));
                    h->mv_cache_clean[list]= 1;
                }*/
                continue;
            }
            h->mv_cache_clean[list]= 0;
            
            if(IS_INTER(topleft_type)){
                const int b_xy = h->mb2b_xy[topleft_xy] + 3 + 3*h->b_stride;
                const int b8_xy= h->mb2b8_xy[topleft_xy] + 1 + h->b8_stride;
                *(uint32_t*)h->mv_cache[list][scan8[0] - 1 - 1*8]= *(uint32_t*)s->current_picture.motion_val[list][b_xy];
                h->ref_cache[list][scan8[0] - 1 - 1*8]= s->current_picture.ref_index[list][b8_xy];
            }else{
                *(uint32_t*)h->mv_cache[list][scan8[0] - 1 - 1*8]= 0;
                h->ref_cache[list][scan8[0] - 1 - 1*8]= topleft_type ? LIST_NOT_USED : PART_NOT_AVAILABLE;
            }
            
            if(IS_INTER(top_type)){
                const int b_xy= h->mb2b_xy[top_xy] + 3*h->b_stride;
                const int b8_xy= h->mb2b8_xy[top_xy] + h->b8_stride;
                *(uint32_t*)h->mv_cache[list][scan8[0] + 0 - 1*8]= *(uint32_t*)s->current_picture.motion_val[list][b_xy + 0];
                *(uint32_t*)h->mv_cache[list][scan8[0] + 1 - 1*8]= *(uint32_t*)s->current_picture.motion_val[list][b_xy + 1];
                *(uint32_t*)h->mv_cache[list][scan8[0] + 2 - 1*8]= *(uint32_t*)s->current_picture.motion_val[list][b_xy + 2];
                *(uint32_t*)h->mv_cache[list][scan8[0] + 3 - 1*8]= *(uint32_t*)s->current_picture.motion_val[list][b_xy + 3];
                h->ref_cache[list][scan8[0] + 0 - 1*8]=
                h->ref_cache[list][scan8[0] + 1 - 1*8]= s->current_picture.ref_index[list][b8_xy + 0];
                h->ref_cache[list][scan8[0] + 2 - 1*8]=
                h->ref_cache[list][scan8[0] + 3 - 1*8]= s->current_picture.ref_index[list][b8_xy + 1];
            }else{
                *(uint32_t*)h->mv_cache [list][scan8[0] + 0 - 1*8]= 
                *(uint32_t*)h->mv_cache [list][scan8[0] + 1 - 1*8]= 
                *(uint32_t*)h->mv_cache [list][scan8[0] + 2 - 1*8]= 
                *(uint32_t*)h->mv_cache [list][scan8[0] + 3 - 1*8]= 0;
                *(uint32_t*)&h->ref_cache[list][scan8[0] + 0 - 1*8]= ((top_type ? LIST_NOT_USED : PART_NOT_AVAILABLE)&0xFF)*0x01010101;
            }

            if(IS_INTER(topright_type)){
                const int b_xy= h->mb2b_xy[topright_xy] + 3*h->b_stride;
                const int b8_xy= h->mb2b8_xy[topright_xy] + h->b8_stride;
                *(uint32_t*)h->mv_cache[list][scan8[0] + 4 - 1*8]= *(uint32_t*)s->current_picture.motion_val[list][b_xy];
                h->ref_cache[list][scan8[0] + 4 - 1*8]= s->current_picture.ref_index[list][b8_xy];
            }else{
                *(uint32_t*)h->mv_cache [list][scan8[0] + 4 - 1*8]= 0;
                h->ref_cache[list][scan8[0] + 4 - 1*8]= topright_type ? LIST_NOT_USED : PART_NOT_AVAILABLE;
            }
            
            //FIXME unify cleanup or sth
            if(IS_INTER(left_type[0])){
                const int b_xy= h->mb2b_xy[left_xy[0]] + 3;
                const int b8_xy= h->mb2b8_xy[left_xy[0]] + 1;
                *(uint32_t*)h->mv_cache[list][scan8[0] - 1 + 0*8]= *(uint32_t*)s->current_picture.motion_val[list][b_xy + h->b_stride*left_block[0]];
                *(uint32_t*)h->mv_cache[list][scan8[0] - 1 + 1*8]= *(uint32_t*)s->current_picture.motion_val[list][b_xy + h->b_stride*left_block[1]];
                h->ref_cache[list][scan8[0] - 1 + 0*8]= 
                h->ref_cache[list][scan8[0] - 1 + 1*8]= s->current_picture.ref_index[list][b8_xy + h->b8_stride*(left_block[0]>>1)];
            }else{
                *(uint32_t*)h->mv_cache [list][scan8[0] - 1 + 0*8]=
                *(uint32_t*)h->mv_cache [list][scan8[0] - 1 + 1*8]= 0;
                h->ref_cache[list][scan8[0] - 1 + 0*8]=
                h->ref_cache[list][scan8[0] - 1 + 1*8]= left_type[0] ? LIST_NOT_USED : PART_NOT_AVAILABLE;
            }
            
            if(IS_INTER(left_type[1])){
                const int b_xy= h->mb2b_xy[left_xy[1]] + 3;
                const int b8_xy= h->mb2b8_xy[left_xy[1]] + 1;
                *(uint32_t*)h->mv_cache[list][scan8[0] - 1 + 2*8]= *(uint32_t*)s->current_picture.motion_val[list][b_xy + h->b_stride*left_block[2]];
                *(uint32_t*)h->mv_cache[list][scan8[0] - 1 + 3*8]= *(uint32_t*)s->current_picture.motion_val[list][b_xy + h->b_stride*left_block[3]];
                h->ref_cache[list][scan8[0] - 1 + 2*8]= 
                h->ref_cache[list][scan8[0] - 1 + 3*8]= s->current_picture.ref_index[list][b8_xy + h->b8_stride*(left_block[2]>>1)];
            }else{
                *(uint32_t*)h->mv_cache [list][scan8[0] - 1 + 2*8]=
                *(uint32_t*)h->mv_cache [list][scan8[0] - 1 + 3*8]= 0;
                h->ref_cache[list][scan8[0] - 1 + 2*8]=
                h->ref_cache[list][scan8[0] - 1 + 3*8]= left_type[0] ? LIST_NOT_USED : PART_NOT_AVAILABLE;
            }

            h->ref_cache[list][scan8[5 ]+1] = 
            h->ref_cache[list][scan8[7 ]+1] = 
            h->ref_cache[list][scan8[13]+1] =  //FIXME remove past 3 (init somewher else)
            h->ref_cache[list][scan8[4 ]] = 
            h->ref_cache[list][scan8[12]] = PART_NOT_AVAILABLE;
            *(uint32_t*)h->mv_cache [list][scan8[5 ]+1]=
            *(uint32_t*)h->mv_cache [list][scan8[7 ]+1]=
            *(uint32_t*)h->mv_cache [list][scan8[13]+1]= //FIXME remove past 3 (init somewher else)
            *(uint32_t*)h->mv_cache [list][scan8[4 ]]=
            *(uint32_t*)h->mv_cache [list][scan8[12]]= 0;

            if( h->pps.cabac ) {
                /* XXX beurk, Load mvd */
                if(IS_INTER(topleft_type)){
                    const int b_xy = h->mb2b_xy[topleft_xy] + 3 + 3*h->b_stride;
                    *(uint32_t*)h->mvd_cache[list][scan8[0] - 1 - 1*8]= *(uint32_t*)h->mvd_table[list][b_xy];
                }else{
                    *(uint32_t*)h->mvd_cache[list][scan8[0] - 1 - 1*8]= 0;
                }

                if(IS_INTER(top_type)){
                    const int b_xy= h->mb2b_xy[top_xy] + 3*h->b_stride;
                    *(uint32_t*)h->mvd_cache[list][scan8[0] + 0 - 1*8]= *(uint32_t*)h->mvd_table[list][b_xy + 0];
                    *(uint32_t*)h->mvd_cache[list][scan8[0] + 1 - 1*8]= *(uint32_t*)h->mvd_table[list][b_xy + 1];
                    *(uint32_t*)h->mvd_cache[list][scan8[0] + 2 - 1*8]= *(uint32_t*)h->mvd_table[list][b_xy + 2];
                    *(uint32_t*)h->mvd_cache[list][scan8[0] + 3 - 1*8]= *(uint32_t*)h->mvd_table[list][b_xy + 3];
                }else{
                    *(uint32_t*)h->mvd_cache [list][scan8[0] + 0 - 1*8]= 
                    *(uint32_t*)h->mvd_cache [list][scan8[0] + 1 - 1*8]= 
                    *(uint32_t*)h->mvd_cache [list][scan8[0] + 2 - 1*8]= 
                    *(uint32_t*)h->mvd_cache [list][scan8[0] + 3 - 1*8]= 0;
                }
                if(IS_INTER(left_type[0])){
                    const int b_xy= h->mb2b_xy[left_xy[0]] + 3;
                    *(uint32_t*)h->mvd_cache[list][scan8[0] - 1 + 0*8]= *(uint32_t*)h->mvd_table[list][b_xy + h->b_stride*left_block[0]];
                    *(uint32_t*)h->mvd_cache[list][scan8[0] - 1 + 1*8]= *(uint32_t*)h->mvd_table[list][b_xy + h->b_stride*left_block[1]];
                }else{
                    *(uint32_t*)h->mvd_cache [list][scan8[0] - 1 + 0*8]=
                    *(uint32_t*)h->mvd_cache [list][scan8[0] - 1 + 1*8]= 0;
                }
                if(IS_INTER(left_type[1])){
                    const int b_xy= h->mb2b_xy[left_xy[1]] + 3;
                    *(uint32_t*)h->mvd_cache[list][scan8[0] - 1 + 2*8]= *(uint32_t*)h->mvd_table[list][b_xy + h->b_stride*left_block[2]];
                    *(uint32_t*)h->mvd_cache[list][scan8[0] - 1 + 3*8]= *(uint32_t*)h->mvd_table[list][b_xy + h->b_stride*left_block[3]];
                }else{
                    *(uint32_t*)h->mvd_cache [list][scan8[0] - 1 + 2*8]=
                    *(uint32_t*)h->mvd_cache [list][scan8[0] - 1 + 3*8]= 0;
                }
                *(uint32_t*)h->mvd_cache [list][scan8[5 ]+1]=
                *(uint32_t*)h->mvd_cache [list][scan8[7 ]+1]=
                *(uint32_t*)h->mvd_cache [list][scan8[13]+1]= //FIXME remove past 3 (init somewher else)
                *(uint32_t*)h->mvd_cache [list][scan8[4 ]]=
                *(uint32_t*)h->mvd_cache [list][scan8[12]]= 0;

                if(h->slice_type == B_TYPE){
                    fill_rectangle(&h->direct_cache[scan8[0]], 4, 4, 8, 0, 1);

                    if(IS_DIRECT(top_type)){
                        *(uint32_t*)&h->direct_cache[scan8[0] - 1*8]= 0x01010101;
                    }else if(IS_8X8(top_type)){
                        int b8_xy = h->mb2b8_xy[top_xy] + h->b8_stride;
                        h->direct_cache[scan8[0] + 0 - 1*8]= h->direct_table[b8_xy];
                        h->direct_cache[scan8[0] + 2 - 1*8]= h->direct_table[b8_xy + 1];
                    }else{
                        *(uint32_t*)&h->direct_cache[scan8[0] - 1*8]= 0;
                    }
                    
                    //FIXME interlacing
                    if(IS_DIRECT(left_type[0])){
                        h->direct_cache[scan8[0] - 1 + 0*8]=
                        h->direct_cache[scan8[0] - 1 + 2*8]= 1;
                    }else if(IS_8X8(left_type[0])){
                        int b8_xy = h->mb2b8_xy[left_xy[0]] + 1;
                        h->direct_cache[scan8[0] - 1 + 0*8]= h->direct_table[b8_xy];
                        h->direct_cache[scan8[0] - 1 + 2*8]= h->direct_table[b8_xy + h->b8_stride];
                    }else{
                        h->direct_cache[scan8[0] - 1 + 0*8]=
                        h->direct_cache[scan8[0] - 1 + 2*8]= 0;
                    }
                }
            }
        }
    }
#endif
}

static inline void write_back_intra_pred_mode(H264Context *h){
    MpegEncContext * const s = &h->s;
    const int mb_xy= s->mb_x + s->mb_y*s->mb_stride;

    h->intra4x4_pred_mode[mb_xy][0]= h->intra4x4_pred_mode_cache[7+8*1];
    h->intra4x4_pred_mode[mb_xy][1]= h->intra4x4_pred_mode_cache[7+8*2];
    h->intra4x4_pred_mode[mb_xy][2]= h->intra4x4_pred_mode_cache[7+8*3];
    h->intra4x4_pred_mode[mb_xy][3]= h->intra4x4_pred_mode_cache[7+8*4];
    h->intra4x4_pred_mode[mb_xy][4]= h->intra4x4_pred_mode_cache[4+8*4];
    h->intra4x4_pred_mode[mb_xy][5]= h->intra4x4_pred_mode_cache[5+8*4];
    h->intra4x4_pred_mode[mb_xy][6]= h->intra4x4_pred_mode_cache[6+8*4];
}

/**
 * checks if the top & left blocks are available if needed & changes the dc mode so it only uses the available blocks.
 */
static inline int check_intra4x4_pred_mode(H264Context *h){
    MpegEncContext * const s = &h->s;
    static const int8_t top [12]= {-1, 0,LEFT_DC_PRED,-1,-1,-1,-1,-1, 0};
    static const int8_t left[12]= { 0,-1, TOP_DC_PRED, 0,-1,-1,-1, 0,-1,DC_128_PRED};
    int i;
    
    if(!(h->top_samples_available&0x8000)){
        for(i=0; i<4; i++){
            int status= top[ h->intra4x4_pred_mode_cache[scan8[0] + i] ];
            if(status<0){
                av_log(h->s.avctx, AV_LOG_ERROR, "top block unavailable for requested intra4x4 mode %d at %d %d\n", status, s->mb_x, s->mb_y);
                return -1;
            } else if(status){
                h->intra4x4_pred_mode_cache[scan8[0] + i]= status;
            }
        }
    }
    
    if(!(h->left_samples_available&0x8000)){
        for(i=0; i<4; i++){
            int status= left[ h->intra4x4_pred_mode_cache[scan8[0] + 8*i] ];
            if(status<0){
                av_log(h->s.avctx, AV_LOG_ERROR, "left block unavailable for requested intra4x4 mode %d at %d %d\n", status, s->mb_x, s->mb_y);
                return -1;
            } else if(status){
                h->intra4x4_pred_mode_cache[scan8[0] + 8*i]= status;
            }
        }
    }

    return 0;
} //FIXME cleanup like next

/**
 * checks if the top & left blocks are available if needed & changes the dc mode so it only uses the available blocks.
 */
static inline int check_intra_pred_mode(H264Context *h, int mode){
    MpegEncContext * const s = &h->s;
    static const int8_t top [7]= {LEFT_DC_PRED8x8, 1,-1,-1};
    static const int8_t left[7]= { TOP_DC_PRED8x8,-1, 2,-1,DC_128_PRED8x8};
    
    if(mode < 0 || mode > 6) {
        av_log(h->s.avctx, AV_LOG_ERROR, "out of range intra chroma pred mode at %d %d\n", s->mb_x, s->mb_y);
        return -1;
    }
    
    if(!(h->top_samples_available&0x8000)){
        mode= top[ mode ];
        if(mode<0){
            av_log(h->s.avctx, AV_LOG_ERROR, "top block unavailable for requested intra mode at %d %d\n", s->mb_x, s->mb_y);
            return -1;
        }
    }
    
    if(!(h->left_samples_available&0x8000)){
        mode= left[ mode ];
        if(mode<0){
            av_log(h->s.avctx, AV_LOG_ERROR, "left block unavailable for requested intra mode at %d %d\n", s->mb_x, s->mb_y);
            return -1;
        } 
    }

    return mode;
}

/**
 * gets the predicted intra4x4 prediction mode.
 */
static inline int pred_intra_mode(H264Context *h, int n){
    const int index8= scan8[n];
    const int left= h->intra4x4_pred_mode_cache[index8 - 1];
    const int top = h->intra4x4_pred_mode_cache[index8 - 8];
    const int min= FFMIN(left, top);

    tprintf("mode:%d %d min:%d\n", left ,top, min);

    if(min<0) return DC_PRED;
    else      return min;
}

static inline void write_back_non_zero_count(H264Context *h){
    MpegEncContext * const s = &h->s;
    const int mb_xy= s->mb_x + s->mb_y*s->mb_stride;

    h->non_zero_count[mb_xy][0]= h->non_zero_count_cache[4+8*4];
    h->non_zero_count[mb_xy][1]= h->non_zero_count_cache[5+8*4];
    h->non_zero_count[mb_xy][2]= h->non_zero_count_cache[6+8*4];
    h->non_zero_count[mb_xy][3]= h->non_zero_count_cache[7+8*4];
    h->non_zero_count[mb_xy][4]= h->non_zero_count_cache[7+8*3];
    h->non_zero_count[mb_xy][5]= h->non_zero_count_cache[7+8*2];
    h->non_zero_count[mb_xy][6]= h->non_zero_count_cache[7+8*1];
    
    h->non_zero_count[mb_xy][7]= h->non_zero_count_cache[1+8*2];
    h->non_zero_count[mb_xy][8]= h->non_zero_count_cache[2+8*2];
    h->non_zero_count[mb_xy][9]= h->non_zero_count_cache[2+8*1];

    h->non_zero_count[mb_xy][10]=h->non_zero_count_cache[1+8*5];
    h->non_zero_count[mb_xy][11]=h->non_zero_count_cache[2+8*5];
    h->non_zero_count[mb_xy][12]=h->non_zero_count_cache[2+8*4];
}

/**
 * gets the predicted number of non zero coefficients.
 * @param n block index
 */
static inline int pred_non_zero_count(H264Context *h, int n){
    const int index8= scan8[n];
    const int left= h->non_zero_count_cache[index8 - 1];
    const int top = h->non_zero_count_cache[index8 - 8];
    int i= left + top;
    
    if(i<64) i= (i+1)>>1;

    tprintf("pred_nnz L%X T%X n%d s%d P%X\n", left, top, n, scan8[n], i&31);

    return i&31;
}

static inline int fetch_diagonal_mv(H264Context *h, const int16_t **C, int i, int list, int part_width){
    const int topright_ref= h->ref_cache[list][ i - 8 + part_width ];

    if(topright_ref != PART_NOT_AVAILABLE){
        *C= h->mv_cache[list][ i - 8 + part_width ];
        return topright_ref;
    }else{
        tprintf("topright MV not available\n");

        *C= h->mv_cache[list][ i - 8 - 1 ];
        return h->ref_cache[list][ i - 8 - 1 ];
    }
}

/**
 * gets the predicted MV.
 * @param n the block index
 * @param part_width the width of the partition (4, 8,16) -> (1, 2, 4)
 * @param mx the x component of the predicted motion vector
 * @param my the y component of the predicted motion vector
 */
static inline void pred_motion(H264Context * const h, int n, int part_width, int list, int ref, int * const mx, int * const my){
    const int index8= scan8[n];
    const int top_ref=      h->ref_cache[list][ index8 - 8 ];
    const int left_ref=     h->ref_cache[list][ index8 - 1 ];
    const int16_t * const A= h->mv_cache[list][ index8 - 1 ];
    const int16_t * const B= h->mv_cache[list][ index8 - 8 ];
    const int16_t * C;
    int diagonal_ref, match_count;

    assert(part_width==1 || part_width==2 || part_width==4);

/* mv_cache
  B . . A T T T T 
  U . . L . . , .
  U . . L . . . .
  U . . L . . , .
  . . . L . . . .
*/

    diagonal_ref= fetch_diagonal_mv(h, &C, index8, list, part_width);
    match_count= (diagonal_ref==ref) + (top_ref==ref) + (left_ref==ref);
    if(match_count > 1){ //most common
        *mx= mid_pred(A[0], B[0], C[0]);
        *my= mid_pred(A[1], B[1], C[1]);
    }else if(match_count==1){
        if(left_ref==ref){
            *mx= A[0];
            *my= A[1];        
        }else if(top_ref==ref){
            *mx= B[0];
            *my= B[1];        
        }else{
            *mx= C[0];
            *my= C[1];        
        }
    }else{
        if(top_ref == PART_NOT_AVAILABLE && diagonal_ref == PART_NOT_AVAILABLE && left_ref != PART_NOT_AVAILABLE){
            *mx= A[0];
            *my= A[1];        
        }else{
            *mx= mid_pred(A[0], B[0], C[0]);
            *my= mid_pred(A[1], B[1], C[1]);
        }
    }
        
    tprintf("pred_motion (%2d %2d %2d) (%2d %2d %2d) (%2d %2d %2d) -> (%2d %2d %2d) at %2d %2d %d list %d\n", top_ref, B[0], B[1],                    diagonal_ref, C[0], C[1], left_ref, A[0], A[1], ref, *mx, *my, h->s.mb_x, h->s.mb_y, n, list);
}

/**
 * gets the directionally predicted 16x8 MV.
 * @param n the block index
 * @param mx the x component of the predicted motion vector
 * @param my the y component of the predicted motion vector
 */
static inline void pred_16x8_motion(H264Context * const h, int n, int list, int ref, int * const mx, int * const my){
    if(n==0){
        const int top_ref=      h->ref_cache[list][ scan8[0] - 8 ];
        const int16_t * const B= h->mv_cache[list][ scan8[0] - 8 ];

        tprintf("pred_16x8: (%2d %2d %2d) at %2d %2d %d list %d\n", top_ref, B[0], B[1], h->s.mb_x, h->s.mb_y, n, list);
        
        if(top_ref == ref){
            *mx= B[0];
            *my= B[1];
            return;
        }
    }else{
        const int left_ref=     h->ref_cache[list][ scan8[8] - 1 ];
        const int16_t * const A= h->mv_cache[list][ scan8[8] - 1 ];
        
        tprintf("pred_16x8: (%2d %2d %2d) at %2d %2d %d list %d\n", left_ref, A[0], A[1], h->s.mb_x, h->s.mb_y, n, list);

        if(left_ref == ref){
            *mx= A[0];
            *my= A[1];
            return;
        }
    }

    //RARE
    pred_motion(h, n, 4, list, ref, mx, my);
}

/**
 * gets the directionally predicted 8x16 MV.
 * @param n the block index
 * @param mx the x component of the predicted motion vector
 * @param my the y component of the predicted motion vector
 */
static inline void pred_8x16_motion(H264Context * const h, int n, int list, int ref, int * const mx, int * const my){
    if(n==0){
        const int left_ref=      h->ref_cache[list][ scan8[0] - 1 ];
        const int16_t * const A=  h->mv_cache[list][ scan8[0] - 1 ];
        
        tprintf("pred_8x16: (%2d %2d %2d) at %2d %2d %d list %d\n", left_ref, A[0], A[1], h->s.mb_x, h->s.mb_y, n, list);

        if(left_ref == ref){
            *mx= A[0];
            *my= A[1];
            return;
        }
    }else{
        const int16_t * C;
        int diagonal_ref;

        diagonal_ref= fetch_diagonal_mv(h, &C, scan8[4], list, 2);
        
        tprintf("pred_8x16: (%2d %2d %2d) at %2d %2d %d list %d\n", diagonal_ref, C[0], C[1], h->s.mb_x, h->s.mb_y, n, list);

        if(diagonal_ref == ref){ 
            *mx= C[0];
            *my= C[1];
            return;
        }
    }

    //RARE
    pred_motion(h, n, 2, list, ref, mx, my);
}

static inline void pred_pskip_motion(H264Context * const h, int * const mx, int * const my){
    const int top_ref = h->ref_cache[0][ scan8[0] - 8 ];
    const int left_ref= h->ref_cache[0][ scan8[0] - 1 ];

    tprintf("pred_pskip: (%d) (%d) at %2d %2d\n", top_ref, left_ref, h->s.mb_x, h->s.mb_y);

    if(top_ref == PART_NOT_AVAILABLE || left_ref == PART_NOT_AVAILABLE
       || (top_ref == 0  && *(uint32_t*)h->mv_cache[0][ scan8[0] - 8 ] == 0)
       || (left_ref == 0 && *(uint32_t*)h->mv_cache[0][ scan8[0] - 1 ] == 0)){
       
        *mx = *my = 0;
        return;
    }
        
    pred_motion(h, 0, 4, 0, 0, mx, my);

    return;
}

static inline void direct_dist_scale_factor(H264Context * const h){
    const int poc = h->s.current_picture_ptr->poc;
    const int poc1 = h->ref_list[1][0].poc;
    int i;
    for(i=0; i<h->ref_count[0]; i++){
        int poc0 = h->ref_list[0][i].poc;
        int td = clip(poc1 - poc0, -128, 127);
        if(td == 0 /* FIXME || pic0 is a long-term ref */){
            h->dist_scale_factor[i] = 256;
        }else{
            int tb = clip(poc - poc0, -128, 127);
            int tx = (16384 + (ABS(td) >> 1)) / td;
            h->dist_scale_factor[i] = clip((tb*tx + 32) >> 6, -1024, 1023);
        }
    }
}

static inline void pred_direct_motion(H264Context * const h, int *mb_type){
    MpegEncContext * const s = &h->s;
    const int mb_xy =   s->mb_x +   s->mb_y*s->mb_stride;
    const int b8_xy = 2*s->mb_x + 2*s->mb_y*h->b8_stride;
    const int b4_xy = 4*s->mb_x + 4*s->mb_y*h->b_stride;
    const int mb_type_col = h->ref_list[1][0].mb_type[mb_xy];
    const int16_t (*l1mv0)[2] = (const int16_t (*)[2]) &h->ref_list[1][0].motion_val[0][b4_xy];
    const int8_t *l1ref0 = &h->ref_list[1][0].ref_index[0][b8_xy];
    const int is_b8x8 = IS_8X8(*mb_type);
    int sub_mb_type;
    int i8, i4;

    if(IS_8X8(mb_type_col) && !h->sps.direct_8x8_inference_flag){
        /* FIXME save sub mb types from previous frames (or derive from MVs)
         * so we know exactly what block size to use */
        sub_mb_type = MB_TYPE_8x8|MB_TYPE_P0L0|MB_TYPE_P0L1|MB_TYPE_DIRECT2; /* B_SUB_4x4 */
        *mb_type =    MB_TYPE_8x8;
    }else if(!is_b8x8 && (IS_16X16(mb_type_col) || IS_INTRA(mb_type_col))){
        sub_mb_type = MB_TYPE_16x16|MB_TYPE_P0L0|MB_TYPE_P0L1|MB_TYPE_DIRECT2; /* B_SUB_8x8 */
        *mb_type =    MB_TYPE_16x16|MB_TYPE_P0L0|MB_TYPE_P0L1|MB_TYPE_DIRECT2; /* B_16x16 */
    }else{
        sub_mb_type = MB_TYPE_16x16|MB_TYPE_P0L0|MB_TYPE_P0L1|MB_TYPE_DIRECT2; /* B_SUB_8x8 */
        *mb_type =    MB_TYPE_8x8;
    }
    if(!is_b8x8)
        *mb_type |= MB_TYPE_DIRECT2;

    if(h->direct_spatial_mv_pred){
        int ref[2];
        int mv[2][2];
        int list;

        /* ref = min(neighbors) */
        for(list=0; list<2; list++){
            int refa = h->ref_cache[list][scan8[0] - 1];
            int refb = h->ref_cache[list][scan8[0] - 8];
            int refc = h->ref_cache[list][scan8[0] - 8 + 4];
            if(refc == -2)
                refc = h->ref_cache[list][scan8[0] - 8 - 1];
            ref[list] = refa;
            if(ref[list] < 0 || (refb < ref[list] && refb >= 0))
                ref[list] = refb;
            if(ref[list] < 0 || (refc < ref[list] && refc >= 0))
                ref[list] = refc;
            if(ref[list] < 0)
                ref[list] = -1;
        }

        if(ref[0] < 0 && ref[1] < 0){
            ref[0] = ref[1] = 0;
            mv[0][0] = mv[0][1] =
            mv[1][0] = mv[1][1] = 0;
        }else{
            for(list=0; list<2; list++){
                if(ref[list] >= 0)
                    pred_motion(h, 0, 4, list, ref[list], &mv[list][0], &mv[list][1]);
                else
                    mv[list][0] = mv[list][1] = 0;
            }
        }

        if(ref[1] < 0){
            *mb_type &= ~MB_TYPE_P0L1;
            sub_mb_type &= ~MB_TYPE_P0L1;
        }else if(ref[0] < 0){
            *mb_type &= ~MB_TYPE_P0L0;
            sub_mb_type &= ~MB_TYPE_P0L0;
        }

        if(IS_16X16(*mb_type)){
            fill_rectangle(&h->ref_cache[0][scan8[0]], 4, 4, 8, ref[0], 1);
            fill_rectangle(&h->ref_cache[1][scan8[0]], 4, 4, 8, ref[1], 1);
            if(!IS_INTRA(mb_type_col) && l1ref0[0] == 0 &&
                ABS(l1mv0[0][0]) <= 1 && ABS(l1mv0[0][1]) <= 1){
                if(ref[0] > 0)
                    fill_rectangle(&h->mv_cache[0][scan8[0]], 4, 4, 8, pack16to32(mv[0][0],mv[0][1]), 4);
                else
                    fill_rectangle(&h->mv_cache[0][scan8[0]], 4, 4, 8, 0, 4);
                if(ref[1] > 0)
                    fill_rectangle(&h->mv_cache[1][scan8[0]], 4, 4, 8, pack16to32(mv[1][0],mv[1][1]), 4);
                else
                    fill_rectangle(&h->mv_cache[1][scan8[0]], 4, 4, 8, 0, 4);
            }else{
                fill_rectangle(&h->mv_cache[0][scan8[0]], 4, 4, 8, pack16to32(mv[0][0],mv[0][1]), 4);
                fill_rectangle(&h->mv_cache[1][scan8[0]], 4, 4, 8, pack16to32(mv[1][0],mv[1][1]), 4);
            }
        }else{
            for(i8=0; i8<4; i8++){
                const int x8 = i8&1;
                const int y8 = i8>>1;
    
                if(is_b8x8 && !IS_DIRECT(h->sub_mb_type[i8]))
                    continue;
                h->sub_mb_type[i8] = sub_mb_type;
    
                fill_rectangle(&h->mv_cache[0][scan8[i8*4]], 2, 2, 8, pack16to32(mv[0][0],mv[0][1]), 4);
                fill_rectangle(&h->mv_cache[1][scan8[i8*4]], 2, 2, 8, pack16to32(mv[1][0],mv[1][1]), 4);
                fill_rectangle(&h->ref_cache[0][scan8[i8*4]], 2, 2, 8, ref[0], 1);
                fill_rectangle(&h->ref_cache[1][scan8[i8*4]], 2, 2, 8, ref[1], 1);
    
                /* col_zero_flag */
                if(!IS_INTRA(mb_type_col) && l1ref0[x8 + y8*h->b8_stride] == 0){
                    for(i4=0; i4<4; i4++){
                        const int16_t *mv_col = l1mv0[x8*2 + (i4&1) + (y8*2 + (i4>>1))*h->b_stride];
                        if(ABS(mv_col[0]) <= 1 && ABS(mv_col[1]) <= 1){
                            if(ref[0] == 0)
                                *(uint32_t*)h->mv_cache[0][scan8[i8*4+i4]] = 0;
                            if(ref[1] == 0)
                                *(uint32_t*)h->mv_cache[1][scan8[i8*4+i4]] = 0;
                        }
                    }
                }
            }
        }
    }else{ /* direct temporal mv pred */
        /* FIXME assumes that L1ref0 used the same ref lists as current frame */
        if(IS_16X16(*mb_type)){
            fill_rectangle(&h->ref_cache[1][scan8[0]], 4, 4, 8, 0, 1);
            if(IS_INTRA(mb_type_col)){
                fill_rectangle(&h->ref_cache[0][scan8[0]], 4, 4, 8, 0, 1);
                fill_rectangle(&h-> mv_cache[0][scan8[0]], 4, 4, 8, 0, 4);
                fill_rectangle(&h-> mv_cache[1][scan8[0]], 4, 4, 8, 0, 4);
            }else{
                const int ref0 = l1ref0[0];
                const int dist_scale_factor = h->dist_scale_factor[ref0];
                const int16_t *mv_col = l1mv0[0];
                int mv_l0[2];
                mv_l0[0] = (dist_scale_factor * mv_col[0] + 128) >> 8;
                mv_l0[1] = (dist_scale_factor * mv_col[1] + 128) >> 8;
                fill_rectangle(&h->ref_cache[0][scan8[0]], 4, 4, 8, ref0, 1);
                fill_rectangle(&h-> mv_cache[0][scan8[0]], 4, 4, 8, pack16to32(mv_l0[0],mv_l0[1]), 4);
                fill_rectangle(&h-> mv_cache[1][scan8[0]], 4, 4, 8, pack16to32(mv_l0[0]-mv_col[0],mv_l0[1]-mv_col[1]), 4);
            }
        }else{
            for(i8=0; i8<4; i8++){
                const int x8 = i8&1;
                const int y8 = i8>>1;
                int ref0, dist_scale_factor;
    
                if(is_b8x8 && !IS_DIRECT(h->sub_mb_type[i8]))
                    continue;
                h->sub_mb_type[i8] = sub_mb_type;
                if(IS_INTRA(mb_type_col)){
                    fill_rectangle(&h->ref_cache[0][scan8[i8*4]], 2, 2, 8, 0, 1);
                    fill_rectangle(&h->ref_cache[1][scan8[i8*4]], 2, 2, 8, 0, 1);
                    fill_rectangle(&h-> mv_cache[0][scan8[i8*4]], 2, 2, 8, 0, 4);
                    fill_rectangle(&h-> mv_cache[1][scan8[i8*4]], 2, 2, 8, 0, 4);
                    continue;
                }
    
                ref0 = l1ref0[x8 + y8*h->b8_stride];
                dist_scale_factor = h->dist_scale_factor[ref0];
    
                fill_rectangle(&h->ref_cache[0][scan8[i8*4]], 2, 2, 8, ref0, 1);
                fill_rectangle(&h->ref_cache[1][scan8[i8*4]], 2, 2, 8, 0, 1);
                for(i4=0; i4<4; i4++){
                    const int16_t *mv_col = l1mv0[x8*2 + (i4&1) + (y8*2 + (i4>>1))*h->b_stride];
                    int16_t *mv_l0 = h->mv_cache[0][scan8[i8*4+i4]];
                    mv_l0[0] = (dist_scale_factor * mv_col[0] + 128) >> 8;
                    mv_l0[1] = (dist_scale_factor * mv_col[1] + 128) >> 8;
                    *(uint32_t*)h->mv_cache[1][scan8[i8*4+i4]] =
                        pack16to32(mv_l0[0]-mv_col[0],mv_l0[1]-mv_col[1]);
                }
            }
        }
    }
}

static inline void write_back_motion(H264Context *h, int mb_type){
    MpegEncContext * const s = &h->s;
    const int b_xy = 4*s->mb_x + 4*s->mb_y*h->b_stride;
    const int b8_xy= 2*s->mb_x + 2*s->mb_y*h->b8_stride;
    int list;

    for(list=0; list<2; list++){
        int y;
        if((!IS_8X8(mb_type)) && !USES_LIST(mb_type, list)){
            if(1){ //FIXME skip or never read if mb_type doesnt use it
                for(y=0; y<4; y++){
                    *(uint64_t*)s->current_picture.motion_val[list][b_xy + 0 + y*h->b_stride]=
                    *(uint64_t*)s->current_picture.motion_val[list][b_xy + 2 + y*h->b_stride]= 0;
                }
                if( h->pps.cabac ) {
                    /* FIXME needed ? */
                    for(y=0; y<4; y++){
                        *(uint64_t*)h->mvd_table[list][b_xy + 0 + y*h->b_stride]=
                        *(uint64_t*)h->mvd_table[list][b_xy + 2 + y*h->b_stride]= 0;
                    }
                }
                for(y=0; y<2; y++){
                    *(uint16_t*)&s->current_picture.ref_index[list][b8_xy + y*h->b8_stride]= (LIST_NOT_USED&0xFF)*0x0101;
                }
            }
            continue;
        }
        
        for(y=0; y<4; y++){
            *(uint64_t*)s->current_picture.motion_val[list][b_xy + 0 + y*h->b_stride]= *(uint64_t*)h->mv_cache[list][scan8[0]+0 + 8*y];
            *(uint64_t*)s->current_picture.motion_val[list][b_xy + 2 + y*h->b_stride]= *(uint64_t*)h->mv_cache[list][scan8[0]+2 + 8*y];
        }
        if( h->pps.cabac ) {
            for(y=0; y<4; y++){
                *(uint64_t*)h->mvd_table[list][b_xy + 0 + y*h->b_stride]= *(uint64_t*)h->mvd_cache[list][scan8[0]+0 + 8*y];
                *(uint64_t*)h->mvd_table[list][b_xy + 2 + y*h->b_stride]= *(uint64_t*)h->mvd_cache[list][scan8[0]+2 + 8*y];
            }
        }
        for(y=0; y<2; y++){
            s->current_picture.ref_index[list][b8_xy + 0 + y*h->b8_stride]= h->ref_cache[list][scan8[0]+0 + 16*y];
            s->current_picture.ref_index[list][b8_xy + 1 + y*h->b8_stride]= h->ref_cache[list][scan8[0]+2 + 16*y];
        }
    }
    
    if(h->slice_type == B_TYPE && h->pps.cabac){
        if(IS_8X8(mb_type)){
            h->direct_table[b8_xy+1+0*h->b8_stride] = IS_DIRECT(h->sub_mb_type[1]) ? 1 : 0;
            h->direct_table[b8_xy+0+1*h->b8_stride] = IS_DIRECT(h->sub_mb_type[2]) ? 1 : 0;
            h->direct_table[b8_xy+1+1*h->b8_stride] = IS_DIRECT(h->sub_mb_type[3]) ? 1 : 0;
        }
    }
}

/**
 * Decodes a network abstraction layer unit.
 * @param consumed is the number of bytes used as input
 * @param length is the length of the array
 * @param dst_length is the number of decoded bytes FIXME here or a decode rbsp ttailing?
 * @returns decoded bytes, might be src+1 if no escapes 
 */
static uint8_t *decode_nal(H264Context *h, uint8_t *src, int *dst_length, int *consumed, int length){
    int i, si, di;
    uint8_t *dst;

//    src[0]&0x80;		//forbidden bit
    h->nal_ref_idc= src[0]>>5;
    h->nal_unit_type= src[0]&0x1F;

    src++; length--;
#if 0    
    for(i=0; i<length; i++)
        printf("%2X ", src[i]);
#endif
    for(i=0; i+1<length; i+=2){
        if(src[i]) continue;
        if(i>0 && src[i-1]==0) i--;
        if(i+2<length && src[i+1]==0 && src[i+2]<=3){
            if(src[i+2]!=3){
                /* startcode, so we must be past the end */
                length=i;
            }
            break;
        }
    }

    if(i>=length-1){ //no escaped 0
        *dst_length= length;
        *consumed= length+1; //+1 for the header
        return src; 
    }

    h->rbsp_buffer= av_fast_realloc(h->rbsp_buffer, &h->rbsp_buffer_size, length);
    dst= h->rbsp_buffer;

//printf("deoding esc\n");
    si=di=0;
    while(si<length){ 
        //remove escapes (very rare 1:2^22)
        if(si+2<length && src[si]==0 && src[si+1]==0 && src[si+2]<=3){
            if(src[si+2]==3){ //escape
                dst[di++]= 0;
                dst[di++]= 0;
                si+=3;
                continue;
            }else //next start code
                break;
        }

        dst[di++]= src[si++];
    }

    *dst_length= di;
    *consumed= si + 1;//+1 for the header
//FIXME store exact number of bits in the getbitcontext (its needed for decoding)
    return dst;
}

#if 0
/**
 * @param src the data which should be escaped
 * @param dst the target buffer, dst+1 == src is allowed as a special case
 * @param length the length of the src data
 * @param dst_length the length of the dst array
 * @returns length of escaped data in bytes or -1 if an error occured
 */
static int encode_nal(H264Context *h, uint8_t *dst, uint8_t *src, int length, int dst_length){
    int i, escape_count, si, di;
    uint8_t *temp;
    
    assert(length>=0);
    assert(dst_length>0);
    
    dst[0]= (h->nal_ref_idc<<5) + h->nal_unit_type;

    if(length==0) return 1;

    escape_count= 0;
    for(i=0; i<length; i+=2){
        if(src[i]) continue;
        if(i>0 && src[i-1]==0) 
            i--;
        if(i+2<length && src[i+1]==0 && src[i+2]<=3){
            escape_count++;
            i+=2;
        }
    }
    
    if(escape_count==0){ 
        if(dst+1 != src)
            memcpy(dst+1, src, length);
        return length + 1;
    }
    
    if(length + escape_count + 1> dst_length)
        return -1;

    //this should be damn rare (hopefully)

    h->rbsp_buffer= av_fast_realloc(h->rbsp_buffer, &h->rbsp_buffer_size, length + escape_count);
    temp= h->rbsp_buffer;
//printf("encoding esc\n");
    
    si= 0;
    di= 0;
    while(si < length){
        if(si+2<length && src[si]==0 && src[si+1]==0 && src[si+2]<=3){
            temp[di++]= 0; si++;
            temp[di++]= 0; si++;
            temp[di++]= 3; 
            temp[di++]= src[si++];
        }
        else
            temp[di++]= src[si++];
    }
    memcpy(dst+1, temp, length+escape_count);
    
    assert(di == length+escape_count);
    
    return di + 1;
}

/**
 * write 1,10,100,1000,... for alignment, yes its exactly inverse to mpeg4
 */
static void encode_rbsp_trailing(PutBitContext *pb){
    int length;
    put_bits(pb, 1, 1);
    length= (-put_bits_count(pb))&7;
    if(length) put_bits(pb, length, 0);
}
#endif

/**
 * identifies the exact end of the bitstream
 * @return the length of the trailing, or 0 if damaged
 */
static int decode_rbsp_trailing(uint8_t *src){
    int v= *src;
    int r;

    tprintf("rbsp trailing %X\n", v);

    for(r=1; r<9; r++){
        if(v&1) return r;
        v>>=1;
    }
    return 0;
}

/**
 * idct tranforms the 16 dc values and dequantize them.
 * @param qp quantization parameter
 */
static void h264_luma_dc_dequant_idct_c(DCTELEM *block, int qp){
    const int qmul= dequant_coeff[qp][0];
#define stride 16
    int i;
    int temp[16]; //FIXME check if this is a good idea
    static const int x_offset[4]={0, 1*stride, 4* stride,  5*stride};
    static const int y_offset[4]={0, 2*stride, 8* stride, 10*stride};

//memset(block, 64, 2*256);
//return;
    for(i=0; i<4; i++){
        const int offset= y_offset[i];
        const int z0= block[offset+stride*0] + block[offset+stride*4];
        const int z1= block[offset+stride*0] - block[offset+stride*4];
        const int z2= block[offset+stride*1] - block[offset+stride*5];
        const int z3= block[offset+stride*1] + block[offset+stride*5];

        temp[4*i+0]= z0+z3;
        temp[4*i+1]= z1+z2;
        temp[4*i+2]= z1-z2;
        temp[4*i+3]= z0-z3;
    }

    for(i=0; i<4; i++){
        const int offset= x_offset[i];
        const int z0= temp[4*0+i] + temp[4*2+i];
        const int z1= temp[4*0+i] - temp[4*2+i];
        const int z2= temp[4*1+i] - temp[4*3+i];
        const int z3= temp[4*1+i] + temp[4*3+i];

        block[stride*0 +offset]= ((z0 + z3)*qmul + 2)>>2; //FIXME think about merging this into decode_resdual
        block[stride*2 +offset]= ((z1 + z2)*qmul + 2)>>2;
        block[stride*8 +offset]= ((z1 - z2)*qmul + 2)>>2;
        block[stride*10+offset]= ((z0 - z3)*qmul + 2)>>2;
    }
}

#if 0
/**
 * dct tranforms the 16 dc values.
 * @param qp quantization parameter ??? FIXME
 */
static void h264_luma_dc_dct_c(DCTELEM *block/*, int qp*/){
//    const int qmul= dequant_coeff[qp][0];
    int i;
    int temp[16]; //FIXME check if this is a good idea
    static const int x_offset[4]={0, 1*stride, 4* stride,  5*stride};
    static const int y_offset[4]={0, 2*stride, 8* stride, 10*stride};

    for(i=0; i<4; i++){
        const int offset= y_offset[i];
        const int z0= block[offset+stride*0] + block[offset+stride*4];
        const int z1= block[offset+stride*0] - block[offset+stride*4];
        const int z2= block[offset+stride*1] - block[offset+stride*5];
        const int z3= block[offset+stride*1] + block[offset+stride*5];

        temp[4*i+0]= z0+z3;
        temp[4*i+1]= z1+z2;
        temp[4*i+2]= z1-z2;
        temp[4*i+3]= z0-z3;
    }

    for(i=0; i<4; i++){
        const int offset= x_offset[i];
        const int z0= temp[4*0+i] + temp[4*2+i];
        const int z1= temp[4*0+i] - temp[4*2+i];
        const int z2= temp[4*1+i] - temp[4*3+i];
        const int z3= temp[4*1+i] + temp[4*3+i];

        block[stride*0 +offset]= (z0 + z3)>>1;
        block[stride*2 +offset]= (z1 + z2)>>1;
        block[stride*8 +offset]= (z1 - z2)>>1;
        block[stride*10+offset]= (z0 - z3)>>1;
    }
}
#endif

#undef xStride
#undef stride

static void chroma_dc_dequant_idct_c(DCTELEM *block, int qp){
    const int qmul= dequant_coeff[qp][0];
    const int stride= 16*2;
    const int xStride= 16;
    int a,b,c,d,e;

    a= block[stride*0 + xStride*0];
    b= block[stride*0 + xStride*1];
    c= block[stride*1 + xStride*0];
    d= block[stride*1 + xStride*1];

    e= a-b;
    a= a+b;
    b= c-d;
    c= c+d;

    block[stride*0 + xStride*0]= ((a+c)*qmul + 0)>>1;
    block[stride*0 + xStride*1]= ((e+b)*qmul + 0)>>1;
    block[stride*1 + xStride*0]= ((a-c)*qmul + 0)>>1;
    block[stride*1 + xStride*1]= ((e-b)*qmul + 0)>>1;
}

#if 0
static void chroma_dc_dct_c(DCTELEM *block){
    const int stride= 16*2;
    const int xStride= 16;
    int a,b,c,d,e;

    a= block[stride*0 + xStride*0];
    b= block[stride*0 + xStride*1];
    c= block[stride*1 + xStride*0];
    d= block[stride*1 + xStride*1];

    e= a-b;
    a= a+b;
    b= c-d;
    c= c+d;

    block[stride*0 + xStride*0]= (a+c);
    block[stride*0 + xStride*1]= (e+b);
    block[stride*1 + xStride*0]= (a-c);
    block[stride*1 + xStride*1]= (e-b);
}
#endif

/**
 * gets the chroma qp.
 */
static inline int get_chroma_qp(H264Context *h, int qscale){
    
    return chroma_qp[clip(qscale + h->pps.chroma_qp_index_offset, 0, 51)];
}


#if 0
static void h264_diff_dct_c(DCTELEM *block, uint8_t *src1, uint8_t *src2, int stride){
    int i;
    //FIXME try int temp instead of block
    
    for(i=0; i<4; i++){
        const int d0= src1[0 + i*stride] - src2[0 + i*stride];
        const int d1= src1[1 + i*stride] - src2[1 + i*stride];
        const int d2= src1[2 + i*stride] - src2[2 + i*stride];
        const int d3= src1[3 + i*stride] - src2[3 + i*stride];
        const int z0= d0 + d3;
        const int z3= d0 - d3;
        const int z1= d1 + d2;
        const int z2= d1 - d2;
        
        block[0 + 4*i]=   z0 +   z1;
        block[1 + 4*i]= 2*z3 +   z2;
        block[2 + 4*i]=   z0 -   z1;
        block[3 + 4*i]=   z3 - 2*z2;
    }    

    for(i=0; i<4; i++){
        const int z0= block[0*4 + i] + block[3*4 + i];
        const int z3= block[0*4 + i] - block[3*4 + i];
        const int z1= block[1*4 + i] + block[2*4 + i];
        const int z2= block[1*4 + i] - block[2*4 + i];
        
        block[0*4 + i]=   z0 +   z1;
        block[1*4 + i]= 2*z3 +   z2;
        block[2*4 + i]=   z0 -   z1;
        block[3*4 + i]=   z3 - 2*z2;
    }
}
#endif

//FIXME need to check that this doesnt overflow signed 32 bit for low qp, iam not sure, its very close
//FIXME check that gcc inlines this (and optimizes intra & seperate_dc stuff away)
static inline int quantize_c(DCTELEM *block, uint8_t *scantable, int qscale, int intra, int seperate_dc){
    int i;
    const int * const quant_table= quant_coeff[qscale];
    const int bias= intra ? (1<<QUANT_SHIFT)/3 : (1<<QUANT_SHIFT)/6;
    const unsigned int threshold1= (1<<QUANT_SHIFT) - bias - 1;
    const unsigned int threshold2= (threshold1<<1);
    int last_non_zero;

    if(seperate_dc){
        if(qscale<=18){
            //avoid overflows
            const int dc_bias= intra ? (1<<(QUANT_SHIFT-2))/3 : (1<<(QUANT_SHIFT-2))/6;
            const unsigned int dc_threshold1= (1<<(QUANT_SHIFT-2)) - dc_bias - 1;
            const unsigned int dc_threshold2= (dc_threshold1<<1);

            int level= block[0]*quant_coeff[qscale+18][0];
            if(((unsigned)(level+dc_threshold1))>dc_threshold2){
                if(level>0){
                    level= (dc_bias + level)>>(QUANT_SHIFT-2);
                    block[0]= level;
                }else{
                    level= (dc_bias - level)>>(QUANT_SHIFT-2);
                    block[0]= -level;
                }
//                last_non_zero = i;
            }else{
                block[0]=0;
            }
        }else{
            const int dc_bias= intra ? (1<<(QUANT_SHIFT+1))/3 : (1<<(QUANT_SHIFT+1))/6;
            const unsigned int dc_threshold1= (1<<(QUANT_SHIFT+1)) - dc_bias - 1;
            const unsigned int dc_threshold2= (dc_threshold1<<1);

            int level= block[0]*quant_table[0];
            if(((unsigned)(level+dc_threshold1))>dc_threshold2){
                if(level>0){
                    level= (dc_bias + level)>>(QUANT_SHIFT+1);
                    block[0]= level;
                }else{
                    level= (dc_bias - level)>>(QUANT_SHIFT+1);
                    block[0]= -level;
                }
//                last_non_zero = i;
            }else{
                block[0]=0;
            }
        }
        last_non_zero= 0;
        i=1;
    }else{
        last_non_zero= -1;
        i=0;
    }

    for(; i<16; i++){
        const int j= scantable[i];
        int level= block[j]*quant_table[j];

//        if(   bias+level >= (1<<(QMAT_SHIFT - 3))
//           || bias-level >= (1<<(QMAT_SHIFT - 3))){
        if(((unsigned)(level+threshold1))>threshold2){
            if(level>0){
                level= (bias + level)>>QUANT_SHIFT;
                block[j]= level;
            }else{
                level= (bias - level)>>QUANT_SHIFT;
                block[j]= -level;
            }
            last_non_zero = i;
        }else{
            block[j]=0;
        }
    }

    return last_non_zero;
}

static void pred4x4_vertical_c(uint8_t *src, uint8_t *topright, int stride){
    const uint32_t a= ((uint32_t*)(src-stride))[0];
    ((uint32_t*)(src+0*stride))[0]= a;
    ((uint32_t*)(src+1*stride))[0]= a;
    ((uint32_t*)(src+2*stride))[0]= a;
    ((uint32_t*)(src+3*stride))[0]= a;
}

static void pred4x4_horizontal_c(uint8_t *src, uint8_t *topright, int stride){
    ((uint32_t*)(src+0*stride))[0]= src[-1+0*stride]*0x01010101;
    ((uint32_t*)(src+1*stride))[0]= src[-1+1*stride]*0x01010101;
    ((uint32_t*)(src+2*stride))[0]= src[-1+2*stride]*0x01010101;
    ((uint32_t*)(src+3*stride))[0]= src[-1+3*stride]*0x01010101;
}

static void pred4x4_dc_c(uint8_t *src, uint8_t *topright, int stride){
    const int dc= (  src[-stride] + src[1-stride] + src[2-stride] + src[3-stride]
                   + src[-1+0*stride] + src[-1+1*stride] + src[-1+2*stride] + src[-1+3*stride] + 4) >>3;
    
    ((uint32_t*)(src+0*stride))[0]= 
    ((uint32_t*)(src+1*stride))[0]= 
    ((uint32_t*)(src+2*stride))[0]= 
    ((uint32_t*)(src+3*stride))[0]= dc* 0x01010101; 
}

static void pred4x4_left_dc_c(uint8_t *src, uint8_t *topright, int stride){
    const int dc= (  src[-1+0*stride] + src[-1+1*stride] + src[-1+2*stride] + src[-1+3*stride] + 2) >>2;
    
    ((uint32_t*)(src+0*stride))[0]= 
    ((uint32_t*)(src+1*stride))[0]= 
    ((uint32_t*)(src+2*stride))[0]= 
    ((uint32_t*)(src+3*stride))[0]= dc* 0x01010101; 
}

static void pred4x4_top_dc_c(uint8_t *src, uint8_t *topright, int stride){
    const int dc= (  src[-stride] + src[1-stride] + src[2-stride] + src[3-stride] + 2) >>2;
    
    ((uint32_t*)(src+0*stride))[0]= 
    ((uint32_t*)(src+1*stride))[0]= 
    ((uint32_t*)(src+2*stride))[0]= 
    ((uint32_t*)(src+3*stride))[0]= dc* 0x01010101; 
}

static void pred4x4_128_dc_c(uint8_t *src, uint8_t *topright, int stride){
    ((uint32_t*)(src+0*stride))[0]= 
    ((uint32_t*)(src+1*stride))[0]= 
    ((uint32_t*)(src+2*stride))[0]= 
    ((uint32_t*)(src+3*stride))[0]= 128U*0x01010101U;
}


#define LOAD_TOP_RIGHT_EDGE\
    const int t4= topright[0];\
    const int t5= topright[1];\
    const int t6= topright[2];\
    const int t7= topright[3];\

#define LOAD_LEFT_EDGE\
    const int l0= src[-1+0*stride];\
    const int l1= src[-1+1*stride];\
    const int l2= src[-1+2*stride];\
    const int l3= src[-1+3*stride];\

#define LOAD_TOP_EDGE\
    const int t0= src[ 0-1*stride];\
    const int t1= src[ 1-1*stride];\
    const int t2= src[ 2-1*stride];\
    const int t3= src[ 3-1*stride];\

static void pred4x4_down_right_c(uint8_t *src, uint8_t *topright, int stride){
    const int lt= src[-1-1*stride];
    LOAD_TOP_EDGE
    LOAD_LEFT_EDGE

    src[0+3*stride]=(l3 + 2*l2 + l1 + 2)>>2; 
    src[0+2*stride]=
    src[1+3*stride]=(l2 + 2*l1 + l0 + 2)>>2; 
    src[0+1*stride]=
    src[1+2*stride]=
    src[2+3*stride]=(l1 + 2*l0 + lt + 2)>>2; 
    src[0+0*stride]=
    src[1+1*stride]=
    src[2+2*stride]=
    src[3+3*stride]=(l0 + 2*lt + t0 + 2)>>2; 
    src[1+0*stride]=
    src[2+1*stride]=
    src[3+2*stride]=(lt + 2*t0 + t1 + 2)>>2;
    src[2+0*stride]=
    src[3+1*stride]=(t0 + 2*t1 + t2 + 2)>>2;
    src[3+0*stride]=(t1 + 2*t2 + t3 + 2)>>2;
}

static void pred4x4_down_left_c(uint8_t *src, uint8_t *topright, int stride){
    LOAD_TOP_EDGE    
    LOAD_TOP_RIGHT_EDGE    
//    LOAD_LEFT_EDGE    

    src[0+0*stride]=(t0 + t2 + 2*t1 + 2)>>2;
    src[1+0*stride]=
    src[0+1*stride]=(t1 + t3 + 2*t2 + 2)>>2;
    src[2+0*stride]=
    src[1+1*stride]=
    src[0+2*stride]=(t2 + t4 + 2*t3 + 2)>>2;
    src[3+0*stride]=
    src[2+1*stride]=
    src[1+2*stride]=
    src[0+3*stride]=(t3 + t5 + 2*t4 + 2)>>2;
    src[3+1*stride]=
    src[2+2*stride]=
    src[1+3*stride]=(t4 + t6 + 2*t5 + 2)>>2;
    src[3+2*stride]=
    src[2+3*stride]=(t5 + t7 + 2*t6 + 2)>>2;
    src[3+3*stride]=(t6 + 3*t7 + 2)>>2;
}

static void pred4x4_vertical_right_c(uint8_t *src, uint8_t *topright, int stride){
    const int lt= src[-1-1*stride];
    LOAD_TOP_EDGE    
    LOAD_LEFT_EDGE    
    const __attribute__((unused)) int unu= l3;

    src[0+0*stride]=
    src[1+2*stride]=(lt + t0 + 1)>>1;
    src[1+0*stride]=
    src[2+2*stride]=(t0 + t1 + 1)>>1;
    src[2+0*stride]=
    src[3+2*stride]=(t1 + t2 + 1)>>1;
    src[3+0*stride]=(t2 + t3 + 1)>>1;
    src[0+1*stride]=
    src[1+3*stride]=(l0 + 2*lt + t0 + 2)>>2;
    src[1+1*stride]=
    src[2+3*stride]=(lt + 2*t0 + t1 + 2)>>2;
    src[2+1*stride]=
    src[3+3*stride]=(t0 + 2*t1 + t2 + 2)>>2;
    src[3+1*stride]=(t1 + 2*t2 + t3 + 2)>>2;
    src[0+2*stride]=(lt + 2*l0 + l1 + 2)>>2;
    src[0+3*stride]=(l0 + 2*l1 + l2 + 2)>>2;
}

static void pred4x4_vertical_left_c(uint8_t *src, uint8_t *topright, int stride){
    LOAD_TOP_EDGE    
    LOAD_TOP_RIGHT_EDGE    
    const __attribute__((unused)) int unu= t7;

    src[0+0*stride]=(t0 + t1 + 1)>>1;
    src[1+0*stride]=
    src[0+2*stride]=(t1 + t2 + 1)>>1;
    src[2+0*stride]=
    src[1+2*stride]=(t2 + t3 + 1)>>1;
    src[3+0*stride]=
    src[2+2*stride]=(t3 + t4+ 1)>>1;
    src[3+2*stride]=(t4 + t5+ 1)>>1;
    src[0+1*stride]=(t0 + 2*t1 + t2 + 2)>>2;
    src[1+1*stride]=
    src[0+3*stride]=(t1 + 2*t2 + t3 + 2)>>2;
    src[2+1*stride]=
    src[1+3*stride]=(t2 + 2*t3 + t4 + 2)>>2;
    src[3+1*stride]=
    src[2+3*stride]=(t3 + 2*t4 + t5 + 2)>>2;
    src[3+3*stride]=(t4 + 2*t5 + t6 + 2)>>2;
}

static void pred4x4_horizontal_up_c(uint8_t *src, uint8_t *topright, int stride){
    LOAD_LEFT_EDGE    

    src[0+0*stride]=(l0 + l1 + 1)>>1;
    src[1+0*stride]=(l0 + 2*l1 + l2 + 2)>>2;
    src[2+0*stride]=
    src[0+1*stride]=(l1 + l2 + 1)>>1;
    src[3+0*stride]=
    src[1+1*stride]=(l1 + 2*l2 + l3 + 2)>>2;
    src[2+1*stride]=
    src[0+2*stride]=(l2 + l3 + 1)>>1;
    src[3+1*stride]=
    src[1+2*stride]=(l2 + 2*l3 + l3 + 2)>>2;
    src[3+2*stride]=
    src[1+3*stride]=
    src[0+3*stride]=
    src[2+2*stride]=
    src[2+3*stride]=
    src[3+3*stride]=l3;
}
    
static void pred4x4_horizontal_down_c(uint8_t *src, uint8_t *topright, int stride){
    const int lt= src[-1-1*stride];
    LOAD_TOP_EDGE    
    LOAD_LEFT_EDGE    
    const __attribute__((unused)) int unu= t3;

    src[0+0*stride]=
    src[2+1*stride]=(lt + l0 + 1)>>1;
    src[1+0*stride]=
    src[3+1*stride]=(l0 + 2*lt + t0 + 2)>>2;
    src[2+0*stride]=(lt + 2*t0 + t1 + 2)>>2;
    src[3+0*stride]=(t0 + 2*t1 + t2 + 2)>>2;
    src[0+1*stride]=
    src[2+2*stride]=(l0 + l1 + 1)>>1;
    src[1+1*stride]=
    src[3+2*stride]=(lt + 2*l0 + l1 + 2)>>2;
    src[0+2*stride]=
    src[2+3*stride]=(l1 + l2+ 1)>>1;
    src[1+2*stride]=
    src[3+3*stride]=(l0 + 2*l1 + l2 + 2)>>2;
    src[0+3*stride]=(l2 + l3 + 1)>>1;
    src[1+3*stride]=(l1 + 2*l2 + l3 + 2)>>2;
}

static void pred16x16_vertical_c(uint8_t *src, int stride){
    int i;
    const uint32_t a= ((uint32_t*)(src-stride))[0];
    const uint32_t b= ((uint32_t*)(src-stride))[1];
    const uint32_t c= ((uint32_t*)(src-stride))[2];
    const uint32_t d= ((uint32_t*)(src-stride))[3];
    
    for(i=0; i<16; i++){
        ((uint32_t*)(src+i*stride))[0]= a;
        ((uint32_t*)(src+i*stride))[1]= b;
        ((uint32_t*)(src+i*stride))[2]= c;
        ((uint32_t*)(src+i*stride))[3]= d;
    }
}

static void pred16x16_horizontal_c(uint8_t *src, int stride){
    int i;

    for(i=0; i<16; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]=
        ((uint32_t*)(src+i*stride))[2]=
        ((uint32_t*)(src+i*stride))[3]= src[-1+i*stride]*0x01010101;
    }
}

static void pred16x16_dc_c(uint8_t *src, int stride){
    int i, dc=0;

    for(i=0;i<16; i++){
        dc+= src[-1+i*stride];
    }
    
    for(i=0;i<16; i++){
        dc+= src[i-stride];
    }

    dc= 0x01010101*((dc + 16)>>5);

    for(i=0; i<16; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]=
        ((uint32_t*)(src+i*stride))[2]=
        ((uint32_t*)(src+i*stride))[3]= dc;
    }
}

static void pred16x16_left_dc_c(uint8_t *src, int stride){
    int i, dc=0;

    for(i=0;i<16; i++){
        dc+= src[-1+i*stride];
    }
    
    dc= 0x01010101*((dc + 8)>>4);

    for(i=0; i<16; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]=
        ((uint32_t*)(src+i*stride))[2]=
        ((uint32_t*)(src+i*stride))[3]= dc;
    }
}

static void pred16x16_top_dc_c(uint8_t *src, int stride){
    int i, dc=0;

    for(i=0;i<16; i++){
        dc+= src[i-stride];
    }
    dc= 0x01010101*((dc + 8)>>4);

    for(i=0; i<16; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]=
        ((uint32_t*)(src+i*stride))[2]=
        ((uint32_t*)(src+i*stride))[3]= dc;
    }
}

static void pred16x16_128_dc_c(uint8_t *src, int stride){
    int i;

    for(i=0; i<16; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]=
        ((uint32_t*)(src+i*stride))[2]=
        ((uint32_t*)(src+i*stride))[3]= 0x01010101U*128U;
    }
}

static inline void pred16x16_plane_compat_c(uint8_t *src, int stride, const int svq3){
  int i, j, k;
  int a;
  uint8_t *cm = cropTbl + MAX_NEG_CROP;
  const uint8_t * const src0 = src+7-stride;
  const uint8_t *src1 = src+8*stride-1;
  const uint8_t *src2 = src1-2*stride;      // == src+6*stride-1;
  int H = src0[1] - src0[-1];
  int V = src1[0] - src2[ 0];
  for(k=2; k<=8; ++k) {
    src1 += stride; src2 -= stride;
    H += k*(src0[k] - src0[-k]);
    V += k*(src1[0] - src2[ 0]);
  }
  if(svq3){
    H = ( 5*(H/4) ) / 16;
    V = ( 5*(V/4) ) / 16;

    /* required for 100% accuracy */
    i = H; H = V; V = i;
  }else{
    H = ( 5*H+32 ) >> 6;
    V = ( 5*V+32 ) >> 6;
  }

  a = 16*(src1[0] + src2[16] + 1) - 7*(V+H);
  for(j=16; j>0; --j) {
    int b = a;
    a += V;
    for(i=-16; i<0; i+=4) {
      src[16+i] = cm[ (b    ) >> 5 ];
      src[17+i] = cm[ (b+  H) >> 5 ];
      src[18+i] = cm[ (b+2*H) >> 5 ];
      src[19+i] = cm[ (b+3*H) >> 5 ];
      b += 4*H;
    }
    src += stride;
  }
}

static void pred16x16_plane_c(uint8_t *src, int stride){
    pred16x16_plane_compat_c(src, stride, 0);
}

static void pred8x8_vertical_c(uint8_t *src, int stride){
    int i;
    const uint32_t a= ((uint32_t*)(src-stride))[0];
    const uint32_t b= ((uint32_t*)(src-stride))[1];
    
    for(i=0; i<8; i++){
        ((uint32_t*)(src+i*stride))[0]= a;
        ((uint32_t*)(src+i*stride))[1]= b;
    }
}

static void pred8x8_horizontal_c(uint8_t *src, int stride){
    int i;

    for(i=0; i<8; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]= src[-1+i*stride]*0x01010101;
    }
}

static void pred8x8_128_dc_c(uint8_t *src, int stride){
    int i;

    for(i=0; i<4; i++){
        ((uint32_t*)(src+i*stride))[0]= 
        ((uint32_t*)(src+i*stride))[1]= 0x01010101U*128U;
    }
    for(i=4; i<8; i++){
        ((uint32_t*)(src+i*stride))[0]= 
        ((uint32_t*)(src+i*stride))[1]= 0x01010101U*128U;
    }
}

static void pred8x8_left_dc_c(uint8_t *src, int stride){
    int i;
    int dc0, dc2;

    dc0=dc2=0;
    for(i=0;i<4; i++){
        dc0+= src[-1+i*stride];
        dc2+= src[-1+(i+4)*stride];
    }
    dc0= 0x01010101*((dc0 + 2)>>2);
    dc2= 0x01010101*((dc2 + 2)>>2);

    for(i=0; i<4; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]= dc0;
    }
    for(i=4; i<8; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]= dc2;
    }
}

static void pred8x8_top_dc_c(uint8_t *src, int stride){
    int i;
    int dc0, dc1;

    dc0=dc1=0;
    for(i=0;i<4; i++){
        dc0+= src[i-stride];
        dc1+= src[4+i-stride];
    }
    dc0= 0x01010101*((dc0 + 2)>>2);
    dc1= 0x01010101*((dc1 + 2)>>2);

    for(i=0; i<4; i++){
        ((uint32_t*)(src+i*stride))[0]= dc0;
        ((uint32_t*)(src+i*stride))[1]= dc1;
    }
    for(i=4; i<8; i++){
        ((uint32_t*)(src+i*stride))[0]= dc0;
        ((uint32_t*)(src+i*stride))[1]= dc1;
    }
}


static void pred8x8_dc_c(uint8_t *src, int stride){
    int i;
    int dc0, dc1, dc2, dc3;

    dc0=dc1=dc2=0;
    for(i=0;i<4; i++){
        dc0+= src[-1+i*stride] + src[i-stride];
        dc1+= src[4+i-stride];
        dc2+= src[-1+(i+4)*stride];
    }
    dc3= 0x01010101*((dc1 + dc2 + 4)>>3);
    dc0= 0x01010101*((dc0 + 4)>>3);
    dc1= 0x01010101*((dc1 + 2)>>2);
    dc2= 0x01010101*((dc2 + 2)>>2);

    for(i=0; i<4; i++){
        ((uint32_t*)(src+i*stride))[0]= dc0;
        ((uint32_t*)(src+i*stride))[1]= dc1;
    }
    for(i=4; i<8; i++){
        ((uint32_t*)(src+i*stride))[0]= dc2;
        ((uint32_t*)(src+i*stride))[1]= dc3;
    }
}

static void pred8x8_plane_c(uint8_t *src, int stride){
  int j, k;
  int a;
  uint8_t *cm = cropTbl + MAX_NEG_CROP;
  const uint8_t * const src0 = src+3-stride;
  const uint8_t *src1 = src+4*stride-1;
  const uint8_t *src2 = src1-2*stride;      // == src+2*stride-1;
  int H = src0[1] - src0[-1];
  int V = src1[0] - src2[ 0];
  for(k=2; k<=4; ++k) {
    src1 += stride; src2 -= stride;
    H += k*(src0[k] - src0[-k]);
    V += k*(src1[0] - src2[ 0]);
  }
  H = ( 17*H+16 ) >> 5;
  V = ( 17*V+16 ) >> 5;

  a = 16*(src1[0] + src2[8]+1) - 3*(V+H);
  for(j=8; j>0; --j) {
    int b = a;
    a += V;
    src[0] = cm[ (b    ) >> 5 ];
    src[1] = cm[ (b+  H) >> 5 ];
    src[2] = cm[ (b+2*H) >> 5 ];
    src[3] = cm[ (b+3*H) >> 5 ];
    src[4] = cm[ (b+4*H) >> 5 ];
    src[5] = cm[ (b+5*H) >> 5 ];
    src[6] = cm[ (b+6*H) >> 5 ];
    src[7] = cm[ (b+7*H) >> 5 ];
    src += stride;
  }
}

static inline void mc_dir_part(H264Context *h, Picture *pic, int n, int square, int chroma_height, int delta, int list,
                           uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                           int src_x_offset, int src_y_offset,
                           qpel_mc_func *qpix_op, h264_chroma_mc_func chroma_op){
    MpegEncContext * const s = &h->s;
    const int mx= h->mv_cache[list][ scan8[n] ][0] + src_x_offset*8;
    const int my= h->mv_cache[list][ scan8[n] ][1] + src_y_offset*8;
    const int luma_xy= (mx&3) + ((my&3)<<2);
    uint8_t * src_y = pic->data[0] + (mx>>2) + (my>>2)*s->linesize;
    uint8_t * src_cb= pic->data[1] + (mx>>3) + (my>>3)*s->uvlinesize;
    uint8_t * src_cr= pic->data[2] + (mx>>3) + (my>>3)*s->uvlinesize;
    int extra_width= (s->flags&CODEC_FLAG_EMU_EDGE) ? 0 : 16; //FIXME increase edge?, IMHO not worth it
    int extra_height= extra_width;
    int emu=0;
    const int full_mx= mx>>2;
    const int full_my= my>>2;
    
    assert(pic->data[0]);
    
    if(mx&7) extra_width -= 3;
    if(my&7) extra_height -= 3;
    
    if(   full_mx < 0-extra_width 
       || full_my < 0-extra_height 
       || full_mx + 16/*FIXME*/ > s->width + extra_width 
       || full_my + 16/*FIXME*/ > s->height + extra_height){
        ff_emulated_edge_mc(s->edge_emu_buffer, src_y - 2 - 2*s->linesize, s->linesize, 16+5, 16+5/*FIXME*/, full_mx-2, full_my-2, s->width, s->height);
            src_y= s->edge_emu_buffer + 2 + 2*s->linesize;
        emu=1;
    }
    
    qpix_op[luma_xy](dest_y, src_y, s->linesize); //FIXME try variable height perhaps?
    if(!square){
        qpix_op[luma_xy](dest_y + delta, src_y + delta, s->linesize);
    }
    
    if(s->flags&CODEC_FLAG_GRAY) return;
    
    if(emu){
        ff_emulated_edge_mc(s->edge_emu_buffer, src_cb, s->uvlinesize, 9, 9/*FIXME*/, (mx>>3), (my>>3), s->width>>1, s->height>>1);
            src_cb= s->edge_emu_buffer;
    }
    chroma_op(dest_cb, src_cb, s->uvlinesize, chroma_height, mx&7, my&7);

    if(emu){
        ff_emulated_edge_mc(s->edge_emu_buffer, src_cr, s->uvlinesize, 9, 9/*FIXME*/, (mx>>3), (my>>3), s->width>>1, s->height>>1);
            src_cr= s->edge_emu_buffer;
    }
    chroma_op(dest_cr, src_cr, s->uvlinesize, chroma_height, mx&7, my&7);
}

static inline void mc_part_std(H264Context *h, int n, int square, int chroma_height, int delta,
                           uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                           int x_offset, int y_offset,
                           qpel_mc_func *qpix_put, h264_chroma_mc_func chroma_put,
                           qpel_mc_func *qpix_avg, h264_chroma_mc_func chroma_avg,
                           int list0, int list1){
    MpegEncContext * const s = &h->s;
    qpel_mc_func *qpix_op=  qpix_put;
    h264_chroma_mc_func chroma_op= chroma_put;
    
    dest_y  += 2*x_offset + 2*y_offset*s->  linesize;
    dest_cb +=   x_offset +   y_offset*s->uvlinesize;
    dest_cr +=   x_offset +   y_offset*s->uvlinesize;
    x_offset += 8*s->mb_x;
    y_offset += 8*s->mb_y;
    
    if(list0){
        Picture *ref= &h->ref_list[0][ h->ref_cache[0][ scan8[n] ] ];
        mc_dir_part(h, ref, n, square, chroma_height, delta, 0,
                           dest_y, dest_cb, dest_cr, x_offset, y_offset,
                           qpix_op, chroma_op);

        qpix_op=  qpix_avg;
        chroma_op= chroma_avg;
    }

    if(list1){
        Picture *ref= &h->ref_list[1][ h->ref_cache[1][ scan8[n] ] ];
        mc_dir_part(h, ref, n, square, chroma_height, delta, 1,
                           dest_y, dest_cb, dest_cr, x_offset, y_offset,
                           qpix_op, chroma_op);
    }
}

static inline void mc_part_weighted(H264Context *h, int n, int square, int chroma_height, int delta,
                           uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                           int x_offset, int y_offset,
                           qpel_mc_func *qpix_put, h264_chroma_mc_func chroma_put,
                           h264_weight_func luma_weight_op, h264_weight_func chroma_weight_op,
                           h264_biweight_func luma_weight_avg, h264_biweight_func chroma_weight_avg,
                           int list0, int list1){
    MpegEncContext * const s = &h->s;

    dest_y  += 2*x_offset + 2*y_offset*s->  linesize;
    dest_cb +=   x_offset +   y_offset*s->uvlinesize;
    dest_cr +=   x_offset +   y_offset*s->uvlinesize;
    x_offset += 8*s->mb_x;
    y_offset += 8*s->mb_y;
    
    if(list0 && list1){
        /* don't optimize for luma-only case, since B-frames usually
         * use implicit weights => chroma too. */
        uint8_t *tmp_cb = s->obmc_scratchpad;
        uint8_t *tmp_cr = tmp_cb + 8*s->uvlinesize;
        uint8_t *tmp_y  = tmp_cr + 8*s->uvlinesize;
        int refn0 = h->ref_cache[0][ scan8[n] ];
        int refn1 = h->ref_cache[1][ scan8[n] ];

        mc_dir_part(h, &h->ref_list[0][refn0], n, square, chroma_height, delta, 0,
                    dest_y, dest_cb, dest_cr,
                    x_offset, y_offset, qpix_put, chroma_put);
        mc_dir_part(h, &h->ref_list[1][refn1], n, square, chroma_height, delta, 1,
                    tmp_y, tmp_cb, tmp_cr,
                    x_offset, y_offset, qpix_put, chroma_put);

        if(h->use_weight == 2){
            int weight0 = h->implicit_weight[refn0][refn1];
            int weight1 = 64 - weight0;
            luma_weight_avg(  dest_y,  tmp_y,  s->  linesize, 5, weight0, weight1, 0, 0);
            chroma_weight_avg(dest_cb, tmp_cb, s->uvlinesize, 5, weight0, weight1, 0, 0);
            chroma_weight_avg(dest_cr, tmp_cr, s->uvlinesize, 5, weight0, weight1, 0, 0);
        }else{
            luma_weight_avg(dest_y, tmp_y, s->linesize, h->luma_log2_weight_denom,
                            h->luma_weight[0][refn0], h->luma_weight[1][refn1], 
                            h->luma_offset[0][refn0], h->luma_offset[1][refn1]);
            chroma_weight_avg(dest_cb, tmp_cb, s->uvlinesize, h->chroma_log2_weight_denom,
                            h->chroma_weight[0][refn0][0], h->chroma_weight[1][refn1][0], 
                            h->chroma_offset[0][refn0][0], h->chroma_offset[1][refn1][0]);
            chroma_weight_avg(dest_cr, tmp_cr, s->uvlinesize, h->chroma_log2_weight_denom,
                            h->chroma_weight[0][refn0][1], h->chroma_weight[1][refn1][1], 
                            h->chroma_offset[0][refn0][1], h->chroma_offset[1][refn1][1]);
        }
    }else{
        int list = list1 ? 1 : 0;
        int refn = h->ref_cache[list][ scan8[n] ];
        Picture *ref= &h->ref_list[list][refn];
        mc_dir_part(h, ref, n, square, chroma_height, delta, list,
                    dest_y, dest_cb, dest_cr, x_offset, y_offset,
                    qpix_put, chroma_put);

        luma_weight_op(dest_y, s->linesize, h->luma_log2_weight_denom,
                       h->luma_weight[list][refn], h->luma_offset[list][refn]);
        if(h->use_weight_chroma){
            chroma_weight_op(dest_cb, s->uvlinesize, h->chroma_log2_weight_denom,
                             h->chroma_weight[list][refn][0], h->chroma_offset[list][refn][0]);
            chroma_weight_op(dest_cr, s->uvlinesize, h->chroma_log2_weight_denom,
                             h->chroma_weight[list][refn][1], h->chroma_offset[list][refn][1]);
        }
    }
}

static inline void mc_part(H264Context *h, int n, int square, int chroma_height, int delta,
                           uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                           int x_offset, int y_offset,
                           qpel_mc_func *qpix_put, h264_chroma_mc_func chroma_put,
                           qpel_mc_func *qpix_avg, h264_chroma_mc_func chroma_avg,
                           h264_weight_func *weight_op, h264_biweight_func *weight_avg, 
                           int list0, int list1){
    if((h->use_weight==2 && list0 && list1
        && (h->implicit_weight[ h->ref_cache[0][scan8[n]] ][ h->ref_cache[1][scan8[n]] ] != 32))
       || h->use_weight==1)
        mc_part_weighted(h, n, square, chroma_height, delta, dest_y, dest_cb, dest_cr,
                         x_offset, y_offset, qpix_put, chroma_put,
                         weight_op[0], weight_op[3], weight_avg[0], weight_avg[3], list0, list1);
    else
        mc_part_std(h, n, square, chroma_height, delta, dest_y, dest_cb, dest_cr,
                    x_offset, y_offset, qpix_put, chroma_put, qpix_avg, chroma_avg, list0, list1);
}

static void hl_motion(H264Context *h, uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                      qpel_mc_func (*qpix_put)[16], h264_chroma_mc_func (*chroma_put),
                      qpel_mc_func (*qpix_avg)[16], h264_chroma_mc_func (*chroma_avg),
                      h264_weight_func *weight_op, h264_biweight_func *weight_avg){
    MpegEncContext * const s = &h->s;
    const int mb_xy= s->mb_x + s->mb_y*s->mb_stride;
    const int mb_type= s->current_picture.mb_type[mb_xy];
    
    assert(IS_INTER(mb_type));
    
    if(IS_16X16(mb_type)){
        mc_part(h, 0, 1, 8, 0, dest_y, dest_cb, dest_cr, 0, 0,
                qpix_put[0], chroma_put[0], qpix_avg[0], chroma_avg[0],
                &weight_op[0], &weight_avg[0],
                IS_DIR(mb_type, 0, 0), IS_DIR(mb_type, 0, 1));
    }else if(IS_16X8(mb_type)){
        mc_part(h, 0, 0, 4, 8, dest_y, dest_cb, dest_cr, 0, 0,
                qpix_put[1], chroma_put[0], qpix_avg[1], chroma_avg[0],
                &weight_op[1], &weight_avg[1],
                IS_DIR(mb_type, 0, 0), IS_DIR(mb_type, 0, 1));
        mc_part(h, 8, 0, 4, 8, dest_y, dest_cb, dest_cr, 0, 4,
                qpix_put[1], chroma_put[0], qpix_avg[1], chroma_avg[0],
                &weight_op[1], &weight_avg[1],
                IS_DIR(mb_type, 1, 0), IS_DIR(mb_type, 1, 1));
    }else if(IS_8X16(mb_type)){
        mc_part(h, 0, 0, 8, 8*s->linesize, dest_y, dest_cb, dest_cr, 0, 0,
                qpix_put[1], chroma_put[1], qpix_avg[1], chroma_avg[1],
                &weight_op[2], &weight_avg[2],
                IS_DIR(mb_type, 0, 0), IS_DIR(mb_type, 0, 1));
        mc_part(h, 4, 0, 8, 8*s->linesize, dest_y, dest_cb, dest_cr, 4, 0,
                qpix_put[1], chroma_put[1], qpix_avg[1], chroma_avg[1],
                &weight_op[2], &weight_avg[2],
                IS_DIR(mb_type, 1, 0), IS_DIR(mb_type, 1, 1));
    }else{
        int i;
        
        assert(IS_8X8(mb_type));

        for(i=0; i<4; i++){
            const int sub_mb_type= h->sub_mb_type[i];
            const int n= 4*i;
            int x_offset= (i&1)<<2;
            int y_offset= (i&2)<<1;

            if(IS_SUB_8X8(sub_mb_type)){
                mc_part(h, n, 1, 4, 0, dest_y, dest_cb, dest_cr, x_offset, y_offset,
                    qpix_put[1], chroma_put[1], qpix_avg[1], chroma_avg[1],
                    &weight_op[3], &weight_avg[3],
                    IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
            }else if(IS_SUB_8X4(sub_mb_type)){
                mc_part(h, n  , 0, 2, 4, dest_y, dest_cb, dest_cr, x_offset, y_offset,
                    qpix_put[2], chroma_put[1], qpix_avg[2], chroma_avg[1],
                    &weight_op[4], &weight_avg[4],
                    IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
                mc_part(h, n+2, 0, 2, 4, dest_y, dest_cb, dest_cr, x_offset, y_offset+2,
                    qpix_put[2], chroma_put[1], qpix_avg[2], chroma_avg[1],
                    &weight_op[4], &weight_avg[4],
                    IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
            }else if(IS_SUB_4X8(sub_mb_type)){
                mc_part(h, n  , 0, 4, 4*s->linesize, dest_y, dest_cb, dest_cr, x_offset, y_offset,
                    qpix_put[2], chroma_put[2], qpix_avg[2], chroma_avg[2],
                    &weight_op[5], &weight_avg[5],
                    IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
                mc_part(h, n+1, 0, 4, 4*s->linesize, dest_y, dest_cb, dest_cr, x_offset+2, y_offset,
                    qpix_put[2], chroma_put[2], qpix_avg[2], chroma_avg[2],
                    &weight_op[5], &weight_avg[5],
                    IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
            }else{
                int j;
                assert(IS_SUB_4X4(sub_mb_type));
                for(j=0; j<4; j++){
                    int sub_x_offset= x_offset + 2*(j&1);
                    int sub_y_offset= y_offset +   (j&2);
                    mc_part(h, n+j, 1, 2, 0, dest_y, dest_cb, dest_cr, sub_x_offset, sub_y_offset,
                        qpix_put[2], chroma_put[2], qpix_avg[2], chroma_avg[2],
                        &weight_op[6], &weight_avg[6],
                        IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
                }
            }
        }
    }
}

static void decode_init_vlc(H264Context *h){
    static int done = 0;

    if (!done) {
        int i;
        done = 1;

        init_vlc(&chroma_dc_coeff_token_vlc, CHROMA_DC_COEFF_TOKEN_VLC_BITS, 4*5, 
                 &chroma_dc_coeff_token_len [0], 1, 1,
                 &chroma_dc_coeff_token_bits[0], 1, 1, 1);

        for(i=0; i<4; i++){
            init_vlc(&coeff_token_vlc[i], COEFF_TOKEN_VLC_BITS, 4*17, 
                     &coeff_token_len [i][0], 1, 1,
                     &coeff_token_bits[i][0], 1, 1, 1);
        }

        for(i=0; i<3; i++){
            init_vlc(&chroma_dc_total_zeros_vlc[i], CHROMA_DC_TOTAL_ZEROS_VLC_BITS, 4,
                     &chroma_dc_total_zeros_len [i][0], 1, 1,
                     &chroma_dc_total_zeros_bits[i][0], 1, 1, 1);
        }
        for(i=0; i<15; i++){
            init_vlc(&total_zeros_vlc[i], TOTAL_ZEROS_VLC_BITS, 16, 
                     &total_zeros_len [i][0], 1, 1,
                     &total_zeros_bits[i][0], 1, 1, 1);
        }

        for(i=0; i<6; i++){
            init_vlc(&run_vlc[i], RUN_VLC_BITS, 7, 
                     &run_len [i][0], 1, 1,
                     &run_bits[i][0], 1, 1, 1);
        }
        init_vlc(&run7_vlc, RUN7_VLC_BITS, 16, 
                 &run_len [6][0], 1, 1,
                 &run_bits[6][0], 1, 1, 1);
    }
}

/**
 * Sets the intra prediction function pointers.
 */
static void init_pred_ptrs(H264Context *h){
//    MpegEncContext * const s = &h->s;

    h->pred4x4[VERT_PRED           ]= pred4x4_vertical_c;
    h->pred4x4[HOR_PRED            ]= pred4x4_horizontal_c;
    h->pred4x4[DC_PRED             ]= pred4x4_dc_c;
    h->pred4x4[DIAG_DOWN_LEFT_PRED ]= pred4x4_down_left_c;
    h->pred4x4[DIAG_DOWN_RIGHT_PRED]= pred4x4_down_right_c;
    h->pred4x4[VERT_RIGHT_PRED     ]= pred4x4_vertical_right_c;
    h->pred4x4[HOR_DOWN_PRED       ]= pred4x4_horizontal_down_c;
    h->pred4x4[VERT_LEFT_PRED      ]= pred4x4_vertical_left_c;
    h->pred4x4[HOR_UP_PRED         ]= pred4x4_horizontal_up_c;
    h->pred4x4[LEFT_DC_PRED        ]= pred4x4_left_dc_c;
    h->pred4x4[TOP_DC_PRED         ]= pred4x4_top_dc_c;
    h->pred4x4[DC_128_PRED         ]= pred4x4_128_dc_c;

    h->pred8x8[DC_PRED8x8     ]= pred8x8_dc_c;
    h->pred8x8[VERT_PRED8x8   ]= pred8x8_vertical_c;
    h->pred8x8[HOR_PRED8x8    ]= pred8x8_horizontal_c;
    h->pred8x8[PLANE_PRED8x8  ]= pred8x8_plane_c;
    h->pred8x8[LEFT_DC_PRED8x8]= pred8x8_left_dc_c;
    h->pred8x8[TOP_DC_PRED8x8 ]= pred8x8_top_dc_c;
    h->pred8x8[DC_128_PRED8x8 ]= pred8x8_128_dc_c;

    h->pred16x16[DC_PRED8x8     ]= pred16x16_dc_c;
    h->pred16x16[VERT_PRED8x8   ]= pred16x16_vertical_c;
    h->pred16x16[HOR_PRED8x8    ]= pred16x16_horizontal_c;
    h->pred16x16[PLANE_PRED8x8  ]= pred16x16_plane_c;
    h->pred16x16[LEFT_DC_PRED8x8]= pred16x16_left_dc_c;
    h->pred16x16[TOP_DC_PRED8x8 ]= pred16x16_top_dc_c;
    h->pred16x16[DC_128_PRED8x8 ]= pred16x16_128_dc_c;
}

static void free_tables(H264Context *h){
    av_freep(&h->intra4x4_pred_mode);
    av_freep(&h->chroma_pred_mode_table);
    av_freep(&h->cbp_table);
    av_freep(&h->mvd_table[0]);
    av_freep(&h->mvd_table[1]);
    av_freep(&h->direct_table);
    av_freep(&h->non_zero_count);
    av_freep(&h->slice_table_base);
    av_freep(&h->top_border);
    h->slice_table= NULL;

    av_freep(&h->mb2b_xy);
    av_freep(&h->mb2b8_xy);

    av_freep(&h->s.obmc_scratchpad);
}

/**
 * allocates tables.
 * needs widzh/height
 */
static int alloc_tables(H264Context *h){
    MpegEncContext * const s = &h->s;
    const int big_mb_num= s->mb_stride * (s->mb_height+1);
    int x,y;

    CHECKED_ALLOCZ(h->intra4x4_pred_mode, big_mb_num * 8  * sizeof(uint8_t))

    CHECKED_ALLOCZ(h->non_zero_count    , big_mb_num * 16 * sizeof(uint8_t))
    CHECKED_ALLOCZ(h->slice_table_base  , big_mb_num * sizeof(uint8_t))
    CHECKED_ALLOCZ(h->top_border       , s->mb_width * (16+8+8) * sizeof(uint8_t))
    CHECKED_ALLOCZ(h->cbp_table, big_mb_num * sizeof(uint16_t))

    if( h->pps.cabac ) {
        CHECKED_ALLOCZ(h->chroma_pred_mode_table, big_mb_num * sizeof(uint8_t))
        CHECKED_ALLOCZ(h->mvd_table[0], 32*big_mb_num * sizeof(uint16_t));
        CHECKED_ALLOCZ(h->mvd_table[1], 32*big_mb_num * sizeof(uint16_t));
        CHECKED_ALLOCZ(h->direct_table, 32*big_mb_num * sizeof(uint8_t));
    }

    memset(h->slice_table_base, -1, big_mb_num  * sizeof(uint8_t));
    h->slice_table= h->slice_table_base + s->mb_stride + 1;

    CHECKED_ALLOCZ(h->mb2b_xy  , big_mb_num * sizeof(uint16_t));
    CHECKED_ALLOCZ(h->mb2b8_xy , big_mb_num * sizeof(uint16_t));
    for(y=0; y<s->mb_height; y++){
        for(x=0; x<s->mb_width; x++){
            const int mb_xy= x + y*s->mb_stride;
            const int b_xy = 4*x + 4*y*h->b_stride;
            const int b8_xy= 2*x + 2*y*h->b8_stride;
        
            h->mb2b_xy [mb_xy]= b_xy;
            h->mb2b8_xy[mb_xy]= b8_xy;
        }
    }

    s->obmc_scratchpad = NULL;

    return 0;
fail:
    free_tables(h);
    return -1;
}

static void common_init(H264Context *h){
    MpegEncContext * const s = &h->s;

    s->width = s->avctx->width;
    s->height = s->avctx->height;
    s->codec_id= s->avctx->codec->id;
    
    init_pred_ptrs(h);

    s->unrestricted_mv=1;
    s->decode=1; //FIXME
}

static int decode_init(AVCodecContext *avctx){
    H264Context *h= avctx->priv_data;
    MpegEncContext * const s = &h->s;

    MPV_decode_defaults(s);
    
    s->avctx = avctx;
    common_init(h);

    s->out_format = FMT_H264;
    s->workaround_bugs= avctx->workaround_bugs;

    // set defaults
//    s->decode_mb= ff_h263_decode_mb;
    s->low_delay= 1;
    avctx->pix_fmt= PIX_FMT_YUV420P;

    decode_init_vlc(h);
    
    if(avctx->codec_tag != 0x31637661 && avctx->codec_tag != 0x31435641) // avc1
        h->is_avc = 0;
    else {
        if((avctx->extradata_size == 0) || (avctx->extradata == NULL)) {
            av_log(avctx, AV_LOG_ERROR, "AVC codec requires avcC data\n");
            return -1;
        }
        h->is_avc = 1;
        h->got_avcC = 0;
    }

    return 0;
}

static void frame_start(H264Context *h){
    MpegEncContext * const s = &h->s;
    int i;

    MPV_frame_start(s, s->avctx);
    ff_er_frame_start(s);
    h->mmco_index=0;

    assert(s->linesize && s->uvlinesize);

    for(i=0; i<16; i++){
        h->block_offset[i]= 4*((scan8[i] - scan8[0])&7) + 4*s->linesize*((scan8[i] - scan8[0])>>3);
        h->chroma_subblock_offset[i]= 2*((scan8[i] - scan8[0])&7) + 2*s->uvlinesize*((scan8[i] - scan8[0])>>3);
    }
    for(i=0; i<4; i++){
        h->block_offset[16+i]=
        h->block_offset[20+i]= 4*((scan8[i] - scan8[0])&7) + 4*s->uvlinesize*((scan8[i] - scan8[0])>>3);
    }

    /* can't be in alloc_tables because linesize isn't known there.
     * FIXME: redo bipred weight to not require extra buffer? */
    if(!s->obmc_scratchpad)
        s->obmc_scratchpad = av_malloc(16*s->linesize + 2*8*s->uvlinesize);

//    s->decode= (s->flags&CODEC_FLAG_PSNR) || !s->encoding || s->current_picture.reference /*|| h->contains_intra*/ || 1;
}

static inline void backup_mb_border(H264Context *h, uint8_t *src_y, uint8_t *src_cb, uint8_t *src_cr, int linesize, int uvlinesize){
    MpegEncContext * const s = &h->s;
    int i;
    
    src_y  -=   linesize;
    src_cb -= uvlinesize;
    src_cr -= uvlinesize;

    h->left_border[0]= h->top_border[s->mb_x][15];
    for(i=1; i<17; i++){
        h->left_border[i]= src_y[15+i*  linesize];
    }
    
    *(uint64_t*)(h->top_border[s->mb_x]+0)= *(uint64_t*)(src_y +  16*linesize);
    *(uint64_t*)(h->top_border[s->mb_x]+8)= *(uint64_t*)(src_y +8+16*linesize);

    if(!(s->flags&CODEC_FLAG_GRAY)){
        h->left_border[17  ]= h->top_border[s->mb_x][16+7];
        h->left_border[17+9]= h->top_border[s->mb_x][24+7];
        for(i=1; i<9; i++){
            h->left_border[i+17  ]= src_cb[7+i*uvlinesize];
            h->left_border[i+17+9]= src_cr[7+i*uvlinesize];
        }
        *(uint64_t*)(h->top_border[s->mb_x]+16)= *(uint64_t*)(src_cb+8*uvlinesize);
        *(uint64_t*)(h->top_border[s->mb_x]+24)= *(uint64_t*)(src_cr+8*uvlinesize);
    }
}

static inline void xchg_mb_border(H264Context *h, uint8_t *src_y, uint8_t *src_cb, uint8_t *src_cr, int linesize, int uvlinesize, int xchg){
    MpegEncContext * const s = &h->s;
    int temp8, i;
    uint64_t temp64;
    int deblock_left = (s->mb_x > 0);
    int deblock_top  = (s->mb_y > 0);

    src_y  -=   linesize + 1;
    src_cb -= uvlinesize + 1;
    src_cr -= uvlinesize + 1;

#define XCHG(a,b,t,xchg)\
t= a;\
if(xchg)\
    a= b;\
b= t;

    if(deblock_left){
        for(i = !deblock_top; i<17; i++){
            XCHG(h->left_border[i     ], src_y [i*  linesize], temp8, xchg);
        }
    }

    if(deblock_top){
        XCHG(*(uint64_t*)(h->top_border[s->mb_x]+0), *(uint64_t*)(src_y +1), temp64, xchg);
        XCHG(*(uint64_t*)(h->top_border[s->mb_x]+8), *(uint64_t*)(src_y +9), temp64, 1);
    }

    if(!(s->flags&CODEC_FLAG_GRAY)){
        if(deblock_left){
            for(i = !deblock_top; i<9; i++){
                XCHG(h->left_border[i+17  ], src_cb[i*uvlinesize], temp8, xchg);
                XCHG(h->left_border[i+17+9], src_cr[i*uvlinesize], temp8, xchg);
            }
        }
        if(deblock_top){
            XCHG(*(uint64_t*)(h->top_border[s->mb_x]+16), *(uint64_t*)(src_cb+1), temp64, 1);
            XCHG(*(uint64_t*)(h->top_border[s->mb_x]+24), *(uint64_t*)(src_cr+1), temp64, 1);
        }
    }
}

static void hl_decode_mb(H264Context *h){
    MpegEncContext * const s = &h->s;
    const int mb_x= s->mb_x;
    const int mb_y= s->mb_y;
    const int mb_xy= mb_x + mb_y*s->mb_stride;
    const int mb_type= s->current_picture.mb_type[mb_xy];
    uint8_t  *dest_y, *dest_cb, *dest_cr;
    int linesize, uvlinesize /*dct_offset*/;
    int i;

    if(!s->decode)
        return;

    if(s->mb_skiped){
    }

    dest_y  = s->current_picture.data[0] + (mb_y * 16* s->linesize  ) + mb_x * 16;
    dest_cb = s->current_picture.data[1] + (mb_y * 8 * s->uvlinesize) + mb_x * 8;
    dest_cr = s->current_picture.data[2] + (mb_y * 8 * s->uvlinesize) + mb_x * 8;

    if (h->mb_field_decoding_flag) {
        linesize = s->linesize * 2;
        uvlinesize = s->uvlinesize * 2;
        if(mb_y&1){ //FIXME move out of this func?
            dest_y -= s->linesize*15;
            dest_cb-= s->linesize*7;
            dest_cr-= s->linesize*7;
        }
    } else {
        linesize = s->linesize;
        uvlinesize = s->uvlinesize;
//        dct_offset = s->linesize * 16;
    }

    if(IS_INTRA(mb_type)){
        if(h->deblocking_filter)
            xchg_mb_border(h, dest_y, dest_cb, dest_cr, linesize, uvlinesize, 1);

        if(!(s->flags&CODEC_FLAG_GRAY)){
            h->pred8x8[ h->chroma_pred_mode ](dest_cb, uvlinesize);
            h->pred8x8[ h->chroma_pred_mode ](dest_cr, uvlinesize);
        }

        if(IS_INTRA4x4(mb_type)){
            if(!s->encoding){
                for(i=0; i<16; i++){
                    uint8_t * const ptr= dest_y + h->block_offset[i];
                    uint8_t *topright;
                    const int dir= h->intra4x4_pred_mode_cache[ scan8[i] ];
                    int tr;

                    if(dir == DIAG_DOWN_LEFT_PRED || dir == VERT_LEFT_PRED){
                        const int topright_avail= (h->topright_samples_available<<i)&0x8000;
                        assert(mb_y || linesize <= h->block_offset[i]);
                        if(!topright_avail){
                            tr= ptr[3 - linesize]*0x01010101;
                            topright= (uint8_t*) &tr;
                        }else if(i==5 && h->deblocking_filter){
                            tr= *(uint32_t*)h->top_border[mb_x+1];
                            topright= (uint8_t*) &tr;
                        }else
                            topright= ptr + 4 - linesize;
                    }else
                        topright= NULL;

                    h->pred4x4[ dir ](ptr, topright, linesize);
                    if(h->non_zero_count_cache[ scan8[i] ]){
                        if(s->codec_id == CODEC_ID_H264)
                            s->dsp.h264_idct_add(ptr, h->mb + i*16, linesize);
                        else
                            svq3_add_idct_c(ptr, h->mb + i*16, linesize, s->qscale, 0);
                    }
                }
            }
        }else{
            h->pred16x16[ h->intra16x16_pred_mode ](dest_y , linesize);
            if(s->codec_id == CODEC_ID_H264)
                h264_luma_dc_dequant_idct_c(h->mb, s->qscale);
            else
                svq3_luma_dc_dequant_idct_c(h->mb, s->qscale);
        }
        if(h->deblocking_filter)
            xchg_mb_border(h, dest_y, dest_cb, dest_cr, linesize, uvlinesize, 0);
    }else if(s->codec_id == CODEC_ID_H264){
        hl_motion(h, dest_y, dest_cb, dest_cr,
                  s->dsp.put_h264_qpel_pixels_tab, s->dsp.put_h264_chroma_pixels_tab, 
                  s->dsp.avg_h264_qpel_pixels_tab, s->dsp.avg_h264_chroma_pixels_tab,
                  s->dsp.weight_h264_pixels_tab, s->dsp.biweight_h264_pixels_tab);
    }


    if(!IS_INTRA4x4(mb_type)){
        if(s->codec_id == CODEC_ID_H264){
            for(i=0; i<16; i++){
                if(h->non_zero_count_cache[ scan8[i] ] || h->mb[i*16]){ //FIXME benchmark weird rule, & below
                    uint8_t * const ptr= dest_y + h->block_offset[i];
                    s->dsp.h264_idct_add(ptr, h->mb + i*16, linesize);
                }
            }
        }else{
            for(i=0; i<16; i++){
                if(h->non_zero_count_cache[ scan8[i] ] || h->mb[i*16]){ //FIXME benchmark weird rule, & below
                    uint8_t * const ptr= dest_y + h->block_offset[i];
                    svq3_add_idct_c(ptr, h->mb + i*16, linesize, s->qscale, IS_INTRA(mb_type) ? 1 : 0);
                }
            }
        }
    }

    if(!(s->flags&CODEC_FLAG_GRAY)){
        chroma_dc_dequant_idct_c(h->mb + 16*16, h->chroma_qp);
        chroma_dc_dequant_idct_c(h->mb + 16*16+4*16, h->chroma_qp);
        if(s->codec_id == CODEC_ID_H264){
            for(i=16; i<16+4; i++){
                if(h->non_zero_count_cache[ scan8[i] ] || h->mb[i*16]){
                    uint8_t * const ptr= dest_cb + h->block_offset[i];
                    s->dsp.h264_idct_add(ptr, h->mb + i*16, uvlinesize);
                }
            }
            for(i=20; i<20+4; i++){
                if(h->non_zero_count_cache[ scan8[i] ] || h->mb[i*16]){
                    uint8_t * const ptr= dest_cr + h->block_offset[i];
                    s->dsp.h264_idct_add(ptr, h->mb + i*16, uvlinesize);
                }
            }
        }else{
            for(i=16; i<16+4; i++){
                if(h->non_zero_count_cache[ scan8[i] ] || h->mb[i*16]){
                    uint8_t * const ptr= dest_cb + h->block_offset[i];
                    svq3_add_idct_c(ptr, h->mb + i*16, uvlinesize, chroma_qp[s->qscale + 12] - 12, 2);
                }
            }
            for(i=20; i<20+4; i++){
                if(h->non_zero_count_cache[ scan8[i] ] || h->mb[i*16]){
                    uint8_t * const ptr= dest_cr + h->block_offset[i];
                    svq3_add_idct_c(ptr, h->mb + i*16, uvlinesize, chroma_qp[s->qscale + 12] - 12, 2);
                }
            }
        }
    }
    if(h->deblocking_filter) {
        backup_mb_border(h, dest_y, dest_cb, dest_cr, linesize, uvlinesize);
        filter_mb(h, mb_x, mb_y, dest_y, dest_cb, dest_cr);
    }
}

/**
 * fills the default_ref_list.
 */
static int fill_default_ref_list(H264Context *h){
    MpegEncContext * const s = &h->s;
    int i;
    Picture sorted_short_ref[16];
    
    if(h->slice_type==B_TYPE){
        int out_i;
        int limit= -1;

        for(out_i=0; out_i<h->short_ref_count; out_i++){
            int best_i=-1;
            int best_poc=INT_MAX;

            for(i=0; i<h->short_ref_count; i++){
                const int poc= h->short_ref[i]->poc;
                if(poc > limit && poc < best_poc){
                    best_poc= poc;
                    best_i= i;
                }
            }
            
            assert(best_i != -1);
            
            limit= best_poc;
            sorted_short_ref[out_i]= *h->short_ref[best_i];
        }
    }

    if(s->picture_structure == PICT_FRAME){
        if(h->slice_type==B_TYPE){
            const int current_poc= s->current_picture_ptr->poc;
            int list;

            for(list=0; list<2; list++){
                int index=0;

                for(i=0; i<h->short_ref_count && index < h->ref_count[list]; i++){
                    const int i2= list ? i : h->short_ref_count - i - 1;
                    const int poc= sorted_short_ref[i2].poc;
                    
                    if(sorted_short_ref[i2].reference != 3) continue; //FIXME refernce field shit

                    if((list==1 && poc > current_poc) || (list==0 && poc < current_poc)){
                        h->default_ref_list[list][index  ]= sorted_short_ref[i2];
                        h->default_ref_list[list][index++].pic_id= sorted_short_ref[i2].frame_num;
                    }
                }

                for(i=0; i<h->long_ref_count && index < h->ref_count[ list ]; i++){
                    if(h->long_ref[i]->reference != 3) continue;

                    h->default_ref_list[ list ][index  ]= *h->long_ref[i];
                    h->default_ref_list[ list ][index++].pic_id= i;;
                }
                
                if(h->long_ref_count > 1 && h->short_ref_count==0){
                    Picture temp= h->default_ref_list[1][0];
                    h->default_ref_list[1][0] = h->default_ref_list[1][1];
                    h->default_ref_list[1][0] = temp;
                }

                if(index < h->ref_count[ list ])
                    memset(&h->default_ref_list[list][index], 0, sizeof(Picture)*(h->ref_count[ list ] - index));
            }
        }else{
            int index=0;
            for(i=0; i<h->short_ref_count && index < h->ref_count[0]; i++){
                if(h->short_ref[i]->reference != 3) continue; //FIXME refernce field shit
                h->default_ref_list[0][index  ]= *h->short_ref[i];
                h->default_ref_list[0][index++].pic_id= h->short_ref[i]->frame_num;
            }
            for(i=0; i<h->long_ref_count && index < h->ref_count[0]; i++){
                if(h->long_ref[i]->reference != 3) continue;
                h->default_ref_list[0][index  ]= *h->long_ref[i];
                h->default_ref_list[0][index++].pic_id= i;;
            }
            if(index < h->ref_count[0])
                memset(&h->default_ref_list[0][index], 0, sizeof(Picture)*(h->ref_count[0] - index));
        }
    }else{ //FIELD
        if(h->slice_type==B_TYPE){
        }else{
            //FIXME second field balh
        }
    }
    return 0;
}

static int decode_ref_pic_list_reordering(H264Context *h){
    MpegEncContext * const s = &h->s;
    int list;
    
    if(h->slice_type==I_TYPE || h->slice_type==SI_TYPE) return 0; //FIXME move beofre func
    
    for(list=0; list<2; list++){
        memcpy(h->ref_list[list], h->default_ref_list[list], sizeof(Picture)*h->ref_count[list]);

        if(get_bits1(&s->gb)){
            int pred= h->curr_pic_num;
            int index;

            for(index=0; ; index++){
                int reordering_of_pic_nums_idc= get_ue_golomb(&s->gb);
                int pic_id;
                int i;
                
                if(reordering_of_pic_nums_idc==3) 
                    break;
                
                if(index >= h->ref_count[list]){
                    av_log(h->s.avctx, AV_LOG_ERROR, "reference count overflow\n");
                    return -1;
                }
                
                if(reordering_of_pic_nums_idc<3){
                    if(reordering_of_pic_nums_idc<2){
                        const int abs_diff_pic_num= get_ue_golomb(&s->gb) + 1;

                        if(abs_diff_pic_num >= h->max_pic_num){
                            av_log(h->s.avctx, AV_LOG_ERROR, "abs_diff_pic_num overflow\n");
                            return -1;
                        }

                        if(reordering_of_pic_nums_idc == 0) pred-= abs_diff_pic_num;
                        else                                pred+= abs_diff_pic_num;
                        pred &= h->max_pic_num - 1;
                    
                        for(i= h->ref_count[list]-1; i>=index; i--){
                            if(h->ref_list[list][i].pic_id == pred && h->ref_list[list][i].long_ref==0)
                                break;
                        }
                    }else{
                        pic_id= get_ue_golomb(&s->gb); //long_term_pic_idx

                        for(i= h->ref_count[list]-1; i>=index; i--){
                            if(h->ref_list[list][i].pic_id == pic_id && h->ref_list[list][i].long_ref==1)
                                break;
                        }
                    }

                    if(i < index){
                        av_log(h->s.avctx, AV_LOG_ERROR, "reference picture missing during reorder\n");
                        memset(&h->ref_list[list][index], 0, sizeof(Picture)); //FIXME
                    }else if(i > index){
                        Picture tmp= h->ref_list[list][i];
                        for(; i>index; i--){
                            h->ref_list[list][i]= h->ref_list[list][i-1];
                        }
                        h->ref_list[list][index]= tmp;
                    }
                }else{
                    av_log(h->s.avctx, AV_LOG_ERROR, "illegal reordering_of_pic_nums_idc\n");
                    return -1;
                }
            }
        }

        if(h->slice_type!=B_TYPE) break;
    }
    
    if(h->slice_type==B_TYPE && !h->direct_spatial_mv_pred)
        direct_dist_scale_factor(h);
    return 0;    
}

static int pred_weight_table(H264Context *h){
    MpegEncContext * const s = &h->s;
    int list, i;
    int luma_def, chroma_def;
    
    h->use_weight= 0;
    h->use_weight_chroma= 0;
    h->luma_log2_weight_denom= get_ue_golomb(&s->gb);
    h->chroma_log2_weight_denom= get_ue_golomb(&s->gb);
    luma_def = 1<<h->luma_log2_weight_denom;
    chroma_def = 1<<h->chroma_log2_weight_denom;

    for(list=0; list<2; list++){
        for(i=0; i<h->ref_count[list]; i++){
            int luma_weight_flag, chroma_weight_flag;
            
            luma_weight_flag= get_bits1(&s->gb);
            if(luma_weight_flag){
                h->luma_weight[list][i]= get_se_golomb(&s->gb);
                h->luma_offset[list][i]= get_se_golomb(&s->gb);
                if(   h->luma_weight[list][i] != luma_def
                   || h->luma_offset[list][i] != 0)
                    h->use_weight= 1;
            }else{
                h->luma_weight[list][i]= luma_def;
                h->luma_offset[list][i]= 0;
            }

            chroma_weight_flag= get_bits1(&s->gb);
            if(chroma_weight_flag){
                int j;
                for(j=0; j<2; j++){
                    h->chroma_weight[list][i][j]= get_se_golomb(&s->gb);
                    h->chroma_offset[list][i][j]= get_se_golomb(&s->gb);
                    if(   h->chroma_weight[list][i][j] != chroma_def
                       || h->chroma_offset[list][i][j] != 0)
                        h->use_weight_chroma= 1;
                }
            }else{
                int j;
                for(j=0; j<2; j++){
                    h->chroma_weight[list][i][j]= chroma_def;
                    h->chroma_offset[list][i][j]= 0;
                }
            }
        }
        if(h->slice_type != B_TYPE) break;
    }
    h->use_weight= h->use_weight || h->use_weight_chroma;
    return 0;
}

static void implicit_weight_table(H264Context *h){
    MpegEncContext * const s = &h->s;
    int list, i;
    int ref0, ref1;
    int cur_poc = s->current_picture_ptr->poc;

    if(   h->ref_count[0] == 1 && h->ref_count[1] == 1
       && h->ref_list[0][0].poc + h->ref_list[1][0].poc == 2*cur_poc){
        h->use_weight= 0;
        h->use_weight_chroma= 0;
        return;
    }

    h->use_weight= 2;
    h->use_weight_chroma= 2;
    h->luma_log2_weight_denom= 5;
    h->chroma_log2_weight_denom= 5;

    /* FIXME: MBAFF */
    for(ref0=0; ref0 < h->ref_count[0]; ref0++){
        int poc0 = h->ref_list[0][ref0].poc;
        for(ref1=0; ref1 < h->ref_count[1]; ref1++){
            int poc1 = h->ref_list[0][ref1].poc;
            int td = clip(poc1 - poc0, -128, 127);
            if(td){
                int tb = clip(cur_poc - poc0, -128, 127);
                int tx = (16384 + (ABS(td) >> 1)) / td;
                int dist_scale_factor = clip((tb*tx + 32) >> 6, -1024, 1023) >> 2;
                if(dist_scale_factor < -64 || dist_scale_factor > 128)
                    h->implicit_weight[ref0][ref1] = 32;
                else
                    h->implicit_weight[ref0][ref1] = 64 - dist_scale_factor;
            }else
                h->implicit_weight[ref0][ref1] = 32;
        }
    }
}

/**
 * instantaneous decoder refresh.
 */
static void idr(H264Context *h){
    int i,j;

#define CHECK_DELAY(pic) \
    for(j = 0; h->delayed_pic[j]; j++) \
        if(pic == h->delayed_pic[j]){ \
            pic->reference=1; \
            break; \
        }

    for(i=0; i<h->long_ref_count; i++){
        h->long_ref[i]->reference=0;
        CHECK_DELAY(h->long_ref[i]);
        h->long_ref[i]= NULL;
    }
    h->long_ref_count=0;

    for(i=0; i<h->short_ref_count; i++){
        h->short_ref[i]->reference=0;
        CHECK_DELAY(h->short_ref[i]);
        h->short_ref[i]= NULL;
    }
    h->short_ref_count=0;
}
#undef CHECK_DELAY

/**
 *
 * @return the removed picture or NULL if an error occures
 */
static Picture * remove_short(H264Context *h, int frame_num){
    MpegEncContext * const s = &h->s;
    int i;
    
    if(s->avctx->debug&FF_DEBUG_MMCO)
        av_log(h->s.avctx, AV_LOG_DEBUG, "remove short %d count %d\n", frame_num, h->short_ref_count);
    
    for(i=0; i<h->short_ref_count; i++){
        Picture *pic= h->short_ref[i];
        if(s->avctx->debug&FF_DEBUG_MMCO)
            av_log(h->s.avctx, AV_LOG_DEBUG, "%d %d %p\n", i, pic->frame_num, pic);
        if(pic->frame_num == frame_num){
            h->short_ref[i]= NULL;
            memmove(&h->short_ref[i], &h->short_ref[i+1], (h->short_ref_count - i - 1)*sizeof(Picture*));
            h->short_ref_count--;
            return pic;
        }
    }
    return NULL;
}

/**
 *
 * @return the removed picture or NULL if an error occures
 */
static Picture * remove_long(H264Context *h, int i){
    Picture *pic;

    if(i >= h->long_ref_count) return NULL;
    pic= h->long_ref[i];
    if(pic==NULL) return NULL;
    
    h->long_ref[i]= NULL;
    memmove(&h->long_ref[i], &h->long_ref[i+1], (h->long_ref_count - i - 1)*sizeof(Picture*));
    h->long_ref_count--;

    return pic;
}

/**
 * Executes the reference picture marking (memory management control operations).
 */
static int execute_ref_pic_marking(H264Context *h, MMCO *mmco, int mmco_count){
    MpegEncContext * const s = &h->s;
    int i;
    int current_is_long=0;
    Picture *pic;
    
    if((s->avctx->debug&FF_DEBUG_MMCO) && mmco_count==0)
        av_log(h->s.avctx, AV_LOG_DEBUG, "no mmco here\n");
        
    for(i=0; i<mmco_count; i++){
        if(s->avctx->debug&FF_DEBUG_MMCO)
            av_log(h->s.avctx, AV_LOG_DEBUG, "mmco:%d %d %d\n", h->mmco[i].opcode, h->mmco[i].short_frame_num, h->mmco[i].long_index);

        switch(mmco[i].opcode){
        case MMCO_SHORT2UNUSED:
            pic= remove_short(h, mmco[i].short_frame_num);
            if(pic==NULL) return -1;
            pic->reference= 0;
            break;
        case MMCO_SHORT2LONG:
            pic= remove_long(h, mmco[i].long_index);
            if(pic) pic->reference=0;
            
            h->long_ref[ mmco[i].long_index ]= remove_short(h, mmco[i].short_frame_num);
            h->long_ref[ mmco[i].long_index ]->long_ref=1;
            break;
        case MMCO_LONG2UNUSED:
            pic= remove_long(h, mmco[i].long_index);
            if(pic==NULL) return -1;
            pic->reference= 0;
            break;
        case MMCO_LONG:
            pic= remove_long(h, mmco[i].long_index);
            if(pic) pic->reference=0;
            
            h->long_ref[ mmco[i].long_index ]= s->current_picture_ptr;
            h->long_ref[ mmco[i].long_index ]->long_ref=1;
            h->long_ref_count++;
            
            current_is_long=1;
            break;
        case MMCO_SET_MAX_LONG:
            assert(mmco[i].long_index <= 16);
            while(mmco[i].long_index < h->long_ref_count){
                pic= remove_long(h, mmco[i].long_index);
                pic->reference=0;
            }
            while(mmco[i].long_index > h->long_ref_count){
                h->long_ref[ h->long_ref_count++ ]= NULL;
            }
            break;
        case MMCO_RESET:
            while(h->short_ref_count){
                pic= remove_short(h, h->short_ref[0]->frame_num);
                pic->reference=0;
            }
            while(h->long_ref_count){
                pic= remove_long(h, h->long_ref_count-1);
                pic->reference=0;
            }
            break;
        default: assert(0);
        }
    }
    
    if(!current_is_long){
        pic= remove_short(h, s->current_picture_ptr->frame_num);
        if(pic){
            pic->reference=0;
            av_log(h->s.avctx, AV_LOG_ERROR, "illegal short term buffer state detected\n");
        }
        
        if(h->short_ref_count)
            memmove(&h->short_ref[1], &h->short_ref[0], h->short_ref_count*sizeof(Picture*));

        h->short_ref[0]= s->current_picture_ptr;
        h->short_ref[0]->long_ref=0;
        h->short_ref_count++;
    }
    
    return 0; 
}

static int decode_ref_pic_marking(H264Context *h){
    MpegEncContext * const s = &h->s;
    int i;
    
    if(h->nal_unit_type == NAL_IDR_SLICE){ //FIXME fields
        s->broken_link= get_bits1(&s->gb) -1;
        h->mmco[0].long_index= get_bits1(&s->gb) - 1; // current_long_term_idx
        if(h->mmco[0].long_index == -1)
            h->mmco_index= 0;
        else{
            h->mmco[0].opcode= MMCO_LONG;
            h->mmco_index= 1;
        } 
    }else{
        if(get_bits1(&s->gb)){ // adaptive_ref_pic_marking_mode_flag
            for(i= 0; i<MAX_MMCO_COUNT; i++) { 
                MMCOOpcode opcode= get_ue_golomb(&s->gb);;

                h->mmco[i].opcode= opcode;
                if(opcode==MMCO_SHORT2UNUSED || opcode==MMCO_SHORT2LONG){
                    h->mmco[i].short_frame_num= (h->frame_num - get_ue_golomb(&s->gb) - 1) & ((1<<h->sps.log2_max_frame_num)-1); //FIXME fields
/*                    if(h->mmco[i].short_frame_num >= h->short_ref_count || h->short_ref[ h->mmco[i].short_frame_num ] == NULL){
                        fprintf(stderr, "illegal short ref in memory management control operation %d\n", mmco);
                        return -1;
                    }*/
                }
                if(opcode==MMCO_SHORT2LONG || opcode==MMCO_LONG2UNUSED || opcode==MMCO_LONG || opcode==MMCO_SET_MAX_LONG){
                    h->mmco[i].long_index= get_ue_golomb(&s->gb);
                    if(/*h->mmco[i].long_index >= h->long_ref_count || h->long_ref[ h->mmco[i].long_index ] == NULL*/ h->mmco[i].long_index >= 16){
                        av_log(h->s.avctx, AV_LOG_ERROR, "illegal long ref in memory management control operation %d\n", opcode);
                        return -1;
                    }
                }
                    
                if(opcode > MMCO_LONG){
                    av_log(h->s.avctx, AV_LOG_ERROR, "illegal memory management control operation %d\n", opcode);
                    return -1;
                }
                if(opcode == MMCO_END)
                    break;
            }
            h->mmco_index= i;
        }else{
            assert(h->long_ref_count + h->short_ref_count <= h->sps.ref_frame_count);

            if(h->long_ref_count + h->short_ref_count == h->sps.ref_frame_count){ //FIXME fields
                h->mmco[0].opcode= MMCO_SHORT2UNUSED;
                h->mmco[0].short_frame_num= h->short_ref[ h->short_ref_count - 1 ]->frame_num;
                h->mmco_index= 1;
            }else
                h->mmco_index= 0;
        }
    }
    
    return 0; 
}

static int init_poc(H264Context *h){
    MpegEncContext * const s = &h->s;
    const int max_frame_num= 1<<h->sps.log2_max_frame_num;
    int field_poc[2];

    if(h->nal_unit_type == NAL_IDR_SLICE){
        h->frame_num_offset= 0;
    }else{
        if(h->frame_num < h->prev_frame_num)
            h->frame_num_offset= h->prev_frame_num_offset + max_frame_num;
        else
            h->frame_num_offset= h->prev_frame_num_offset;
    }

    if(h->sps.poc_type==0){
        const int max_poc_lsb= 1<<h->sps.log2_max_poc_lsb;

        if     (h->poc_lsb < h->prev_poc_lsb && h->prev_poc_lsb - h->poc_lsb >= max_poc_lsb/2)
            h->poc_msb = h->prev_poc_msb + max_poc_lsb;
        else if(h->poc_lsb > h->prev_poc_lsb && h->prev_poc_lsb - h->poc_lsb < -max_poc_lsb/2)
            h->poc_msb = h->prev_poc_msb - max_poc_lsb;
        else
            h->poc_msb = h->prev_poc_msb;
//printf("poc: %d %d\n", h->poc_msb, h->poc_lsb);
        field_poc[0] = 
        field_poc[1] = h->poc_msb + h->poc_lsb;
        if(s->picture_structure == PICT_FRAME) 
            field_poc[1] += h->delta_poc_bottom;
    }else if(h->sps.poc_type==1){
        int abs_frame_num, expected_delta_per_poc_cycle, expectedpoc;
        int i;

        if(h->sps.poc_cycle_length != 0)
            abs_frame_num = h->frame_num_offset + h->frame_num;
        else
            abs_frame_num = 0;

        if(h->nal_ref_idc==0 && abs_frame_num > 0)
            abs_frame_num--;
            
        expected_delta_per_poc_cycle = 0;
        for(i=0; i < h->sps.poc_cycle_length; i++)
            expected_delta_per_poc_cycle += h->sps.offset_for_ref_frame[ i ]; //FIXME integrate during sps parse

        if(abs_frame_num > 0){
            int poc_cycle_cnt          = (abs_frame_num - 1) / h->sps.poc_cycle_length;
            int frame_num_in_poc_cycle = (abs_frame_num - 1) % h->sps.poc_cycle_length;

            expectedpoc = poc_cycle_cnt * expected_delta_per_poc_cycle;
            for(i = 0; i <= frame_num_in_poc_cycle; i++)
                expectedpoc = expectedpoc + h->sps.offset_for_ref_frame[ i ];
        } else
            expectedpoc = 0;

        if(h->nal_ref_idc == 0) 
            expectedpoc = expectedpoc + h->sps.offset_for_non_ref_pic;
        
        field_poc[0] = expectedpoc + h->delta_poc[0];
        field_poc[1] = field_poc[0] + h->sps.offset_for_top_to_bottom_field;

        if(s->picture_structure == PICT_FRAME)
            field_poc[1] += h->delta_poc[1];
    }else{
        int poc;
        if(h->nal_unit_type == NAL_IDR_SLICE){
            poc= 0;
        }else{
            if(h->nal_ref_idc) poc= 2*(h->frame_num_offset + h->frame_num);
            else               poc= 2*(h->frame_num_offset + h->frame_num) - 1;
        }
        field_poc[0]= poc;
        field_poc[1]= poc;
    }
    
    if(s->picture_structure != PICT_BOTTOM_FIELD)
        s->current_picture_ptr->field_poc[0]= field_poc[0];
    if(s->picture_structure != PICT_TOP_FIELD)
        s->current_picture_ptr->field_poc[1]= field_poc[1];
    if(s->picture_structure == PICT_FRAME) // FIXME field pix?
        s->current_picture_ptr->poc= FFMIN(field_poc[0], field_poc[1]);

    return 0;
}

/**
 * decodes a slice header.
 * this will allso call MPV_common_init() and frame_start() as needed
 */
static int decode_slice_header(H264Context *h){
    MpegEncContext * const s = &h->s;
    int first_mb_in_slice, pps_id;
    int num_ref_idx_active_override_flag;
    static const uint8_t slice_type_map[5]= {P_TYPE, B_TYPE, I_TYPE, SP_TYPE, SI_TYPE};

    s->current_picture.reference= h->nal_ref_idc != 0;

    first_mb_in_slice= get_ue_golomb(&s->gb);

    h->slice_type= get_ue_golomb(&s->gb);
    if(h->slice_type > 9){
        av_log(h->s.avctx, AV_LOG_ERROR, "slice type too large (%d) at %d %d\n", h->slice_type, s->mb_x, s->mb_y);
        return -1;
    }
    if(h->slice_type > 4){
        h->slice_type -= 5;
        h->slice_type_fixed=1;
    }else
        h->slice_type_fixed=0;
    
    h->slice_type= slice_type_map[ h->slice_type ];
    
    s->pict_type= h->slice_type; // to make a few old func happy, its wrong though
        
    pps_id= get_ue_golomb(&s->gb);
    if(pps_id>255){
        av_log(h->s.avctx, AV_LOG_ERROR, "pps_id out of range\n");
        return -1;
    }
    h->pps= h->pps_buffer[pps_id];
    if(h->pps.slice_group_count == 0){
        av_log(h->s.avctx, AV_LOG_ERROR, "non existing PPS referenced\n");
        return -1;
    }

    h->sps= h->sps_buffer[ h->pps.sps_id ];
    if(h->sps.log2_max_frame_num == 0){
        av_log(h->s.avctx, AV_LOG_ERROR, "non existing SPS referenced\n");
        return -1;
    }
    
    s->mb_width= h->sps.mb_width;
    s->mb_height= h->sps.mb_height;
    
    h->b_stride=  s->mb_width*4 + 1;
    h->b8_stride= s->mb_width*2 + 1;

    s->resync_mb_x = s->mb_x = first_mb_in_slice % s->mb_width;
    s->resync_mb_y = s->mb_y = first_mb_in_slice / s->mb_width; //FIXME AFFW
    
    s->width = 16*s->mb_width - 2*(h->sps.crop_left + h->sps.crop_right );
    if(h->sps.frame_mbs_only_flag)
        s->height= 16*s->mb_height - 2*(h->sps.crop_top  + h->sps.crop_bottom);
    else
        s->height= 16*s->mb_height - 4*(h->sps.crop_top  + h->sps.crop_bottom); //FIXME recheck
    
    if (s->context_initialized 
        && (   s->width != s->avctx->width || s->height != s->avctx->height)) {
        free_tables(h);
        MPV_common_end(s);
    }
    if (!s->context_initialized) {
        if (MPV_common_init(s) < 0)
            return -1;

        alloc_tables(h);

        s->avctx->width = s->width;
        s->avctx->height = s->height;
        s->avctx->sample_aspect_ratio= h->sps.sar;

        if(h->sps.timing_info_present_flag && h->sps.fixed_frame_rate_flag){
            s->avctx->frame_rate = h->sps.time_scale;
            s->avctx->frame_rate_base = h->sps.num_units_in_tick;
        }
    }

    if(h->slice_num == 0){
        frame_start(h);
    }

    s->current_picture_ptr->frame_num= //FIXME frame_num cleanup
    h->frame_num= get_bits(&s->gb, h->sps.log2_max_frame_num);

    if(h->sps.frame_mbs_only_flag){
        s->picture_structure= PICT_FRAME;
    }else{
        if(get_bits1(&s->gb)) //field_pic_flag
            s->picture_structure= PICT_TOP_FIELD + get_bits1(&s->gb); //bottom_field_flag
        else
            s->picture_structure= PICT_FRAME;
    }

    if(s->picture_structure==PICT_FRAME){
        h->curr_pic_num=   h->frame_num;
        h->max_pic_num= 1<< h->sps.log2_max_frame_num;
    }else{
        h->curr_pic_num= 2*h->frame_num;
        h->max_pic_num= 1<<(h->sps.log2_max_frame_num + 1);
    }
        
    if(h->nal_unit_type == NAL_IDR_SLICE){
        get_ue_golomb(&s->gb); /* idr_pic_id */
    }
   
    if(h->sps.poc_type==0){
        h->poc_lsb= get_bits(&s->gb, h->sps.log2_max_poc_lsb);
        
        if(h->pps.pic_order_present==1 && s->picture_structure==PICT_FRAME){
            h->delta_poc_bottom= get_se_golomb(&s->gb);
        }
    }
    
    if(h->sps.poc_type==1 && !h->sps.delta_pic_order_always_zero_flag){
        h->delta_poc[0]= get_se_golomb(&s->gb);
        
        if(h->pps.pic_order_present==1 && s->picture_structure==PICT_FRAME)
            h->delta_poc[1]= get_se_golomb(&s->gb);
    }
    
    init_poc(h);
    
    if(h->pps.redundant_pic_cnt_present){
        h->redundant_pic_count= get_ue_golomb(&s->gb);
    }

    //set defaults, might be overriden a few line later
    h->ref_count[0]= h->pps.ref_count[0];
    h->ref_count[1]= h->pps.ref_count[1];

    if(h->slice_type == P_TYPE || h->slice_type == SP_TYPE || h->slice_type == B_TYPE){
        if(h->slice_type == B_TYPE){
            h->direct_spatial_mv_pred= get_bits1(&s->gb);
        }
        num_ref_idx_active_override_flag= get_bits1(&s->gb);
    
        if(num_ref_idx_active_override_flag){
            h->ref_count[0]= get_ue_golomb(&s->gb) + 1;
            if(h->slice_type==B_TYPE)
                h->ref_count[1]= get_ue_golomb(&s->gb) + 1;

            if(h->ref_count[0] > 32 || h->ref_count[1] > 32){
                av_log(h->s.avctx, AV_LOG_ERROR, "reference overflow\n");
                return -1;
            }
        }
    }

    if(h->slice_num == 0){
        fill_default_ref_list(h);
    }

    decode_ref_pic_list_reordering(h);

    if(   (h->pps.weighted_pred          && (h->slice_type == P_TYPE || h->slice_type == SP_TYPE )) 
       || (h->pps.weighted_bipred_idc==1 && h->slice_type==B_TYPE ) )
        pred_weight_table(h);
    else if(h->pps.weighted_bipred_idc==2 && h->slice_type==B_TYPE)
        implicit_weight_table(h);
    else
        h->use_weight = 0;
    
    if(s->current_picture.reference)
        decode_ref_pic_marking(h);

    if( h->slice_type != I_TYPE && h->slice_type != SI_TYPE && h->pps.cabac )
        h->cabac_init_idc = get_ue_golomb(&s->gb);

    h->last_qscale_diff = 0;
    s->qscale = h->pps.init_qp + get_se_golomb(&s->gb);
    if(s->qscale<0 || s->qscale>51){
        av_log(s->avctx, AV_LOG_ERROR, "QP %d out of range\n", s->qscale);
        return -1;
    }
    h->chroma_qp = get_chroma_qp(h, s->qscale);
    //FIXME qscale / qp ... stuff
    if(h->slice_type == SP_TYPE){
        get_bits1(&s->gb); /* sp_for_switch_flag */
    }
    if(h->slice_type==SP_TYPE || h->slice_type == SI_TYPE){
        get_se_golomb(&s->gb); /* slice_qs_delta */
    }

    h->deblocking_filter = 1;
    h->slice_alpha_c0_offset = 0;
    h->slice_beta_offset = 0;
    if( h->pps.deblocking_filter_parameters_present ) {
        h->deblocking_filter= get_ue_golomb(&s->gb);
        if(h->deblocking_filter < 2) 
            h->deblocking_filter^= 1; // 1<->0

        if( h->deblocking_filter ) {
            h->slice_alpha_c0_offset = get_se_golomb(&s->gb) << 1;
            h->slice_beta_offset = get_se_golomb(&s->gb) << 1;
        }
    }

#if 0 //FMO
    if( h->pps.num_slice_groups > 1  && h->pps.mb_slice_group_map_type >= 3 && h->pps.mb_slice_group_map_type <= 5)
        slice_group_change_cycle= get_bits(&s->gb, ?);
#endif

    h->slice_num++;

    if(s->avctx->debug&FF_DEBUG_PICT_INFO){
        av_log(h->s.avctx, AV_LOG_DEBUG, "slice:%d mb:%d %c pps:%d frame:%d poc:%d/%d ref:%d/%d qp:%d loop:%d weight:%d%s\n", 
               h->slice_num, first_mb_in_slice, 
               av_get_pict_type_char(h->slice_type),
               pps_id, h->frame_num,
               s->current_picture_ptr->field_poc[0], s->current_picture_ptr->field_poc[1],
               h->ref_count[0], h->ref_count[1],
               s->qscale,
               h->deblocking_filter,
               h->use_weight,
               h->use_weight==1 && h->use_weight_chroma ? "c" : ""
               );
    }

    return 0;
}

/**
 *
 */
static inline int get_level_prefix(GetBitContext *gb){
    unsigned int buf;
    int log;
    
    OPEN_READER(re, gb);
    UPDATE_CACHE(re, gb);
    buf=GET_CACHE(re, gb);
    
    log= 32 - av_log2(buf);
#ifdef TRACE
    print_bin(buf>>(32-log), log);
    av_log(NULL, AV_LOG_DEBUG, "%5d %2d %3d lpr @%5d in %s get_level_prefix\n", buf>>(32-log), log, log-1, get_bits_count(gb), __FILE__);
#endif

    LAST_SKIP_BITS(re, gb, log);
    CLOSE_READER(re, gb);

    return log-1;
}

/**
 * decodes a residual block.
 * @param n block index
 * @param scantable scantable
 * @param max_coeff number of coefficients in the block
 * @return <0 if an error occured
 */
static int decode_residual(H264Context *h, GetBitContext *gb, DCTELEM *block, int n, const uint8_t *scantable, int qp, int max_coeff){
    MpegEncContext * const s = &h->s;
    const uint16_t *qmul= dequant_coeff[qp];
    static const int coeff_token_table_index[17]= {0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3};
    int level[16], run[16];
    int suffix_length, zeros_left, coeff_num, coeff_token, total_coeff, i, trailing_ones;

    //FIXME put trailing_onex into the context

    if(n == CHROMA_DC_BLOCK_INDEX){
        coeff_token= get_vlc2(gb, chroma_dc_coeff_token_vlc.table, CHROMA_DC_COEFF_TOKEN_VLC_BITS, 1);
        total_coeff= coeff_token>>2;
    }else{    
        if(n == LUMA_DC_BLOCK_INDEX){
            total_coeff= pred_non_zero_count(h, 0);
            coeff_token= get_vlc2(gb, coeff_token_vlc[ coeff_token_table_index[total_coeff] ].table, COEFF_TOKEN_VLC_BITS, 2);
            total_coeff= coeff_token>>2;
        }else{
            total_coeff= pred_non_zero_count(h, n);
            coeff_token= get_vlc2(gb, coeff_token_vlc[ coeff_token_table_index[total_coeff] ].table, COEFF_TOKEN_VLC_BITS, 2);
            total_coeff= coeff_token>>2;
            h->non_zero_count_cache[ scan8[n] ]= total_coeff;
        }
    }

    //FIXME set last_non_zero?

    if(total_coeff==0)
        return 0;
        
    trailing_ones= coeff_token&3;
    tprintf("trailing:%d, total:%d\n", trailing_ones, total_coeff);
    assert(total_coeff<=16);
    
    for(i=0; i<trailing_ones; i++){
        level[i]= 1 - 2*get_bits1(gb);
    }

    suffix_length= total_coeff > 10 && trailing_ones < 3;

    for(; i<total_coeff; i++){
        const int prefix= get_level_prefix(gb);
        int level_code, mask;

        if(prefix<14){ //FIXME try to build a large unified VLC table for all this
            if(suffix_length)
                level_code= (prefix<<suffix_length) + get_bits(gb, suffix_length); //part
            else
                level_code= (prefix<<suffix_length); //part
        }else if(prefix==14){
            if(suffix_length)
                level_code= (prefix<<suffix_length) + get_bits(gb, suffix_length); //part
            else
                level_code= prefix + get_bits(gb, 4); //part
        }else if(prefix==15){
            level_code= (prefix<<suffix_length) + get_bits(gb, 12); //part
            if(suffix_length==0) level_code+=15; //FIXME doesnt make (much)sense
        }else{
            av_log(h->s.avctx, AV_LOG_ERROR, "prefix too large at %d %d\n", s->mb_x, s->mb_y);
            return -1;
        }

        if(i==trailing_ones && i<3) level_code+= 2; //FIXME split first iteration

        mask= -(level_code&1);
        level[i]= (((2+level_code)>>1) ^ mask) - mask;

        if(suffix_length==0) suffix_length=1; //FIXME split first iteration

#if 1
        if(ABS(level[i]) > (3<<(suffix_length-1)) && suffix_length<6) suffix_length++;
#else        
        if((2+level_code)>>1) > (3<<(suffix_length-1)) && suffix_length<6) suffix_length++;
        /* ? == prefix > 2 or sth */
#endif
        tprintf("level: %d suffix_length:%d\n", level[i], suffix_length);
    }

    if(total_coeff == max_coeff)
        zeros_left=0;
    else{
        if(n == CHROMA_DC_BLOCK_INDEX)
            zeros_left= get_vlc2(gb, chroma_dc_total_zeros_vlc[ total_coeff-1 ].table, CHROMA_DC_TOTAL_ZEROS_VLC_BITS, 1);
        else
            zeros_left= get_vlc2(gb, total_zeros_vlc[ total_coeff-1 ].table, TOTAL_ZEROS_VLC_BITS, 1);
    }
    
    for(i=0; i<total_coeff-1; i++){
        if(zeros_left <=0)
            break;
        else if(zeros_left < 7){
            run[i]= get_vlc2(gb, run_vlc[zeros_left-1].table, RUN_VLC_BITS, 1);
        }else{
            run[i]= get_vlc2(gb, run7_vlc.table, RUN7_VLC_BITS, 2);
        }
        zeros_left -= run[i];
    }

    if(zeros_left<0){
        av_log(h->s.avctx, AV_LOG_ERROR, "negative number of zero coeffs at %d %d\n", s->mb_x, s->mb_y);
        return -1;
    }
    
    for(; i<total_coeff-1; i++){
        run[i]= 0;
    }

    run[i]= zeros_left;

    coeff_num=-1;
    if(n > 24){
        for(i=total_coeff-1; i>=0; i--){ //FIXME merge into rundecode?
            int j;

            coeff_num += run[i] + 1; //FIXME add 1 earlier ?
            j= scantable[ coeff_num ];

            block[j]= level[i];
        }
    }else{
        for(i=total_coeff-1; i>=0; i--){ //FIXME merge into  rundecode?
            int j;

            coeff_num += run[i] + 1; //FIXME add 1 earlier ?
            j= scantable[ coeff_num ];

            block[j]= level[i] * qmul[j];
//            printf("%d %d  ", block[j], qmul[j]);
        }
    }
    return 0;
}

/**
 * decodes a P_SKIP or B_SKIP macroblock
 */
static void decode_mb_skip(H264Context *h){
    MpegEncContext * const s = &h->s;
    const int mb_xy= s->mb_x + s->mb_y*s->mb_stride;
    int mb_type;
    
    memset(h->non_zero_count[mb_xy], 0, 16);
    memset(h->non_zero_count_cache + 8, 0, 8*5); //FIXME ugly, remove pfui

    if( h->slice_type == B_TYPE )
    {
        // just for fill_caches. pred_direct_motion will set the real mb_type
        mb_type= MB_TYPE_16x16|MB_TYPE_P0L0|MB_TYPE_P0L1|MB_TYPE_DIRECT2|MB_TYPE_SKIP;
        //FIXME mbaff

        fill_caches(h, mb_type); //FIXME check what is needed and what not ...
        pred_direct_motion(h, &mb_type);
        if(h->pps.cabac){
            fill_rectangle(h->mvd_cache[0][scan8[0]], 4, 4, 8, 0, 4);
            fill_rectangle(h->mvd_cache[1][scan8[0]], 4, 4, 8, 0, 4);
        }
    }
    else
    {
        int mx, my;
        mb_type= MB_TYPE_16x16|MB_TYPE_P0L0|MB_TYPE_P1L0|MB_TYPE_SKIP;

        if(h->sps.mb_aff && s->mb_skip_run==0 && (s->mb_y&1)==0){
            h->mb_field_decoding_flag= get_bits1(&s->gb);
        }
        if(h->mb_field_decoding_flag)
            mb_type|= MB_TYPE_INTERLACED;
        
        fill_caches(h, mb_type); //FIXME check what is needed and what not ...
        pred_pskip_motion(h, &mx, &my);
        fill_rectangle(&h->ref_cache[0][scan8[0]], 4, 4, 8, 0, 1);
        fill_rectangle(  h->mv_cache[0][scan8[0]], 4, 4, 8, pack16to32(mx,my), 4);
        if(h->pps.cabac)
            fill_rectangle(h->mvd_cache[0][scan8[0]], 4, 4, 8, 0, 4);
    }

    write_back_motion(h, mb_type);
    s->current_picture.mb_type[mb_xy]= mb_type|MB_TYPE_SKIP;
    s->current_picture.qscale_table[mb_xy]= s->qscale;
    h->slice_table[ mb_xy ]= h->slice_num;
    h->prev_mb_skiped= 1;
}

/**
 * decodes a macroblock
 * @returns 0 if ok, AC_ERROR / DC_ERROR / MV_ERROR if an error is noticed
 */
static int decode_mb_cavlc(H264Context *h){
    MpegEncContext * const s = &h->s;
    const int mb_xy= s->mb_x + s->mb_y*s->mb_stride;
    int mb_type, partition_count, cbp;

    s->dsp.clear_blocks(h->mb); //FIXME avoid if allready clear (move after skip handlong?    

    tprintf("pic:%d mb:%d/%d\n", h->frame_num, s->mb_x, s->mb_y);
    cbp = 0; /* avoid warning. FIXME: find a solution without slowing
                down the code */
    if(h->slice_type != I_TYPE && h->slice_type != SI_TYPE){
        if(s->mb_skip_run==-1)
            s->mb_skip_run= get_ue_golomb(&s->gb);
        
        if (s->mb_skip_run--) {
            decode_mb_skip(h);
            return 0;
        }
    }
    if(h->sps.mb_aff /* && !field pic FIXME needed? */){
        if((s->mb_y&1)==0)
            h->mb_field_decoding_flag = get_bits1(&s->gb);
    }else
        h->mb_field_decoding_flag=0; //FIXME som ed note ?!
    
    h->prev_mb_skiped= 0;
    
    mb_type= get_ue_golomb(&s->gb);
    if(h->slice_type == B_TYPE){
        if(mb_type < 23){
            partition_count= b_mb_type_info[mb_type].partition_count;
            mb_type=         b_mb_type_info[mb_type].type;
        }else{
            mb_type -= 23;
            goto decode_intra_mb;
        }
    }else if(h->slice_type == P_TYPE /*|| h->slice_type == SP_TYPE */){
        if(mb_type < 5){
            partition_count= p_mb_type_info[mb_type].partition_count;
            mb_type=         p_mb_type_info[mb_type].type;
        }else{
            mb_type -= 5;
            goto decode_intra_mb;
        }
    }else{
       assert(h->slice_type == I_TYPE);
decode_intra_mb:
        if(mb_type > 25){
            av_log(h->s.avctx, AV_LOG_ERROR, "mb_type %d in %c slice to large at %d %d\n", mb_type, av_get_pict_type_char(h->slice_type), s->mb_x, s->mb_y);
            return -1;
        }
        partition_count=0;
        cbp= i_mb_type_info[mb_type].cbp;
        h->intra16x16_pred_mode= i_mb_type_info[mb_type].pred_mode;
        mb_type= i_mb_type_info[mb_type].type;
    }

    if(h->mb_field_decoding_flag)
        mb_type |= MB_TYPE_INTERLACED;

    s->current_picture.mb_type[mb_xy]= mb_type;
    h->slice_table[ mb_xy ]= h->slice_num;
    
    if(IS_INTRA_PCM(mb_type)){
        const uint8_t *ptr;
        int x, y;
        
        // we assume these blocks are very rare so we dont optimize it
        align_get_bits(&s->gb);
        
        ptr= s->gb.buffer + get_bits_count(&s->gb);
    
        for(y=0; y<16; y++){
            const int index= 4*(y&3) + 64*(y>>2);
            for(x=0; x<16; x++){
                h->mb[index + (x&3) + 16*(x>>2)]= *(ptr++);
            }
        }
        for(y=0; y<8; y++){
            const int index= 256 + 4*(y&3) + 32*(y>>2);
            for(x=0; x<8; x++){
                h->mb[index + (x&3) + 16*(x>>2)]= *(ptr++);
            }
        }
        for(y=0; y<8; y++){
            const int index= 256 + 64 + 4*(y&3) + 32*(y>>2);
            for(x=0; x<8; x++){
                h->mb[index + (x&3) + 16*(x>>2)]= *(ptr++);
            }
        }
    
        skip_bits(&s->gb, 384); //FIXME check /fix the bitstream readers
        
        //FIXME deblock filter, non_zero_count_cache init ...
        memset(h->non_zero_count[mb_xy], 16, 16);
        s->current_picture.qscale_table[mb_xy]= s->qscale;
        
        return 0;
    }
        
    fill_caches(h, mb_type);

    //mb_pred
    if(IS_INTRA(mb_type)){
//            init_top_left_availability(h);
            if(IS_INTRA4x4(mb_type)){
                int i;

//                fill_intra4x4_pred_table(h);
                for(i=0; i<16; i++){
                    const int mode_coded= !get_bits1(&s->gb);
                    const int predicted_mode=  pred_intra_mode(h, i);
                    int mode;

                    if(mode_coded){
                        const int rem_mode= get_bits(&s->gb, 3);
                        if(rem_mode<predicted_mode)
                            mode= rem_mode;
                        else
                            mode= rem_mode + 1;
                    }else{
                        mode= predicted_mode;
                    }
                    
                    h->intra4x4_pred_mode_cache[ scan8[i] ] = mode;
                }
                write_back_intra_pred_mode(h);
                if( check_intra4x4_pred_mode(h) < 0)
                    return -1;
            }else{
                h->intra16x16_pred_mode= check_intra_pred_mode(h, h->intra16x16_pred_mode);
                if(h->intra16x16_pred_mode < 0)
                    return -1;
            }
            h->chroma_pred_mode= get_ue_golomb(&s->gb);

            h->chroma_pred_mode= check_intra_pred_mode(h, h->chroma_pred_mode);
            if(h->chroma_pred_mode < 0)
                return -1;
    }else if(partition_count==4){
        int i, j, sub_partition_count[4], list, ref[2][4];
        
        if(h->slice_type == B_TYPE){
            for(i=0; i<4; i++){
                h->sub_mb_type[i]= get_ue_golomb(&s->gb);
                if(h->sub_mb_type[i] >=13){
                    av_log(h->s.avctx, AV_LOG_ERROR, "B sub_mb_type %d out of range at %d %d\n", h->sub_mb_type[i], s->mb_x, s->mb_y);
                    return -1;
                }
                sub_partition_count[i]= b_sub_mb_type_info[ h->sub_mb_type[i] ].partition_count;
                h->sub_mb_type[i]=      b_sub_mb_type_info[ h->sub_mb_type[i] ].type;
            }
            if(   IS_DIRECT(h->sub_mb_type[0]) || IS_DIRECT(h->sub_mb_type[1])
               || IS_DIRECT(h->sub_mb_type[2]) || IS_DIRECT(h->sub_mb_type[3]))
                pred_direct_motion(h, &mb_type);
        }else{
            assert(h->slice_type == P_TYPE || h->slice_type == SP_TYPE); //FIXME SP correct ?
            for(i=0; i<4; i++){
                h->sub_mb_type[i]= get_ue_golomb(&s->gb);
                if(h->sub_mb_type[i] >=4){
                    av_log(h->s.avctx, AV_LOG_ERROR, "P sub_mb_type %d out of range at %d %d\n", h->sub_mb_type[i], s->mb_x, s->mb_y);
                    return -1;
                }
                sub_partition_count[i]= p_sub_mb_type_info[ h->sub_mb_type[i] ].partition_count;
                h->sub_mb_type[i]=      p_sub_mb_type_info[ h->sub_mb_type[i] ].type;
            }
        }
        
        for(list=0; list<2; list++){
            const int ref_count= IS_REF0(mb_type) ? 1 : h->ref_count[list];
            if(ref_count == 0) continue;
            for(i=0; i<4; i++){
                if(IS_DIRECT(h->sub_mb_type[i])) continue;
                if(IS_DIR(h->sub_mb_type[i], 0, list)){
                    ref[list][i] = get_te0_golomb(&s->gb, ref_count); //FIXME init to 0 before and skip?
                }else{
                 //FIXME
                    ref[list][i] = -1;
                }
            }
        }
        
        for(list=0; list<2; list++){
            const int ref_count= IS_REF0(mb_type) ? 1 : h->ref_count[list];
            if(ref_count == 0) continue;

            for(i=0; i<4; i++){
                if(IS_DIRECT(h->sub_mb_type[i])) continue;
                h->ref_cache[list][ scan8[4*i]   ]=h->ref_cache[list][ scan8[4*i]+1 ]=
                h->ref_cache[list][ scan8[4*i]+8 ]=h->ref_cache[list][ scan8[4*i]+9 ]= ref[list][i];

                if(IS_DIR(h->sub_mb_type[i], 0, list)){
                    const int sub_mb_type= h->sub_mb_type[i];
                    const int block_width= (sub_mb_type & (MB_TYPE_16x16|MB_TYPE_16x8)) ? 2 : 1;
                    for(j=0; j<sub_partition_count[i]; j++){
                        int mx, my;
                        const int index= 4*i + block_width*j;
                        int16_t (* mv_cache)[2]= &h->mv_cache[list][ scan8[index] ];
                        pred_motion(h, index, block_width, list, h->ref_cache[list][ scan8[index] ], &mx, &my);
                        mx += get_se_golomb(&s->gb);
                        my += get_se_golomb(&s->gb);
                        tprintf("final mv:%d %d\n", mx, my);

                        if(IS_SUB_8X8(sub_mb_type)){
                            mv_cache[ 0 ][0]= mv_cache[ 1 ][0]= 
                            mv_cache[ 8 ][0]= mv_cache[ 9 ][0]= mx;
                            mv_cache[ 0 ][1]= mv_cache[ 1 ][1]= 
                            mv_cache[ 8 ][1]= mv_cache[ 9 ][1]= my;
                        }else if(IS_SUB_8X4(sub_mb_type)){
                            mv_cache[ 0 ][0]= mv_cache[ 1 ][0]= mx;
                            mv_cache[ 0 ][1]= mv_cache[ 1 ][1]= my;
                        }else if(IS_SUB_4X8(sub_mb_type)){
                            mv_cache[ 0 ][0]= mv_cache[ 8 ][0]= mx;
                            mv_cache[ 0 ][1]= mv_cache[ 8 ][1]= my;
                        }else{
                            assert(IS_SUB_4X4(sub_mb_type));
                            mv_cache[ 0 ][0]= mx;
                            mv_cache[ 0 ][1]= my;
                        }
                    }
                }else{
                    uint32_t *p= (uint32_t *)&h->mv_cache[list][ scan8[4*i] ][0];
                    p[0] = p[1]=
                    p[8] = p[9]= 0;
                }
            }
        }
    }else if(IS_DIRECT(mb_type)){
        pred_direct_motion(h, &mb_type);
        s->current_picture.mb_type[mb_xy]= mb_type;
    }else{
        int list, mx, my, i;
         //FIXME we should set ref_idx_l? to 0 if we use that later ...
        if(IS_16X16(mb_type)){
            for(list=0; list<2; list++){
                if(h->ref_count[list]>0){
                    if(IS_DIR(mb_type, 0, list)){
                        const int val= get_te0_golomb(&s->gb, h->ref_count[list]);
                        fill_rectangle(&h->ref_cache[list][ scan8[0] ], 4, 4, 8, val, 1);
                    }
                }
            }
            for(list=0; list<2; list++){
                if(IS_DIR(mb_type, 0, list)){
                    pred_motion(h, 0, 4, list, h->ref_cache[list][ scan8[0] ], &mx, &my);
                    mx += get_se_golomb(&s->gb);
                    my += get_se_golomb(&s->gb);
                    tprintf("final mv:%d %d\n", mx, my);

                    fill_rectangle(h->mv_cache[list][ scan8[0] ], 4, 4, 8, pack16to32(mx,my), 4);
                }
            }
        }
        else if(IS_16X8(mb_type)){
            for(list=0; list<2; list++){
                if(h->ref_count[list]>0){
                    for(i=0; i<2; i++){
                        if(IS_DIR(mb_type, i, list)){
                            const int val= get_te0_golomb(&s->gb, h->ref_count[list]);
                            fill_rectangle(&h->ref_cache[list][ scan8[0] + 16*i ], 4, 2, 8, val, 1);
                        }else // needed only for mixed refs (e.g. B_L0_L1_16x8)
                            fill_rectangle(&h->ref_cache[list][ scan8[0] + 16*i ], 4, 2, 8, (LIST_NOT_USED&0xFF), 1);
                    }
                }
            }
            for(list=0; list<2; list++){
                for(i=0; i<2; i++){
                    if(IS_DIR(mb_type, i, list)){
                        pred_16x8_motion(h, 8*i, list, h->ref_cache[list][scan8[0] + 16*i], &mx, &my);
                        mx += get_se_golomb(&s->gb);
                        my += get_se_golomb(&s->gb);
                        tprintf("final mv:%d %d\n", mx, my);

                        fill_rectangle(h->mv_cache[list][ scan8[0] + 16*i ], 4, 2, 8, pack16to32(mx,my), 4);
                    }else
                        fill_rectangle(h->mv_cache[list][ scan8[0] + 16*i ], 4, 2, 8, 0, 4);
                }
            }
        }else{
            assert(IS_8X16(mb_type));
            for(list=0; list<2; list++){
                if(h->ref_count[list]>0){
                    for(i=0; i<2; i++){
                        if(IS_DIR(mb_type, i, list)){ //FIXME optimize
                            const int val= get_te0_golomb(&s->gb, h->ref_count[list]);
                            fill_rectangle(&h->ref_cache[list][ scan8[0] + 2*i ], 2, 4, 8, val, 1);
                        }else // needed only for mixed refs
                            fill_rectangle(&h->ref_cache[list][ scan8[0] + 2*i ], 2, 4, 8, (LIST_NOT_USED&0xFF), 1);
                    }
                }
            }
            for(list=0; list<2; list++){
                for(i=0; i<2; i++){
                    if(IS_DIR(mb_type, i, list)){
                        pred_8x16_motion(h, i*4, list, h->ref_cache[list][ scan8[0] + 2*i ], &mx, &my);
                        mx += get_se_golomb(&s->gb);
                        my += get_se_golomb(&s->gb);
                        tprintf("final mv:%d %d\n", mx, my);

                        fill_rectangle(h->mv_cache[list][ scan8[0] + 2*i ], 2, 4, 8, pack16to32(mx,my), 4);
                    }else
                        fill_rectangle(h->mv_cache[list][ scan8[0] + 2*i ], 2, 4, 8, 0, 4);
                }
            }
        }
    }
    
    if(IS_INTER(mb_type))
        write_back_motion(h, mb_type);
    
    if(!IS_INTRA16x16(mb_type)){
        cbp= get_ue_golomb(&s->gb);
        if(cbp > 47){
            av_log(h->s.avctx, AV_LOG_ERROR, "cbp too large (%d) at %d %d\n", cbp, s->mb_x, s->mb_y);
            return -1;
        }
        
        if(IS_INTRA4x4(mb_type))
            cbp= golomb_to_intra4x4_cbp[cbp];
        else
            cbp= golomb_to_inter_cbp[cbp];
    }

    if(cbp || IS_INTRA16x16(mb_type)){
        int i8x8, i4x4, chroma_idx;
        int chroma_qp, dquant;
        GetBitContext *gb= IS_INTRA(mb_type) ? h->intra_gb_ptr : h->inter_gb_ptr;
        const uint8_t *scan, *dc_scan;
        
//        fill_non_zero_count_cache(h);

        if(IS_INTERLACED(mb_type)){
            scan= field_scan;
            dc_scan= luma_dc_field_scan;
        }else{
            scan= zigzag_scan;
            dc_scan= luma_dc_zigzag_scan;
        }

        dquant= get_se_golomb(&s->gb);

        if( dquant > 25 || dquant < -26 ){
            av_log(h->s.avctx, AV_LOG_ERROR, "dquant out of range (%d) at %d %d\n", dquant, s->mb_x, s->mb_y);
            return -1;
        }
        
        s->qscale += dquant;
        if(((unsigned)s->qscale) > 51){
            if(s->qscale<0) s->qscale+= 52;
            else            s->qscale-= 52;
        }
        
        h->chroma_qp= chroma_qp= get_chroma_qp(h, s->qscale);
        if(IS_INTRA16x16(mb_type)){
            if( decode_residual(h, h->intra_gb_ptr, h->mb, LUMA_DC_BLOCK_INDEX, dc_scan, s->qscale, 16) < 0){
                return -1; //FIXME continue if partotioned and other retirn -1 too
            }

            assert((cbp&15) == 0 || (cbp&15) == 15);

            if(cbp&15){
                for(i8x8=0; i8x8<4; i8x8++){
                    for(i4x4=0; i4x4<4; i4x4++){
                        const int index= i4x4 + 4*i8x8;
                        if( decode_residual(h, h->intra_gb_ptr, h->mb + 16*index, index, scan + 1, s->qscale, 15) < 0 ){
                            return -1;
                        }
                    }
                }
            }else{
                fill_rectangle(&h->non_zero_count_cache[scan8[0]], 4, 4, 8, 0, 1);
            }
        }else{
            for(i8x8=0; i8x8<4; i8x8++){
                if(cbp & (1<<i8x8)){
                    for(i4x4=0; i4x4<4; i4x4++){
                        const int index= i4x4 + 4*i8x8;
                        
                        if( decode_residual(h, gb, h->mb + 16*index, index, scan, s->qscale, 16) <0 ){
                            return -1;
                        }
                    }
                }else{
                    uint8_t * const nnz= &h->non_zero_count_cache[ scan8[4*i8x8] ];
                    nnz[0] = nnz[1] = nnz[8] = nnz[9] = 0;
                }
            }
        }
        
        if(cbp&0x30){
            for(chroma_idx=0; chroma_idx<2; chroma_idx++)
                if( decode_residual(h, gb, h->mb + 256 + 16*4*chroma_idx, CHROMA_DC_BLOCK_INDEX, chroma_dc_scan, chroma_qp, 4) < 0){
                    return -1;
                }
        }

        if(cbp&0x20){
            for(chroma_idx=0; chroma_idx<2; chroma_idx++){
                for(i4x4=0; i4x4<4; i4x4++){
                    const int index= 16 + 4*chroma_idx + i4x4;
                    if( decode_residual(h, gb, h->mb + 16*index, index, scan + 1, chroma_qp, 15) < 0){
                        return -1;
                    }
                }
            }
        }else{
            uint8_t * const nnz= &h->non_zero_count_cache[0];
            nnz[ scan8[16]+0 ] = nnz[ scan8[16]+1 ] =nnz[ scan8[16]+8 ] =nnz[ scan8[16]+9 ] =
            nnz[ scan8[20]+0 ] = nnz[ scan8[20]+1 ] =nnz[ scan8[20]+8 ] =nnz[ scan8[20]+9 ] = 0;
        }
    }else{
        uint8_t * const nnz= &h->non_zero_count_cache[0];
        fill_rectangle(&nnz[scan8[0]], 4, 4, 8, 0, 1);
        nnz[ scan8[16]+0 ] = nnz[ scan8[16]+1 ] =nnz[ scan8[16]+8 ] =nnz[ scan8[16]+9 ] =
        nnz[ scan8[20]+0 ] = nnz[ scan8[20]+1 ] =nnz[ scan8[20]+8 ] =nnz[ scan8[20]+9 ] = 0;
    }
    s->current_picture.qscale_table[mb_xy]= s->qscale;
    write_back_non_zero_count(h);

    return 0;
}

static int decode_cabac_intra_mb_type(H264Context *h, int ctx_base, int intra_slice) {
    uint8_t *state= &h->cabac_state[ctx_base];
    int mb_type;
    
    if(intra_slice){
        MpegEncContext * const s = &h->s;
        const int mb_xy= s->mb_x + s->mb_y*s->mb_stride;
        int ctx=0;
        if( s->mb_x > 0 && !IS_INTRA4x4( s->current_picture.mb_type[mb_xy-1] ) )
            ctx++;
        if( s->mb_y > 0 && !IS_INTRA4x4( s->current_picture.mb_type[mb_xy-s->mb_stride] ) )
            ctx++;
        if( get_cabac( &h->cabac, &state[ctx] ) == 0 )
            return 0;   /* I4x4 */
        state += 2;
    }else{
        if( get_cabac( &h->cabac, &state[0] ) == 0 )
            return 0;   /* I4x4 */
    }

    if( get_cabac_terminate( &h->cabac ) )
        return 25;  /* PCM */

    mb_type = 1; /* I16x16 */
    if( get_cabac( &h->cabac, &state[1] ) )
        mb_type += 12;  /* cbp_luma != 0 */

    if( get_cabac( &h->cabac, &state[2] ) ) {
        if( get_cabac( &h->cabac, &state[2+intra_slice] ) )
            mb_type += 4 * 2;   /* cbp_chroma == 2 */
        else
            mb_type += 4 * 1;   /* cbp_chroma == 1 */
    }
    if( get_cabac( &h->cabac, &state[3+intra_slice] ) )
        mb_type += 2;
    if( get_cabac( &h->cabac, &state[3+2*intra_slice] ) )
        mb_type += 1;
    return mb_type;
}

static int decode_cabac_mb_type( H264Context *h ) {
    MpegEncContext * const s = &h->s;

    if( h->slice_type == I_TYPE ) {
        return decode_cabac_intra_mb_type(h, 3, 1);
    } else if( h->slice_type == P_TYPE ) {
        if( get_cabac( &h->cabac, &h->cabac_state[14] ) == 0 ) {
            /* P-type */
            if( get_cabac( &h->cabac, &h->cabac_state[15] ) == 0 ) {
                if( get_cabac( &h->cabac, &h->cabac_state[16] ) == 0 )
                    return 0; /* P_L0_D16x16; */
                else
                    return 3; /* P_8x8; */
            } else {
                if( get_cabac( &h->cabac, &h->cabac_state[17] ) == 0 )
                    return 2; /* P_L0_D8x16; */
                else
                    return 1; /* P_L0_D16x8; */
            }
        } else {
            return decode_cabac_intra_mb_type(h, 17, 0) + 5;
        }
    } else if( h->slice_type == B_TYPE ) {
        const int mb_xy= s->mb_x + s->mb_y*s->mb_stride;
        int ctx = 0;
        int bits;

        if( s->mb_x > 0 && !IS_SKIP( s->current_picture.mb_type[mb_xy-1] )
                      && !IS_DIRECT( s->current_picture.mb_type[mb_xy-1] ) )
            ctx++;
        if( s->mb_y > 0 && !IS_SKIP( s->current_picture.mb_type[mb_xy-s->mb_stride] )
                      && !IS_DIRECT( s->current_picture.mb_type[mb_xy-s->mb_stride] ) )
            ctx++;

        if( !get_cabac( &h->cabac, &h->cabac_state[27+ctx] ) )
            return 0; /* B_Direct_16x16 */

        if( !get_cabac( &h->cabac, &h->cabac_state[27+3] ) ) {
            return 1 + get_cabac( &h->cabac, &h->cabac_state[27+5] ); /* B_L[01]_16x16 */
        }

        bits = get_cabac( &h->cabac, &h->cabac_state[27+4] ) << 3;
        bits|= get_cabac( &h->cabac, &h->cabac_state[27+5] ) << 2;
        bits|= get_cabac( &h->cabac, &h->cabac_state[27+5] ) << 1;
        bits|= get_cabac( &h->cabac, &h->cabac_state[27+5] );
        if( bits < 8 )
            return bits + 3; /* B_Bi_16x16 through B_L1_L0_16x8 */
        else if( bits == 13 ) {
            return decode_cabac_intra_mb_type(h, 32, 0) + 23;
        } else if( bits == 14 )
            return 11; /* B_L1_L0_8x16 */
        else if( bits == 15 )
            return 22; /* B_8x8 */

        bits= ( bits<<1 ) | get_cabac( &h->cabac, &h->cabac_state[27+5] );
        return bits - 4; /* B_L0_Bi_* through B_Bi_Bi_* */
    } else {
        /* TODO SI/SP frames? */
        return -1;
    }
}

static int decode_cabac_mb_skip( H264Context *h) {
    MpegEncContext * const s = &h->s;
    const int mb_xy = s->mb_x + s->mb_y*s->mb_stride;
    const int mba_xy = mb_xy - 1;
    const int mbb_xy = mb_xy - s->mb_stride;
    int ctx = 0;

    if( s->mb_x > 0 && !IS_SKIP( s->current_picture.mb_type[mba_xy] ) )
        ctx++;
    if( s->mb_y > 0 && !IS_SKIP( s->current_picture.mb_type[mbb_xy] ) )
        ctx++;

    if( h->slice_type == P_TYPE || h->slice_type == SP_TYPE)
        return get_cabac( &h->cabac, &h->cabac_state[11+ctx] );
    else /* B-frame */
        return get_cabac( &h->cabac, &h->cabac_state[24+ctx] );
}

static int decode_cabac_mb_intra4x4_pred_mode( H264Context *h, int pred_mode ) {
    int mode = 0;

    if( get_cabac( &h->cabac, &h->cabac_state[68] ) )
        return pred_mode;

    if( get_cabac( &h->cabac, &h->cabac_state[69] ) )
        mode += 1;
    if( get_cabac( &h->cabac, &h->cabac_state[69] ) )
        mode += 2;
    if( get_cabac( &h->cabac, &h->cabac_state[69] ) )
        mode += 4;
    if( mode >= pred_mode )
        return mode + 1;
    else
        return mode;
}

static int decode_cabac_mb_chroma_pre_mode( H264Context *h) {
    MpegEncContext * const s = &h->s;
    const int mb_xy = s->mb_x + s->mb_y*s->mb_stride;
    const int mba_xy = mb_xy - 1;
    const int mbb_xy = mb_xy - s->mb_stride;

    int ctx = 0;

    /* No need to test for IS_INTRA4x4 and IS_INTRA16x16, as we set chroma_pred_mode_table to 0 */
    if( s->mb_x > 0 && h->chroma_pred_mode_table[mba_xy] != 0 )
        ctx++;

    if( s->mb_y > 0 && h->chroma_pred_mode_table[mbb_xy] != 0 )
        ctx++;

    if( get_cabac( &h->cabac, &h->cabac_state[64+ctx] ) == 0 )
        return 0;

    if( get_cabac( &h->cabac, &h->cabac_state[64+3] ) == 0 )
        return 1;
    if( get_cabac( &h->cabac, &h->cabac_state[64+3] ) == 0 )
        return 2;
    else
        return 3;
}

static const uint8_t block_idx_x[16] = {
    0, 1, 0, 1, 2, 3, 2, 3, 0, 1, 0, 1, 2, 3, 2, 3
};
static const uint8_t block_idx_y[16] = {
    0, 0, 1, 1, 0, 0, 1, 1, 2, 2, 3, 3, 2, 2, 3, 3
};
static const uint8_t block_idx_xy[4][4] = {
    { 0, 2, 8,  10},
    { 1, 3, 9,  11},
    { 4, 6, 12, 14},
    { 5, 7, 13, 15}
};

static int decode_cabac_mb_cbp_luma( H264Context *h) {
    MpegEncContext * const s = &h->s;
    const int mb_xy = s->mb_x + s->mb_y*s->mb_stride;

    int cbp = 0;
    int i8x8;

    h->cbp_table[mb_xy] = 0;  /* FIXME aaahahahah beurk */

    for( i8x8 = 0; i8x8 < 4; i8x8++ ) {
        int mba_xy = -1;
        int mbb_xy = -1;
        int x, y;
        int ctx = 0;

        x = block_idx_x[4*i8x8];
        y = block_idx_y[4*i8x8];

        if( x > 0 )
            mba_xy = mb_xy;
        else if( s->mb_x > 0 )
            mba_xy = mb_xy - 1;

        if( y > 0 )
            mbb_xy = mb_xy;
        else if( s->mb_y > 0 )
            mbb_xy = mb_xy - s->mb_stride;

        /* No need to test for skip as we put 0 for skip block */
        if( mba_xy >= 0 ) {
            int i8x8a = block_idx_xy[(x-1)&0x03][y]/4;
            if( ((h->cbp_table[mba_xy] >> i8x8a)&0x01) == 0 )
                ctx++;
        }

        if( mbb_xy >= 0 ) {
            int i8x8b = block_idx_xy[x][(y-1)&0x03]/4;
            if( ((h->cbp_table[mbb_xy] >> i8x8b)&0x01) == 0 )
                ctx += 2;
        }

        if( get_cabac( &h->cabac, &h->cabac_state[73 + ctx] ) ) {
            cbp |= 1 << i8x8;
            h->cbp_table[mb_xy] = cbp;  /* FIXME aaahahahah beurk */
        }
    }
    return cbp;
}
static int decode_cabac_mb_cbp_chroma( H264Context *h) {
    int ctx;
    int cbp_a, cbp_b;

    cbp_a = (h->left_cbp>>4)&0x03;
    cbp_b = (h-> top_cbp>>4)&0x03;

    ctx = 0;
    if( cbp_a > 0 ) ctx++;
    if( cbp_b > 0 ) ctx += 2;
    if( get_cabac( &h->cabac, &h->cabac_state[77 + ctx] ) == 0 )
        return 0;

    ctx = 4;
    if( cbp_a == 2 ) ctx++;
    if( cbp_b == 2 ) ctx += 2;
    return 1 + get_cabac( &h->cabac, &h->cabac_state[77 + ctx] );
}
static int decode_cabac_mb_dqp( H264Context *h) {
    MpegEncContext * const s = &h->s;
    int mbn_xy;
    int   ctx = 0;
    int   val = 0;

    if( s->mb_x > 0 )
        mbn_xy = s->mb_x + s->mb_y*s->mb_stride - 1;
    else
        mbn_xy = s->mb_width - 1 + (s->mb_y-1)*s->mb_stride;

    if( mbn_xy >= 0 && h->last_qscale_diff != 0 && ( IS_INTRA16x16(s->current_picture.mb_type[mbn_xy] ) || (h->cbp_table[mbn_xy]&0x3f) ) )
        ctx++;

    while( get_cabac( &h->cabac, &h->cabac_state[60 + ctx] ) ) {
        if( ctx < 2 )
            ctx = 2;
        else
            ctx = 3;
        val++;
    }

    if( val&0x01 )
        return (val + 1)/2;
    else
        return -(val + 1)/2;
}
static int decode_cabac_p_mb_sub_type( H264Context *h ) {
    if( get_cabac( &h->cabac, &h->cabac_state[21] ) )
        return 0;   /* 8x8 */
    if( !get_cabac( &h->cabac, &h->cabac_state[22] ) )
        return 1;   /* 8x4 */
    if( get_cabac( &h->cabac, &h->cabac_state[23] ) )
        return 2;   /* 4x8 */
    return 3;       /* 4x4 */
}
static int decode_cabac_b_mb_sub_type( H264Context *h ) {
    int type;
    if( !get_cabac( &h->cabac, &h->cabac_state[36] ) )
        return 0;   /* B_Direct_8x8 */
    if( !get_cabac( &h->cabac, &h->cabac_state[37] ) )
        return 1 + get_cabac( &h->cabac, &h->cabac_state[39] ); /* B_L0_8x8, B_L1_8x8 */
    type = 3;
    if( get_cabac( &h->cabac, &h->cabac_state[38] ) ) {
        if( get_cabac( &h->cabac, &h->cabac_state[39] ) )
            return 11 + get_cabac( &h->cabac, &h->cabac_state[39] ); /* B_L1_4x4, B_Bi_4x4 */
        type += 4;
    }
    type += 2*get_cabac( &h->cabac, &h->cabac_state[39] );
    type +=   get_cabac( &h->cabac, &h->cabac_state[39] );
    return type;
}

static int decode_cabac_mb_ref( H264Context *h, int list, int n ) {
    int refa = h->ref_cache[list][scan8[n] - 1];
    int refb = h->ref_cache[list][scan8[n] - 8];
    int ref  = 0;
    int ctx  = 0;

    if( h->slice_type == B_TYPE) {
        if( refa > 0 && !h->direct_cache[scan8[n] - 1] )
            ctx++;
        if( refb > 0 && !h->direct_cache[scan8[n] - 8] )
            ctx += 2;
    } else {
        if( refa > 0 )
            ctx++;
        if( refb > 0 )
            ctx += 2;
    }

    while( get_cabac( &h->cabac, &h->cabac_state[54+ctx] ) ) {
        ref++;
        if( ctx < 4 )
            ctx = 4;
        else
            ctx = 5;
    }
    return ref;
}

static int decode_cabac_mb_mvd( H264Context *h, int list, int n, int l ) {
    int amvd = abs( h->mvd_cache[list][scan8[n] - 1][l] ) +
               abs( h->mvd_cache[list][scan8[n] - 8][l] );
    int ctxbase = (l == 0) ? 40 : 47;
    int ctx, mvd;

    if( amvd < 3 )
        ctx = 0;
    else if( amvd > 32 )
        ctx = 2;
    else
        ctx = 1;

    if(!get_cabac(&h->cabac, &h->cabac_state[ctxbase+ctx]))
        return 0;

    mvd= 1;
    ctx= 3;
    while( mvd < 9 && get_cabac( &h->cabac, &h->cabac_state[ctxbase+ctx] ) ) {
        mvd++;
        if( ctx < 6 )
            ctx++;
    }

    if( mvd >= 9 ) {
        int k = 3;
        while( get_cabac_bypass( &h->cabac ) ) {
            mvd += 1 << k;
            k++;
        }
        while( k-- ) {
            if( get_cabac_bypass( &h->cabac ) )
                mvd += 1 << k;
        }
    }
    if( get_cabac_bypass( &h->cabac ) )  return -mvd;
    else                                 return  mvd;
}

static int inline get_cabac_cbf_ctx( H264Context *h, int cat, int idx ) {
    int nza, nzb;
    int ctx = 0;

    if( cat == 0 ) {
        nza = h->left_cbp&0x100;
        nzb = h-> top_cbp&0x100;
    } else if( cat == 1 || cat == 2 ) {
        nza = h->non_zero_count_cache[scan8[idx] - 1];
        nzb = h->non_zero_count_cache[scan8[idx] - 8];
    } else if( cat == 3 ) {
        nza = (h->left_cbp>>(6+idx))&0x01;
        nzb = (h-> top_cbp>>(6+idx))&0x01;
    } else {
        assert(cat == 4);
        nza = h->non_zero_count_cache[scan8[16+idx] - 1];
        nzb = h->non_zero_count_cache[scan8[16+idx] - 8];
    }

    if( nza > 0 )
        ctx++;

    if( nzb > 0 )
        ctx += 2;

    return ctx + 4 * cat;
}

static int inline decode_cabac_residual( H264Context *h, DCTELEM *block, int cat, int n, const uint8_t *scantable, int qp, int max_coeff) {
    const int mb_xy  = h->s.mb_x + h->s.mb_y*h->s.mb_stride;
    const uint16_t *qmul= dequant_coeff[qp];
    static const int significant_coeff_flag_offset[5] = { 0, 15, 29, 44, 47 };
    static const int coeff_abs_level_m1_offset[5] = {227+ 0, 227+10, 227+20, 227+30, 227+39 };

    int index[16];

    int i, last;
    int coeff_count = 0;

    int abslevel1 = 1;
    int abslevelgt1 = 0;

    /* cat: 0-> DC 16x16  n = 0
     *      1-> AC 16x16  n = luma4x4idx
     *      2-> Luma4x4   n = luma4x4idx
     *      3-> DC Chroma n = iCbCr
     *      4-> AC Chroma n = 4 * iCbCr + chroma4x4idx
     */

    /* read coded block flag */
    if( get_cabac( &h->cabac, &h->cabac_state[85 + get_cabac_cbf_ctx( h, cat, n ) ] ) == 0 ) {
        if( cat == 1 || cat == 2 )
            h->non_zero_count_cache[scan8[n]] = 0;
        else if( cat == 4 )
            h->non_zero_count_cache[scan8[16+n]] = 0;

        return 0;
    }

    for(last= 0; last < max_coeff - 1; last++) {
        if( get_cabac( &h->cabac, &h->cabac_state[105+significant_coeff_flag_offset[cat]+last] )) {
            index[coeff_count++] = last;
            if( get_cabac( &h->cabac, &h->cabac_state[166+significant_coeff_flag_offset[cat]+last] ) ) {
                last= max_coeff;
                break;
            }
        }
    }
    if( last == max_coeff -1 ) {
        index[coeff_count++] = last;
    }
    assert(coeff_count > 0);

    if( cat == 0 )
        h->cbp_table[mb_xy] |= 0x100;
    else if( cat == 1 || cat == 2 )
        h->non_zero_count_cache[scan8[n]] = coeff_count;
    else if( cat == 3 )
        h->cbp_table[mb_xy] |= 0x40 << n;
    else {
        assert( cat == 4 );
        h->non_zero_count_cache[scan8[16+n]] = coeff_count;
    }

    for( i = coeff_count - 1; i >= 0; i-- ) {
        int ctx = (abslevelgt1 != 0 ? 0 : FFMIN( 4, abslevel1 )) + coeff_abs_level_m1_offset[cat];
        int j= scantable[index[i]];

        if( get_cabac( &h->cabac, &h->cabac_state[ctx] ) == 0 ) {
            if( cat == 0 || cat == 3 ) {
                if( get_cabac_bypass( &h->cabac ) ) block[j] = -1;
                else                                block[j] =  1;
            }else{
                if( get_cabac_bypass( &h->cabac ) ) block[j] = -qmul[j];
                else                                block[j] =  qmul[j];
            }
    
            abslevel1++;
        } else {
            int coeff_abs = 2;
            ctx = 5 + FFMIN( 4, abslevelgt1 ) + coeff_abs_level_m1_offset[cat];
            while( coeff_abs < 15 && get_cabac( &h->cabac, &h->cabac_state[ctx] ) ) {
                coeff_abs++;
            }

            if( coeff_abs >= 15 ) {
                int j = 0;
                while( get_cabac_bypass( &h->cabac ) ) {
                    coeff_abs += 1 << j;
                    j++;
                }
    
                while( j-- ) {
                    if( get_cabac_bypass( &h->cabac ) )
                        coeff_abs += 1 << j ;
                }
            }

            if( cat == 0 || cat == 3 ) {
                if( get_cabac_bypass( &h->cabac ) ) block[j] = -coeff_abs;
                else                                block[j] =  coeff_abs;
            }else{
                if( get_cabac_bypass( &h->cabac ) ) block[j] = -coeff_abs * qmul[j];
                else                                block[j] =  coeff_abs * qmul[j];
            }
    
            abslevelgt1++;
        }
    }
    return 0;
}

/**
 * decodes a macroblock
 * @returns 0 if ok, AC_ERROR / DC_ERROR / MV_ERROR if an error is noticed
 */
static int decode_mb_cabac(H264Context *h) {
    MpegEncContext * const s = &h->s;
    const int mb_xy= s->mb_x + s->mb_y*s->mb_stride;
    int mb_type, partition_count, cbp = 0;

    s->dsp.clear_blocks(h->mb); //FIXME avoid if allready clear (move after skip handlong?)

    if( h->sps.mb_aff ) {
        av_log( h->s.avctx, AV_LOG_ERROR, "Fields not supported with CABAC\n" );
        return -1;
    }

    if( h->slice_type != I_TYPE && h->slice_type != SI_TYPE ) {
        /* read skip flags */
        if( decode_cabac_mb_skip( h ) ) {
            decode_mb_skip(h);

            h->cbp_table[mb_xy] = 0;
            h->chroma_pred_mode_table[mb_xy] = 0;
            h->last_qscale_diff = 0;

            return 0;

        }
    }
    h->prev_mb_skiped = 0;

    if( ( mb_type = decode_cabac_mb_type( h ) ) < 0 ) {
        av_log( h->s.avctx, AV_LOG_ERROR, "decode_cabac_mb_type failed\n" );
        return -1;
    }

    if( h->slice_type == B_TYPE ) {
        if( mb_type < 23 ){
            partition_count= b_mb_type_info[mb_type].partition_count;
            mb_type=         b_mb_type_info[mb_type].type;
        }else{
            mb_type -= 23;
            goto decode_intra_mb;
        }
    } else if( h->slice_type == P_TYPE ) {
        if( mb_type < 5) {
            partition_count= p_mb_type_info[mb_type].partition_count;
            mb_type=         p_mb_type_info[mb_type].type;
        } else {
            mb_type -= 5;
            goto decode_intra_mb;
        }
    } else {
       assert(h->slice_type == I_TYPE);
decode_intra_mb:
        partition_count = 0;
        cbp= i_mb_type_info[mb_type].cbp;
        h->intra16x16_pred_mode= i_mb_type_info[mb_type].pred_mode;
        mb_type= i_mb_type_info[mb_type].type;
    }
#if 0
    if(h->mb_field_decoding_flag)
        mb_type |= MB_TYPE_INTERLACED;
#endif

    s->current_picture.mb_type[mb_xy]= mb_type;
    h->slice_table[ mb_xy ]= h->slice_num;

    if(IS_INTRA_PCM(mb_type)) {
        /* TODO */
        assert(0);
        h->cbp_table[mb_xy] = 0xf +4*2; //FIXME ?!
        h->cbp_table[mb_xy] |= 0x1C0;
        h->chroma_pred_mode_table[mb_xy] = 0;
        s->current_picture.qscale_table[mb_xy]= s->qscale;
        return -1;
    }

    fill_caches(h, mb_type);

    if( IS_INTRA( mb_type ) ) {
        if( IS_INTRA4x4( mb_type ) ) {
            int i;
            for( i = 0; i < 16; i++ ) {
                int pred = pred_intra_mode( h, i );
                h->intra4x4_pred_mode_cache[ scan8[i] ] = decode_cabac_mb_intra4x4_pred_mode( h, pred );

                //av_log( s->avctx, AV_LOG_ERROR, "i4x4 pred=%d mode=%d\n", pred, h->intra4x4_pred_mode_cache[ scan8[i] ] );
            }
            write_back_intra_pred_mode(h);
            if( check_intra4x4_pred_mode(h) < 0 ) return -1;
        } else {
            h->intra16x16_pred_mode= check_intra_pred_mode( h, h->intra16x16_pred_mode );
            if( h->intra16x16_pred_mode < 0 ) return -1;
        }
        h->chroma_pred_mode_table[mb_xy] =
            h->chroma_pred_mode          = decode_cabac_mb_chroma_pre_mode( h );

        h->chroma_pred_mode= check_intra_pred_mode( h, h->chroma_pred_mode );
        if( h->chroma_pred_mode < 0 ) return -1;
    } else if( partition_count == 4 ) {
        int i, j, sub_partition_count[4], list, ref[2][4];

        if( h->slice_type == B_TYPE ) {
            for( i = 0; i < 4; i++ ) {
                h->sub_mb_type[i] = decode_cabac_b_mb_sub_type( h );
                sub_partition_count[i]= b_sub_mb_type_info[ h->sub_mb_type[i] ].partition_count;
                h->sub_mb_type[i]=      b_sub_mb_type_info[ h->sub_mb_type[i] ].type;
            }
            if(   IS_DIRECT(h->sub_mb_type[0]) || IS_DIRECT(h->sub_mb_type[1])
               || IS_DIRECT(h->sub_mb_type[2]) || IS_DIRECT(h->sub_mb_type[3])) {
                pred_direct_motion(h, &mb_type);
                if( h->ref_count[0] > 1 || h->ref_count[1] > 1 ) {
                    for( i = 0; i < 4; i++ )
                        if( IS_DIRECT(h->sub_mb_type[i]) )
                            fill_rectangle( &h->direct_cache[scan8[4*i]], 2, 2, 8, 1, 1 );
                }
            }
        } else {
            for( i = 0; i < 4; i++ ) {
                h->sub_mb_type[i] = decode_cabac_p_mb_sub_type( h );
                sub_partition_count[i]= p_sub_mb_type_info[ h->sub_mb_type[i] ].partition_count;
                h->sub_mb_type[i]=      p_sub_mb_type_info[ h->sub_mb_type[i] ].type;
            }
        }

        for( list = 0; list < 2; list++ ) {
            if( h->ref_count[list] > 0 ) {
                for( i = 0; i < 4; i++ ) {
                    if(IS_DIRECT(h->sub_mb_type[i])) continue;
                    if(IS_DIR(h->sub_mb_type[i], 0, list)){
                        if( h->ref_count[list] > 1 )
                            ref[list][i] = decode_cabac_mb_ref( h, list, 4*i );
                        else
                            ref[list][i] = 0;
                    } else {
                        ref[list][i] = -1;
                    }
                                                       h->ref_cache[list][ scan8[4*i]+1 ]=
                    h->ref_cache[list][ scan8[4*i]+8 ]=h->ref_cache[list][ scan8[4*i]+9 ]= ref[list][i];
                }
            }
        }

        for(list=0; list<2; list++){
            for(i=0; i<4; i++){
                if(IS_DIRECT(h->sub_mb_type[i])){
                    fill_rectangle(h->mvd_cache[list][scan8[4*i]], 2, 2, 8, 0, 4);
                    continue;
                }
                h->ref_cache[list][ scan8[4*i]   ]=h->ref_cache[list][ scan8[4*i]+1 ];

                if(IS_DIR(h->sub_mb_type[i], 0, list) && !IS_DIRECT(h->sub_mb_type[i])){
                    const int sub_mb_type= h->sub_mb_type[i];
                    const int block_width= (sub_mb_type & (MB_TYPE_16x16|MB_TYPE_16x8)) ? 2 : 1;
                    for(j=0; j<sub_partition_count[i]; j++){
                        int mpx, mpy;
                        int mx, my;
                        const int index= 4*i + block_width*j;
                        int16_t (* mv_cache)[2]= &h->mv_cache[list][ scan8[index] ];
                        int16_t (* mvd_cache)[2]= &h->mvd_cache[list][ scan8[index] ];
                        pred_motion(h, index, block_width, list, h->ref_cache[list][ scan8[index] ], &mpx, &mpy);

                        mx = mpx + decode_cabac_mb_mvd( h, list, index, 0 );
                        my = mpy + decode_cabac_mb_mvd( h, list, index, 1 );
                        tprintf("final mv:%d %d\n", mx, my);

                        if(IS_SUB_8X8(sub_mb_type)){
                            mv_cache[ 0 ][0]= mv_cache[ 1 ][0]=
                            mv_cache[ 8 ][0]= mv_cache[ 9 ][0]= mx;
                            mv_cache[ 0 ][1]= mv_cache[ 1 ][1]=
                            mv_cache[ 8 ][1]= mv_cache[ 9 ][1]= my;

                            mvd_cache[ 0 ][0]= mvd_cache[ 1 ][0]=
                            mvd_cache[ 8 ][0]= mvd_cache[ 9 ][0]= mx - mpx;
                            mvd_cache[ 0 ][1]= mvd_cache[ 1 ][1]=
                            mvd_cache[ 8 ][1]= mvd_cache[ 9 ][1]= my - mpy;
                        }else if(IS_SUB_8X4(sub_mb_type)){
                            mv_cache[ 0 ][0]= mv_cache[ 1 ][0]= mx;
                            mv_cache[ 0 ][1]= mv_cache[ 1 ][1]= my;

                            mvd_cache[ 0 ][0]= mvd_cache[ 1 ][0]= mx- mpx;
                            mvd_cache[ 0 ][1]= mvd_cache[ 1 ][1]= my - mpy;
                        }else if(IS_SUB_4X8(sub_mb_type)){
                            mv_cache[ 0 ][0]= mv_cache[ 8 ][0]= mx;
                            mv_cache[ 0 ][1]= mv_cache[ 8 ][1]= my;

                            mvd_cache[ 0 ][0]= mvd_cache[ 8 ][0]= mx - mpx;
                            mvd_cache[ 0 ][1]= mvd_cache[ 8 ][1]= my - mpy;
                        }else{
                            assert(IS_SUB_4X4(sub_mb_type));
                            mv_cache[ 0 ][0]= mx;
                            mv_cache[ 0 ][1]= my;

                            mvd_cache[ 0 ][0]= mx - mpx;
                            mvd_cache[ 0 ][1]= my - mpy;
                        }
                    }
                }else{
                    uint32_t *p= (uint32_t *)&h->mv_cache[list][ scan8[4*i] ][0];
                    uint32_t *pd= (uint32_t *)&h->mvd_cache[list][ scan8[4*i] ][0];
                    p[0] = p[1] = p[8] = p[9] = 0;
                    pd[0]= pd[1]= pd[8]= pd[9]= 0;
                }
            }
        }
    } else if( IS_DIRECT(mb_type) ) {
        pred_direct_motion(h, &mb_type);
        s->current_picture.mb_type[mb_xy]= mb_type;
        fill_rectangle(h->mvd_cache[0][scan8[0]], 4, 4, 8, 0, 4);
        fill_rectangle(h->mvd_cache[1][scan8[0]], 4, 4, 8, 0, 4);
    } else {
        int list, mx, my, i, mpx, mpy;
        if(IS_16X16(mb_type)){
            for(list=0; list<2; list++){
                if(IS_DIR(mb_type, 0, list)){
                    if(h->ref_count[list] > 0 ){
                        const int ref = h->ref_count[list] > 1 ? decode_cabac_mb_ref( h, list, 0 ) : 0;
                        fill_rectangle(&h->ref_cache[list][ scan8[0] ], 4, 4, 8, ref, 1);
                    }
                }
            }
            for(list=0; list<2; list++){
                if(IS_DIR(mb_type, 0, list)){
                    pred_motion(h, 0, 4, list, h->ref_cache[list][ scan8[0] ], &mpx, &mpy);

                    mx = mpx + decode_cabac_mb_mvd( h, list, 0, 0 );
                    my = mpy + decode_cabac_mb_mvd( h, list, 0, 1 );
                    tprintf("final mv:%d %d\n", mx, my);

                    fill_rectangle(h->mvd_cache[list][ scan8[0] ], 4, 4, 8, pack16to32(mx-mpx,my-mpy), 4);
                    fill_rectangle(h->mv_cache[list][ scan8[0] ], 4, 4, 8, pack16to32(mx,my), 4);
                }
            }
        }
        else if(IS_16X8(mb_type)){
            for(list=0; list<2; list++){
                if(h->ref_count[list]>0){
                    for(i=0; i<2; i++){
                        if(IS_DIR(mb_type, i, list)){
                            const int ref= h->ref_count[list] > 1 ? decode_cabac_mb_ref( h, list, 8*i ) : 0;
                            fill_rectangle(&h->ref_cache[list][ scan8[0] + 16*i ], 4, 2, 8, ref, 1);
                        }else
                            fill_rectangle(&h->ref_cache[list][ scan8[0] + 16*i ], 4, 2, 8, (LIST_NOT_USED&0xFF), 1);
                    }
                }
            }
            for(list=0; list<2; list++){
                for(i=0; i<2; i++){
                    if(IS_DIR(mb_type, i, list)){
                        pred_16x8_motion(h, 8*i, list, h->ref_cache[list][scan8[0] + 16*i], &mpx, &mpy);
                        mx = mpx + decode_cabac_mb_mvd( h, list, 8*i, 0 );
                        my = mpy + decode_cabac_mb_mvd( h, list, 8*i, 1 );
                        tprintf("final mv:%d %d\n", mx, my);

                        fill_rectangle(h->mvd_cache[list][ scan8[0] + 16*i ], 4, 2, 8, pack16to32(mx-mpx,my-mpy), 4);
                        fill_rectangle(h->mv_cache[list][ scan8[0] + 16*i ], 4, 2, 8, pack16to32(mx,my), 4);
                    }else{ // needed only for mixed refs
                        fill_rectangle(h->mvd_cache[list][ scan8[0] + 16*i ], 4, 2, 8, 0, 4);
                        fill_rectangle(h-> mv_cache[list][ scan8[0] + 16*i ], 4, 2, 8, 0, 4);
                    }
                }
            }
        }else{
            assert(IS_8X16(mb_type));
            for(list=0; list<2; list++){
                if(h->ref_count[list]>0){
                    for(i=0; i<2; i++){
                        if(IS_DIR(mb_type, i, list)){ //FIXME optimize
                            const int ref= h->ref_count[list] > 1 ? decode_cabac_mb_ref( h, list, 4*i ) : 0;
                            fill_rectangle(&h->ref_cache[list][ scan8[0] + 2*i ], 2, 4, 8, ref, 1);
                        }else
                            fill_rectangle(&h->ref_cache[list][ scan8[0] + 2*i ], 2, 4, 8, (LIST_NOT_USED&0xFF), 1);
                    }
                }
            }
            for(list=0; list<2; list++){
                for(i=0; i<2; i++){
                    if(IS_DIR(mb_type, i, list)){
                        pred_8x16_motion(h, i*4, list, h->ref_cache[list][ scan8[0] + 2*i ], &mpx, &mpy);
                        mx = mpx + decode_cabac_mb_mvd( h, list, 4*i, 0 );
                        my = mpy + decode_cabac_mb_mvd( h, list, 4*i, 1 );

                        tprintf("final mv:%d %d\n", mx, my);
                        fill_rectangle(h->mvd_cache[list][ scan8[0] + 2*i ], 2, 4, 8, pack16to32(mx-mpx,my-mpy), 4);
                        fill_rectangle(h->mv_cache[list][ scan8[0] + 2*i ], 2, 4, 8, pack16to32(mx,my), 4);
                    }else{ // needed only for mixed refs
                        fill_rectangle(h->mvd_cache[list][ scan8[0] + 2*i ], 2, 4, 8, 0, 4);
                        fill_rectangle(h-> mv_cache[list][ scan8[0] + 2*i ], 2, 4, 8, 0, 4);
                    }
                }
            }
        }
    }

   if( IS_INTER( mb_type ) ) {
        h->chroma_pred_mode_table[mb_xy] = 0;
        write_back_motion( h, mb_type );
   }

    if( !IS_INTRA16x16( mb_type ) ) {
        cbp  = decode_cabac_mb_cbp_luma( h );
        cbp |= decode_cabac_mb_cbp_chroma( h ) << 4;
    }

    h->cbp_table[mb_xy] = cbp;

    if( cbp || IS_INTRA16x16( mb_type ) ) {
        const uint8_t *scan, *dc_scan;
        int dqp;

        if(IS_INTERLACED(mb_type)){
            scan= field_scan;
            dc_scan= luma_dc_field_scan;
        }else{
            scan= zigzag_scan;
            dc_scan= luma_dc_zigzag_scan;
        }

        h->last_qscale_diff = dqp = decode_cabac_mb_dqp( h );
        s->qscale += dqp;
        if(((unsigned)s->qscale) > 51){
            if(s->qscale<0) s->qscale+= 52;
            else            s->qscale-= 52;
        }
        h->chroma_qp = get_chroma_qp(h, s->qscale);

        if( IS_INTRA16x16( mb_type ) ) {
            int i;
            //av_log( s->avctx, AV_LOG_ERROR, "INTRA16x16 DC\n" );
            if( decode_cabac_residual( h, h->mb, 0, 0, dc_scan, s->qscale, 16) < 0)
                return -1;
            if( cbp&15 ) {
                for( i = 0; i < 16; i++ ) {
                    //av_log( s->avctx, AV_LOG_ERROR, "INTRA16x16 AC:%d\n", i );
                    if( decode_cabac_residual(h, h->mb + 16*i, 1, i, scan + 1, s->qscale, 15) < 0 )
                        return -1;
                }
            } else {
                fill_rectangle(&h->non_zero_count_cache[scan8[0]], 4, 4, 8, 0, 1);
            }
        } else {
            int i8x8, i4x4;
            for( i8x8 = 0; i8x8 < 4; i8x8++ ) {
                if( cbp & (1<<i8x8) ) {
                    for( i4x4 = 0; i4x4 < 4; i4x4++ ) {
                        const int index = 4*i8x8 + i4x4;
                        //av_log( s->avctx, AV_LOG_ERROR, "Luma4x4: %d\n", index );
                        if( decode_cabac_residual(h, h->mb + 16*index, 2, index, scan, s->qscale, 16) < 0 )
                            return -1;
                    }
                } else {
                    uint8_t * const nnz= &h->non_zero_count_cache[ scan8[4*i8x8] ];
                    nnz[0] = nnz[1] = nnz[8] = nnz[9] = 0;
                }
            }
        }

        if( cbp&0x30 ){
            int c;
            for( c = 0; c < 2; c++ ) {
                //av_log( s->avctx, AV_LOG_ERROR, "INTRA C%d-DC\n",c );
                if( decode_cabac_residual(h, h->mb + 256 + 16*4*c, 3, c, chroma_dc_scan, h->chroma_qp, 4) < 0)
                    return -1;
            }
        }

        if( cbp&0x20 ) {
            int c, i;
            for( c = 0; c < 2; c++ ) {
                for( i = 0; i < 4; i++ ) {
                    const int index = 16 + 4 * c + i;
                    //av_log( s->avctx, AV_LOG_ERROR, "INTRA C%d-AC %d\n",c, index - 16 );
                    if( decode_cabac_residual(h, h->mb + 16*index, 4, index - 16, scan + 1, h->chroma_qp, 15) < 0)
                        return -1;
                }
            }
        } else {
            uint8_t * const nnz= &h->non_zero_count_cache[0];
            nnz[ scan8[16]+0 ] = nnz[ scan8[16]+1 ] =nnz[ scan8[16]+8 ] =nnz[ scan8[16]+9 ] =
            nnz[ scan8[20]+0 ] = nnz[ scan8[20]+1 ] =nnz[ scan8[20]+8 ] =nnz[ scan8[20]+9 ] = 0;
        }
    } else {
        uint8_t * const nnz= &h->non_zero_count_cache[0];
        fill_rectangle(&nnz[scan8[0]], 4, 4, 8, 0, 1);
        nnz[ scan8[16]+0 ] = nnz[ scan8[16]+1 ] =nnz[ scan8[16]+8 ] =nnz[ scan8[16]+9 ] =
        nnz[ scan8[20]+0 ] = nnz[ scan8[20]+1 ] =nnz[ scan8[20]+8 ] =nnz[ scan8[20]+9 ] = 0;
    }

    s->current_picture.qscale_table[mb_xy]= s->qscale;
    write_back_non_zero_count(h);

    return 0;
}


static void filter_mb_edgev( H264Context *h, uint8_t *pix, int stride, int bS[4], int qp ) {
    int i, d;
    const int index_a = clip( qp + h->slice_alpha_c0_offset, 0, 51 );
    const int alpha = alpha_table[index_a];
    const int beta  = beta_table[clip( qp + h->slice_beta_offset, 0, 51 )];

    for( i = 0; i < 4; i++ ) {
        if( bS[i] == 0 ) {
            pix += 4 * stride;
            continue;
        }

        if( bS[i] < 4 ) {
            const int tc0 = tc0_table[index_a][bS[i] - 1];
            /* 4px edge length */
            for( d = 0; d < 4; d++ ) {
                const int p0 = pix[-1];
                const int p1 = pix[-2];
                const int p2 = pix[-3];
                const int q0 = pix[0];
                const int q1 = pix[1];
                const int q2 = pix[2];

                if( ABS( p0 - q0 ) < alpha &&
                    ABS( p1 - p0 ) < beta &&
                    ABS( q1 - q0 ) < beta ) {
                    int tc = tc0;
                    int i_delta;

                    if( ABS( p2 - p0 ) < beta ) {
                        pix[-2] = p1 + clip( ( p2 + ( ( p0 + q0 + 1 ) >> 1 ) - ( p1 << 1 ) ) >> 1, -tc0, tc0 );
                        tc++;
                    }
                    if( ABS( q2 - q0 ) < beta ) {
                        pix[1] = q1 + clip( ( q2 + ( ( p0 + q0 + 1 ) >> 1 ) - ( q1 << 1 ) ) >> 1, -tc0, tc0 );
                        tc++;
                    }

                    i_delta = clip( (((q0 - p0 ) << 2) + (p1 - q1) + 4) >> 3, -tc, tc );
                    pix[-1] = clip_uint8( p0 + i_delta );    /* p0' */
                    pix[0]  = clip_uint8( q0 - i_delta );    /* q0' */
                }
                pix += stride;
            }
        }else{
            /* 4px edge length */
            for( d = 0; d < 4; d++ ) {
                const int p0 = pix[-1];
                const int p1 = pix[-2];
                const int p2 = pix[-3];

                const int q0 = pix[0];
                const int q1 = pix[1];
                const int q2 = pix[2];

                if( ABS( p0 - q0 ) < alpha &&
                    ABS( p1 - p0 ) < beta &&
                    ABS( q1 - q0 ) < beta ) {

                    if(ABS( p0 - q0 ) < (( alpha >> 2 ) + 2 )){
                        if( ABS( p2 - p0 ) < beta)
                        {
                            const int p3 = pix[-4];
                            /* p0', p1', p2' */
                            pix[-1] = ( p2 + 2*p1 + 2*p0 + 2*q0 + q1 + 4 ) >> 3;
                            pix[-2] = ( p2 + p1 + p0 + q0 + 2 ) >> 2;
                            pix[-3] = ( 2*p3 + 3*p2 + p1 + p0 + q0 + 4 ) >> 3;
                        } else {
                            /* p0' */
                            pix[-1] = ( 2*p1 + p0 + q1 + 2 ) >> 2;
                        }
                        if( ABS( q2 - q0 ) < beta)
                        {
                            const int q3 = pix[3];
                            /* q0', q1', q2' */
                            pix[0] = ( p1 + 2*p0 + 2*q0 + 2*q1 + q2 + 4 ) >> 3;
                            pix[1] = ( p0 + q0 + q1 + q2 + 2 ) >> 2;
                            pix[2] = ( 2*q3 + 3*q2 + q1 + q0 + p0 + 4 ) >> 3;
                        } else {
                            /* q0' */
                            pix[0] = ( 2*q1 + q0 + p1 + 2 ) >> 2;
                        }
                    }else{
                        /* p0', q0' */
                        pix[-1] = ( 2*p1 + p0 + q1 + 2 ) >> 2;
                        pix[ 0] = ( 2*q1 + q0 + p1 + 2 ) >> 2;
                    }
                }
                pix += stride;
            }
        }
    }
}
static void filter_mb_edgecv( H264Context *h, uint8_t *pix, int stride, int bS[4], int qp ) {
    int i, d;
    const int index_a = clip( qp + h->slice_alpha_c0_offset, 0, 51 );
    const int alpha = alpha_table[index_a];
    const int beta  = beta_table[clip( qp + h->slice_beta_offset, 0, 51 )];

    for( i = 0; i < 4; i++ ) {
        if( bS[i] == 0 ) {
            pix += 2 * stride;
            continue;
        }

        if( bS[i] < 4 ) {
            const int tc = tc0_table[index_a][bS[i] - 1] + 1;
            /* 2px edge length (because we use same bS than the one for luma) */
            for( d = 0; d < 2; d++ ){
                const int p0 = pix[-1];
                const int p1 = pix[-2];
                const int q0 = pix[0];
                const int q1 = pix[1];

                if( ABS( p0 - q0 ) < alpha &&
                    ABS( p1 - p0 ) < beta &&
                    ABS( q1 - q0 ) < beta ) {
                    const int i_delta = clip( (((q0 - p0 ) << 2) + (p1 - q1) + 4) >> 3, -tc, tc );

                    pix[-1] = clip_uint8( p0 + i_delta );    /* p0' */
                    pix[0]  = clip_uint8( q0 - i_delta );    /* q0' */
                }
                pix += stride;
            }
        }else{
            /* 2px edge length (because we use same bS than the one for luma) */
            for( d = 0; d < 2; d++ ){
                const int p0 = pix[-1];
                const int p1 = pix[-2];
                const int q0 = pix[0];
                const int q1 = pix[1];

                if( ABS( p0 - q0 ) < alpha &&
                    ABS( p1 - p0 ) < beta &&
                    ABS( q1 - q0 ) < beta ) {

                    pix[-1] = ( 2*p1 + p0 + q1 + 2 ) >> 2;   /* p0' */
                    pix[0]  = ( 2*q1 + q0 + p1 + 2 ) >> 2;   /* q0' */
                }
                pix += stride;
            }
        }
    }
}

static void filter_mb_edgeh( H264Context *h, uint8_t *pix, int stride, int bS[4], int qp ) {
    int i, d;
    const int index_a = clip( qp + h->slice_alpha_c0_offset, 0, 51 );
    const int alpha = alpha_table[index_a];
    const int beta  = beta_table[clip( qp + h->slice_beta_offset, 0, 51 )];
    const int pix_next  = stride;

    for( i = 0; i < 4; i++ ) {
        if( bS[i] == 0 ) {
            pix += 4;
            continue;
        }

        if( bS[i] < 4 ) {
            const int tc0 = tc0_table[index_a][bS[i] - 1];
            /* 4px edge length */
            for( d = 0; d < 4; d++ ) {
                const int p0 = pix[-1*pix_next];
                const int p1 = pix[-2*pix_next];
                const int p2 = pix[-3*pix_next];
                const int q0 = pix[0];
                const int q1 = pix[1*pix_next];
                const int q2 = pix[2*pix_next];

                if( ABS( p0 - q0 ) < alpha &&
                    ABS( p1 - p0 ) < beta &&
                    ABS( q1 - q0 ) < beta ) {

                    int tc = tc0;
                    int i_delta;

                    if( ABS( p2 - p0 ) < beta ) {
                        pix[-2*pix_next] = p1 + clip( ( p2 + ( ( p0 + q0 + 1 ) >> 1 ) - ( p1 << 1 ) ) >> 1, -tc0, tc0 );
                        tc++;
                    }
                    if( ABS( q2 - q0 ) < beta ) {
                        pix[pix_next] = q1 + clip( ( q2 + ( ( p0 + q0 + 1 ) >> 1 ) - ( q1 << 1 ) ) >> 1, -tc0, tc0 );
                        tc++;
                    }

                    i_delta = clip( (((q0 - p0 ) << 2) + (p1 - q1) + 4) >> 3, -tc, tc );
                    pix[-pix_next] = clip_uint8( p0 + i_delta );    /* p0' */
                    pix[0]         = clip_uint8( q0 - i_delta );    /* q0' */
                }
                pix++;
            }
        }else{
            /* 4px edge length */
            for( d = 0; d < 4; d++ ) {
                const int p0 = pix[-1*pix_next];
                const int p1 = pix[-2*pix_next];
                const int p2 = pix[-3*pix_next];
                const int q0 = pix[0];
                const int q1 = pix[1*pix_next];
                const int q2 = pix[2*pix_next];

                if( ABS( p0 - q0 ) < alpha &&
                    ABS( p1 - p0 ) < beta &&
                    ABS( q1 - q0 ) < beta ) {

                    const int p3 = pix[-4*pix_next];
                    const int q3 = pix[ 3*pix_next];

                    if(ABS( p0 - q0 ) < (( alpha >> 2 ) + 2 )){
                        if( ABS( p2 - p0 ) < beta) {
                            /* p0', p1', p2' */
                            pix[-1*pix_next] = ( p2 + 2*p1 + 2*p0 + 2*q0 + q1 + 4 ) >> 3;
                            pix[-2*pix_next] = ( p2 + p1 + p0 + q0 + 2 ) >> 2;
                            pix[-3*pix_next] = ( 2*p3 + 3*p2 + p1 + p0 + q0 + 4 ) >> 3;
                        } else {
                            /* p0' */
                            pix[-1*pix_next] = ( 2*p1 + p0 + q1 + 2 ) >> 2;
                        }
                        if( ABS( q2 - q0 ) < beta) {
                            /* q0', q1', q2' */
                            pix[0*pix_next] = ( p1 + 2*p0 + 2*q0 + 2*q1 + q2 + 4 ) >> 3;
                            pix[1*pix_next] = ( p0 + q0 + q1 + q2 + 2 ) >> 2;
                            pix[2*pix_next] = ( 2*q3 + 3*q2 + q1 + q0 + p0 + 4 ) >> 3;
                        } else {
                            /* q0' */
                            pix[0*pix_next] = ( 2*q1 + q0 + p1 + 2 ) >> 2;
                        }
                    }else{
                        /* p0', q0' */
                        pix[-1*pix_next] = ( 2*p1 + p0 + q1 + 2 ) >> 2;
                        pix[ 0*pix_next] = ( 2*q1 + q0 + p1 + 2 ) >> 2;
                    }
                }
                pix++;
            }
        }
    }
}

static void filter_mb_edgech( H264Context *h, uint8_t *pix, int stride, int bS[4], int qp ) {
    int i, d;
    const int index_a = clip( qp + h->slice_alpha_c0_offset, 0, 51 );
    const int alpha = alpha_table[index_a];
    const int beta  = beta_table[clip( qp + h->slice_beta_offset, 0, 51 )];
    const int pix_next  = stride;

    for( i = 0; i < 4; i++ )
    {
        if( bS[i] == 0 ) {
            pix += 2;
            continue;
        }

        if( bS[i] < 4 ) {
            int tc = tc0_table[index_a][bS[i] - 1] + 1;
            /* 2px edge length (see deblocking_filter_edgecv) */
            for( d = 0; d < 2; d++ ) {
                const int p0 = pix[-1*pix_next];
                const int p1 = pix[-2*pix_next];
                const int q0 = pix[0];
                const int q1 = pix[1*pix_next];

                if( ABS( p0 - q0 ) < alpha &&
                    ABS( p1 - p0 ) < beta &&
                    ABS( q1 - q0 ) < beta ) {

                    int i_delta = clip( (((q0 - p0 ) << 2) + (p1 - q1) + 4) >> 3, -tc, tc );

                    pix[-pix_next] = clip_uint8( p0 + i_delta );    /* p0' */
                    pix[0]         = clip_uint8( q0 - i_delta );    /* q0' */
                }
                pix++;
            }
        }else{
            /* 2px edge length (see deblocking_filter_edgecv) */
            for( d = 0; d < 2; d++ ) {
                const int p0 = pix[-1*pix_next];
                const int p1 = pix[-2*pix_next];
                const int q0 = pix[0];
                const int q1 = pix[1*pix_next];

                if( ABS( p0 - q0 ) < alpha &&
                    ABS( p1 - p0 ) < beta &&
                    ABS( q1 - q0 ) < beta ) {

                    pix[-pix_next] = ( 2*p1 + p0 + q1 + 2 ) >> 2;   /* p0' */
                    pix[0]         = ( 2*q1 + q0 + p1 + 2 ) >> 2;   /* q0' */
                }
                pix++;
            }
        }
    }
}

static void filter_mb( H264Context *h, int mb_x, int mb_y, uint8_t *img_y, uint8_t *img_cb, uint8_t *img_cr) {
    MpegEncContext * const s = &h->s;
    const int mb_xy= mb_x + mb_y*s->mb_stride;
    int linesize, uvlinesize;
    int dir;

    /* FIXME Implement deblocking filter for field MB */
    if( h->sps.mb_aff ) {
        return;
    }
    linesize = s->linesize;
    uvlinesize = s->uvlinesize;

    /* dir : 0 -> vertical edge, 1 -> horizontal edge */
    for( dir = 0; dir < 2; dir++ )
    {
        int start = 0;
        int edge;

        /* test picture boundary */
        if( ( dir == 0 && mb_x == 0 ) || ( dir == 1 && mb_y == 0 ) ) {
            start = 1;
        }
        if( 0 == start && 2 == h->deblocking_filter) {
            const int mbn_xy = dir == 0 ? mb_xy -1 : mb_xy - s->mb_stride;
            if (h->slice_table[mbn_xy] != h->slice_table[mb_xy]) {
                start = 1;
            }
        }

        /* Calculate bS */
        for( edge = start; edge < 4; edge++ ) {
            /* mbn_xy: neighbour macroblock (how that works for field ?) */
            int mbn_xy = edge > 0 ? mb_xy : ( dir == 0 ? mb_xy -1 : mb_xy - s->mb_stride );
            int bS[4];
            int qp;

            if( IS_INTRA( s->current_picture.mb_type[mb_xy] ) ||
                IS_INTRA( s->current_picture.mb_type[mbn_xy] ) ) {
                bS[0] = bS[1] = bS[2] = bS[3] = ( edge == 0 ? 4 : 3 );
            } else {
                int i;
                const int slice_boundary = (h->slice_table[mbn_xy] != h->slice_table[mb_xy]);
                for( i = 0; i < 4; i++ ) {
                    int x = dir == 0 ? edge : i;
                    int y = dir == 0 ? i    : edge;
                    int b_idx= 8 + 4 + x + 8*y;
                    int bn_idx= b_idx - (dir ? 8:1);
                    uint8_t left_non_zero_count;
                    if (slice_boundary) {
                        // must not use non_zero_count_cache, it is not valid
                        // across slice boundaries
                        if (0 == dir) {
                            left_non_zero_count = h->non_zero_count[mbn_xy][6-i];
                        } else {
                            left_non_zero_count = h->non_zero_count[mbn_xy][i];
                        }
                    } else {
                        left_non_zero_count = h->non_zero_count_cache[bn_idx];
                    }

                    if( h->non_zero_count_cache[b_idx] != 0 ||
                        left_non_zero_count != 0 ) {
                        bS[i] = 2;
                    }
                    else if( h->slice_type == P_TYPE ) {
                        int16_t left_mv[2];
                        int8_t  left_ref;
                        if (slice_boundary) {
                            // must not use ref_cache and mv_cache, they are not 
                            // valid across slice boundaries
                            if (dir == 0) {
                                left_ref = s->current_picture.ref_index[0][h->mb2b8_xy[mbn_xy] + (i>>1) * h->b8_stride + 1];
                                *(uint32_t*)left_mv = *(uint32_t*)s->current_picture.motion_val[0][h->mb2b_xy[mbn_xy]+i*h->b_stride+3];
                            } else {
                                left_ref = s->current_picture.ref_index[0][h->mb2b8_xy[mbn_xy] + (i>>1) + h->b8_stride];
                                *(uint32_t*)left_mv = *(uint32_t*)s->current_picture.motion_val[0][h->mb2b_xy[mbn_xy]+3*h->b_stride+i];
                            }
                        } else {
                            left_ref = h->ref_cache[0][bn_idx];
                            *(uint32_t*)left_mv = *(uint32_t*)h->mv_cache[0][bn_idx];
                        }
                        if( h->ref_cache[0][b_idx] != left_ref ||
                            ABS( h->mv_cache[0][b_idx][0] - left_mv[0] ) >= 4 ||
                            ABS( h->mv_cache[0][b_idx][1] - left_mv[1] ) >= 4 )
                            bS[i] = 1;
                        else
                            bS[i] = 0;
                    }
                    else {
                        /* FIXME Add support for B frame */
                        return;
                    }
                }

                if(bS[0]+bS[1]+bS[2]+bS[3] == 0)
                    continue;
            }

            /* Filter edge */
            qp = ( s->qscale + s->current_picture.qscale_table[mbn_xy] + 1 ) >> 1;
            if( dir == 0 ) {
                filter_mb_edgev( h, &img_y[4*edge], linesize, bS, qp );
                if( (edge&1) == 0 ) {
                    int chroma_qp = ( h->chroma_qp +
                                      get_chroma_qp( h, s->current_picture.qscale_table[mbn_xy] ) + 1 ) >> 1;
                    filter_mb_edgecv( h, &img_cb[2*edge], uvlinesize, bS, chroma_qp );
                    filter_mb_edgecv( h, &img_cr[2*edge], uvlinesize, bS, chroma_qp );
                }
            } else {
                filter_mb_edgeh( h, &img_y[4*edge*linesize], linesize, bS, qp );
                if( (edge&1) == 0 ) {
                    int chroma_qp = ( h->chroma_qp +
                                      get_chroma_qp( h, s->current_picture.qscale_table[mbn_xy] ) + 1 ) >> 1;
                    filter_mb_edgech( h, &img_cb[2*edge*uvlinesize], uvlinesize, bS, chroma_qp );
                    filter_mb_edgech( h, &img_cr[2*edge*uvlinesize], uvlinesize, bS, chroma_qp );
                }
            }
        }
    }
}

static int decode_slice(H264Context *h){
    MpegEncContext * const s = &h->s;
    const int part_mask= s->partitioned_frame ? (AC_END|AC_ERROR) : 0x7F;

    s->mb_skip_run= -1;

    if( h->pps.cabac ) {
        int i;

        /* realign */
        align_get_bits( &s->gb );

        /* init cabac */
        ff_init_cabac_states( &h->cabac, ff_h264_lps_range, ff_h264_mps_state, ff_h264_lps_state, 64 );
        ff_init_cabac_decoder( &h->cabac,
                               s->gb.buffer + get_bits_count(&s->gb)/8,
                               ( s->gb.size_in_bits - get_bits_count(&s->gb) + 7)/8);
        /* calculate pre-state */
        for( i= 0; i < 399; i++ ) {
            int pre;
            if( h->slice_type == I_TYPE )
                pre = clip( ((cabac_context_init_I[i][0] * s->qscale) >>4 ) + cabac_context_init_I[i][1], 1, 126 );
            else
                pre = clip( ((cabac_context_init_PB[h->cabac_init_idc][i][0] * s->qscale) >>4 ) + cabac_context_init_PB[h->cabac_init_idc][i][1], 1, 126 );

            if( pre <= 63 )
                h->cabac_state[i] = 2 * ( 63 - pre ) + 0;
            else
                h->cabac_state[i] = 2 * ( pre - 64 ) + 1;
        }

        for(;;){
            int ret = decode_mb_cabac(h);
            int eos = get_cabac_terminate( &h->cabac ); /* End of Slice flag */

            if(ret>=0) hl_decode_mb(h);

            /* XXX: useless as decode_mb_cabac it doesn't support that ... */
            if( ret >= 0 && h->sps.mb_aff ) { //FIXME optimal? or let mb_decode decode 16x32 ?
                s->mb_y++;

                if(ret>=0) ret = decode_mb_cabac(h);
                eos = get_cabac_terminate( &h->cabac );

                hl_decode_mb(h);
                s->mb_y--;
            }

            if( ret < 0 || h->cabac.bytestream > h->cabac.bytestream_end + 1) {
                av_log(h->s.avctx, AV_LOG_ERROR, "error while decoding MB %d %d\n", s->mb_x, s->mb_y);
                ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x, s->mb_y, (AC_ERROR|DC_ERROR|MV_ERROR)&part_mask);
                return -1;
            }

            if( ++s->mb_x >= s->mb_width ) {
                s->mb_x = 0;
                ff_draw_horiz_band(s, 16*s->mb_y, 16);
                ++s->mb_y;
            }

            if( eos || s->mb_y >= s->mb_height ) {
                tprintf("slice end %d %d\n", get_bits_count(&s->gb), s->gb.size_in_bits);
                ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x-1, s->mb_y, (AC_END|DC_END|MV_END)&part_mask);
                return 0;
            }
#if 0
            /* TODO test over-reading in cabac code */
            else if( read too much in h->cabac ) {
                ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x, s->mb_y, (AC_ERROR|DC_ERROR|MV_ERROR)&part_mask);
                return -1;
            }
#endif
        }

    } else {
        for(;;){
            int ret = decode_mb_cavlc(h);

            if(ret>=0) hl_decode_mb(h);

            if(ret>=0 && h->sps.mb_aff){ //FIXME optimal? or let mb_decode decode 16x32 ?
                s->mb_y++;
                ret = decode_mb_cavlc(h);

                if(ret>=0) hl_decode_mb(h);
                s->mb_y--;
            }

            if(ret<0){
                av_log(h->s.avctx, AV_LOG_ERROR, "error while decoding MB %d %d\n", s->mb_x, s->mb_y);
                ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x, s->mb_y, (AC_ERROR|DC_ERROR|MV_ERROR)&part_mask);

                return -1;
            }

            if(++s->mb_x >= s->mb_width){
                s->mb_x=0;
                ff_draw_horiz_band(s, 16*s->mb_y, 16);
                if(++s->mb_y >= s->mb_height){
                    tprintf("slice end %d %d\n", get_bits_count(&s->gb), s->gb.size_in_bits);

                    if(get_bits_count(&s->gb) == s->gb.size_in_bits ) {
                        ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x-1, s->mb_y, (AC_END|DC_END|MV_END)&part_mask);

                        return 0;
                    }else{
                        ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x, s->mb_y, (AC_END|DC_END|MV_END)&part_mask);

                        return -1;
                    }
                }
            }

            if(get_bits_count(&s->gb) >= s->gb.size_in_bits && s->mb_skip_run<=0){
                tprintf("slice end %d %d\n", get_bits_count(&s->gb), s->gb.size_in_bits);
                if(get_bits_count(&s->gb) == s->gb.size_in_bits ){
                    ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x-1, s->mb_y, (AC_END|DC_END|MV_END)&part_mask);

                    return 0;
                }else{
                    ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x, s->mb_y, (AC_ERROR|DC_ERROR|MV_ERROR)&part_mask);

                    return -1;
                }
            }
        }
    }

#if 0
    for(;s->mb_y < s->mb_height; s->mb_y++){
        for(;s->mb_x < s->mb_width; s->mb_x++){
            int ret= decode_mb(h);
            
            hl_decode_mb(h);

            if(ret<0){
                fprintf(stderr, "error while decoding MB %d %d\n", s->mb_x, s->mb_y);
                ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x, s->mb_y, (AC_ERROR|DC_ERROR|MV_ERROR)&part_mask);

                return -1;
            }
        
            if(++s->mb_x >= s->mb_width){
                s->mb_x=0;
                if(++s->mb_y >= s->mb_height){
                    if(get_bits_count(s->gb) == s->gb.size_in_bits){
                        ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x-1, s->mb_y, (AC_END|DC_END|MV_END)&part_mask);

                        return 0;
                    }else{
                        ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x, s->mb_y, (AC_END|DC_END|MV_END)&part_mask);

                        return -1;
                    }
                }
            }
        
            if(get_bits_count(s->?gb) >= s->gb?.size_in_bits){
                if(get_bits_count(s->gb) == s->gb.size_in_bits){
                    ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x-1, s->mb_y, (AC_END|DC_END|MV_END)&part_mask);

                    return 0;
                }else{
                    ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x, s->mb_y, (AC_ERROR|DC_ERROR|MV_ERROR)&part_mask);

                    return -1;
                }
            }
        }
        s->mb_x=0;
        ff_draw_horiz_band(s, 16*s->mb_y, 16);
    }
#endif
    return -1; //not reached
}

static inline int decode_vui_parameters(H264Context *h, SPS *sps){
    MpegEncContext * const s = &h->s;
    int aspect_ratio_info_present_flag, aspect_ratio_idc;

    aspect_ratio_info_present_flag= get_bits1(&s->gb);
    
    if( aspect_ratio_info_present_flag ) {
        aspect_ratio_idc= get_bits(&s->gb, 8);
        if( aspect_ratio_idc == EXTENDED_SAR ) {
            sps->sar.num= get_bits(&s->gb, 16);
            sps->sar.den= get_bits(&s->gb, 16);
        }else if(aspect_ratio_idc < 16){
            sps->sar=  pixel_aspect[aspect_ratio_idc];
        }else{
            av_log(h->s.avctx, AV_LOG_ERROR, "illegal aspect ratio\n");
            return -1;
        }
    }else{
        sps->sar.num= 
        sps->sar.den= 0;
    }
//            s->avctx->aspect_ratio= sar_width*s->width / (float)(s->height*sar_height);

    if(get_bits1(&s->gb)){      /* overscan_info_present_flag */
        get_bits1(&s->gb);      /* overscan_appropriate_flag */
    }

    if(get_bits1(&s->gb)){      /* video_signal_type_present_flag */
        get_bits(&s->gb, 3);    /* video_format */
        get_bits1(&s->gb);      /* video_full_range_flag */
        if(get_bits1(&s->gb)){  /* colour_description_present_flag */
            get_bits(&s->gb, 8); /* colour_primaries */
            get_bits(&s->gb, 8); /* transfer_characteristics */
            get_bits(&s->gb, 8); /* matrix_coefficients */
        }
    }

    if(get_bits1(&s->gb)){      /* chroma_location_info_present_flag */
        get_ue_golomb(&s->gb);  /* chroma_sample_location_type_top_field */
        get_ue_golomb(&s->gb);  /* chroma_sample_location_type_bottom_field */
    }

    sps->timing_info_present_flag = get_bits1(&s->gb);
    if(sps->timing_info_present_flag){
        sps->num_units_in_tick = get_bits_long(&s->gb, 32);
        sps->time_scale = get_bits_long(&s->gb, 32);
        sps->fixed_frame_rate_flag = get_bits1(&s->gb);
    }

#if 0
| nal_hrd_parameters_present_flag                   |0  |u(1)    |
| if( nal_hrd_parameters_present_flag  = =  1)      |   |        |
|  hrd_parameters( )                                |   |        |
| vcl_hrd_parameters_present_flag                   |0  |u(1)    |
| if( vcl_hrd_parameters_present_flag  = =  1)      |   |        |
|  hrd_parameters( )                                |   |        |
| if( ( nal_hrd_parameters_present_flag  = =  1  | ||   |        |
|                                                   |   |        |
|( vcl_hrd_parameters_present_flag  = =  1 ) )      |   |        |
|  low_delay_hrd_flag                               |0  |u(1)    |
| bitstream_restriction_flag                        |0  |u(1)    |
| if( bitstream_restriction_flag ) {                |0  |u(1)    |
|  motion_vectors_over_pic_boundaries_flag          |0  |u(1)    |
|  max_bytes_per_pic_denom                          |0  |ue(v)   |
|  max_bits_per_mb_denom                            |0  |ue(v)   |
|  log2_max_mv_length_horizontal                    |0  |ue(v)   |
|  log2_max_mv_length_vertical                      |0  |ue(v)   |
|  num_reorder_frames                               |0  |ue(v)   |
|  max_dec_frame_buffering                          |0  |ue(v)   |
| }                                                 |   |        |
|}                                                  |   |        |
#endif
    return 0;
}

static inline int decode_seq_parameter_set(H264Context *h){
    MpegEncContext * const s = &h->s;
    int profile_idc, level_idc;
    int sps_id, i;
    SPS *sps;
    
    profile_idc= get_bits(&s->gb, 8);
    get_bits1(&s->gb);   //constraint_set0_flag
    get_bits1(&s->gb);   //constraint_set1_flag
    get_bits1(&s->gb);   //constraint_set2_flag
    get_bits1(&s->gb);   //constraint_set3_flag
    get_bits(&s->gb, 4); // reserved
    level_idc= get_bits(&s->gb, 8);
    sps_id= get_ue_golomb(&s->gb);
    
    sps= &h->sps_buffer[ sps_id ];
    sps->profile_idc= profile_idc;
    sps->level_idc= level_idc;

    sps->log2_max_frame_num= get_ue_golomb(&s->gb) + 4;
    sps->poc_type= get_ue_golomb(&s->gb);
    
    if(sps->poc_type == 0){ //FIXME #define
        sps->log2_max_poc_lsb= get_ue_golomb(&s->gb) + 4;
    } else if(sps->poc_type == 1){//FIXME #define
        sps->delta_pic_order_always_zero_flag= get_bits1(&s->gb);
        sps->offset_for_non_ref_pic= get_se_golomb(&s->gb);
        sps->offset_for_top_to_bottom_field= get_se_golomb(&s->gb);
        sps->poc_cycle_length= get_ue_golomb(&s->gb);
        
        for(i=0; i<sps->poc_cycle_length; i++)
            sps->offset_for_ref_frame[i]= get_se_golomb(&s->gb);
    }
    if(sps->poc_type > 2){
        av_log(h->s.avctx, AV_LOG_ERROR, "illegal POC type %d\n", sps->poc_type);
        return -1;
    }

    sps->ref_frame_count= get_ue_golomb(&s->gb);
    if(sps->ref_frame_count > MAX_PICTURE_COUNT-2){
        av_log(h->s.avctx, AV_LOG_ERROR, "too many reference frames\n");
    }
    sps->gaps_in_frame_num_allowed_flag= get_bits1(&s->gb);
    sps->mb_width= get_ue_golomb(&s->gb) + 1;
    sps->mb_height= get_ue_golomb(&s->gb) + 1;
    sps->frame_mbs_only_flag= get_bits1(&s->gb);
    if(!sps->frame_mbs_only_flag)
        sps->mb_aff= get_bits1(&s->gb);
    else
        sps->mb_aff= 0;

    sps->direct_8x8_inference_flag= get_bits1(&s->gb);

    sps->crop= get_bits1(&s->gb);
    if(sps->crop){
        sps->crop_left  = get_ue_golomb(&s->gb);
        sps->crop_right = get_ue_golomb(&s->gb);
        sps->crop_top   = get_ue_golomb(&s->gb);
        sps->crop_bottom= get_ue_golomb(&s->gb);
        if(sps->crop_left || sps->crop_top){
            av_log(h->s.avctx, AV_LOG_ERROR, "insane cropping not completly supported, this could look slightly wrong ...\n");
        }
    }else{
        sps->crop_left  = 
        sps->crop_right = 
        sps->crop_top   = 
        sps->crop_bottom= 0;
    }

    sps->vui_parameters_present_flag= get_bits1(&s->gb);
    if( sps->vui_parameters_present_flag )
        decode_vui_parameters(h, sps);
    
    if(s->avctx->debug&FF_DEBUG_PICT_INFO){
        av_log(h->s.avctx, AV_LOG_DEBUG, "sps:%d profile:%d/%d poc:%d ref:%d %dx%d %s %s crop:%d/%d/%d/%d %s\n", 
               sps_id, sps->profile_idc, sps->level_idc,
               sps->poc_type,
               sps->ref_frame_count,
               sps->mb_width, sps->mb_height,
               sps->frame_mbs_only_flag ? "FRM" : (sps->mb_aff ? "MB-AFF" : "PIC-AFF"),
               sps->direct_8x8_inference_flag ? "8B8" : "",
               sps->crop_left, sps->crop_right, 
               sps->crop_top, sps->crop_bottom, 
               sps->vui_parameters_present_flag ? "VUI" : ""
               );
    }
    return 0;
}

static inline int decode_picture_parameter_set(H264Context *h){
    MpegEncContext * const s = &h->s;
    int pps_id= get_ue_golomb(&s->gb);
    PPS *pps= &h->pps_buffer[pps_id];
    
    pps->sps_id= get_ue_golomb(&s->gb);
    pps->cabac= get_bits1(&s->gb);
    pps->pic_order_present= get_bits1(&s->gb);
    pps->slice_group_count= get_ue_golomb(&s->gb) + 1;
    if(pps->slice_group_count > 1 ){
        pps->mb_slice_group_map_type= get_ue_golomb(&s->gb);
        av_log(h->s.avctx, AV_LOG_ERROR, "FMO not supported\n");
        switch(pps->mb_slice_group_map_type){
        case 0:
#if 0
|   for( i = 0; i <= num_slice_groups_minus1; i++ ) |   |        |
|    run_length[ i ]                                |1  |ue(v)   |
#endif
            break;
        case 2:
#if 0
|   for( i = 0; i < num_slice_groups_minus1; i++ )  |   |        |
|{                                                  |   |        |
|    top_left_mb[ i ]                               |1  |ue(v)   |
|    bottom_right_mb[ i ]                           |1  |ue(v)   |
|   }                                               |   |        |
#endif
            break;
        case 3:
        case 4:
        case 5:
#if 0
|   slice_group_change_direction_flag               |1  |u(1)    |
|   slice_group_change_rate_minus1                  |1  |ue(v)   |
#endif
            break;
        case 6:
#if 0
|   slice_group_id_cnt_minus1                       |1  |ue(v)   |
|   for( i = 0; i <= slice_group_id_cnt_minus1; i++ |   |        |
|)                                                  |   |        |
|    slice_group_id[ i ]                            |1  |u(v)    |
#endif
            break;
        }
    }
    pps->ref_count[0]= get_ue_golomb(&s->gb) + 1;
    pps->ref_count[1]= get_ue_golomb(&s->gb) + 1;
    if(pps->ref_count[0] > 32 || pps->ref_count[1] > 32){
        av_log(h->s.avctx, AV_LOG_ERROR, "reference overflow (pps)\n");
        return -1;
    }
    
    pps->weighted_pred= get_bits1(&s->gb);
    pps->weighted_bipred_idc= get_bits(&s->gb, 2);
    pps->init_qp= get_se_golomb(&s->gb) + 26;
    pps->init_qs= get_se_golomb(&s->gb) + 26;
    pps->chroma_qp_index_offset= get_se_golomb(&s->gb);
    pps->deblocking_filter_parameters_present= get_bits1(&s->gb);
    pps->constrained_intra_pred= get_bits1(&s->gb);
    pps->redundant_pic_cnt_present = get_bits1(&s->gb);
    
    if(s->avctx->debug&FF_DEBUG_PICT_INFO){
        av_log(h->s.avctx, AV_LOG_DEBUG, "pps:%d sps:%d %s slice_groups:%d ref:%d/%d %s qp:%d/%d/%d %s %s %s\n", 
               pps_id, pps->sps_id,
               pps->cabac ? "CABAC" : "CAVLC",
               pps->slice_group_count,
               pps->ref_count[0], pps->ref_count[1],
               pps->weighted_pred ? "weighted" : "",
               pps->init_qp, pps->init_qs, pps->chroma_qp_index_offset,
               pps->deblocking_filter_parameters_present ? "LPAR" : "",
               pps->constrained_intra_pred ? "CONSTR" : "",
               pps->redundant_pic_cnt_present ? "REDU" : ""
               );
    }
    
    return 0;
}

/**
 * finds the end of the current frame in the bitstream.
 * @return the position of the first byte of the next frame, or -1
 */
static int find_frame_end(H264Context *h, const uint8_t *buf, int buf_size){
    int i;
    uint32_t state;
    ParseContext *pc = &(h->s.parse_context);
//printf("first %02X%02X%02X%02X\n", buf[0], buf[1],buf[2],buf[3]);
//    mb_addr= pc->mb_addr - 1;
    state= pc->state;
    for(i=0; i<=buf_size; i++){
        if((state&0xFFFFFF1F) == 0x101 || (state&0xFFFFFF1F) == 0x102 || (state&0xFFFFFF1F) == 0x105){
            tprintf("find_frame_end new startcode = %08x, frame_start_found = %d, pos = %d\n", state, pc->frame_start_found, i);
            if(pc->frame_start_found){
                // If there isn't one more byte in the buffer
                // the test on first_mb_in_slice cannot be done yet
                // do it at next call.
                if (i >= buf_size) break;
                if (buf[i] & 0x80) {
                    // first_mb_in_slice is 0, probably the first nal of a new
                    // slice
                    tprintf("find_frame_end frame_end_found, state = %08x, pos = %d\n", state, i);
                    pc->state=-1; 
                    pc->frame_start_found= 0;
                    return i-4;
                }
            }
            pc->frame_start_found = 1;
        }
        if (i<buf_size)
            state= (state<<8) | buf[i];
    }
    
    pc->state= state;
    return END_NOT_FOUND;
}

static int h264_parse(AVCodecParserContext *s,
                      AVCodecContext *avctx,
                      uint8_t **poutbuf, int *poutbuf_size, 
                      const uint8_t *buf, int buf_size)
{
    H264Context *h = s->priv_data;
    ParseContext *pc = &h->s.parse_context;
    int next;
    
    next= find_frame_end(h, buf, buf_size);

    if (ff_combine_frame(pc, next, (uint8_t **)&buf, &buf_size) < 0) {
        *poutbuf = NULL;
        *poutbuf_size = 0;
        return buf_size;
    }

    *poutbuf = (uint8_t *)buf;
    *poutbuf_size = buf_size;
    return next;
}

static int decode_nal_units(H264Context *h, uint8_t *buf, int buf_size){
    MpegEncContext * const s = &h->s;
    AVCodecContext * const avctx= s->avctx;
    int buf_index=0;
#if 0
    int i;
    for(i=0; i<32; i++){
        printf("%X ", buf[i]);
    }
#endif
    h->slice_num = 0;
    for(;;){
        int consumed;
        int dst_length;
        int bit_length;
        uint8_t *ptr;
        int i, nalsize = 0;
        
      if(h->is_avc) {
        if(buf_index >= buf_size) break;
        nalsize = 0;
        for(i = 0; i < h->nal_length_size; i++)
            nalsize = (nalsize << 8) | buf[buf_index++];
      } else {
        // start code prefix search
        for(; buf_index + 3 < buf_size; buf_index++){
            // this should allways succeed in the first iteration
            if(buf[buf_index] == 0 && buf[buf_index+1] == 0 && buf[buf_index+2] == 1)
                break;
        }
        
        if(buf_index+3 >= buf_size) break;
        
        buf_index+=3;
      }  
        
        ptr= decode_nal(h, buf + buf_index, &dst_length, &consumed, h->is_avc ? nalsize : buf_size - buf_index);
        if(ptr[dst_length - 1] == 0) dst_length--;
        bit_length= 8*dst_length - decode_rbsp_trailing(ptr + dst_length - 1);

        if(s->avctx->debug&FF_DEBUG_STARTCODE){
            av_log(h->s.avctx, AV_LOG_DEBUG, "NAL %d at %d/%d length %d\n", h->nal_unit_type, buf_index, buf_size, dst_length);
        }
        
        if (h->is_avc && (nalsize != consumed))
            av_log(h->s.avctx, AV_LOG_ERROR, "AVC: Consumed only %d bytes instead of %d\n", consumed, nalsize);

        buf_index += consumed;

        if( s->hurry_up == 1 && h->nal_ref_idc  == 0 )
            continue;
        
        switch(h->nal_unit_type){
        case NAL_IDR_SLICE:
            idr(h); //FIXME ensure we dont loose some frames if there is reordering
        case NAL_SLICE:
            init_get_bits(&s->gb, ptr, bit_length);
            h->intra_gb_ptr=
            h->inter_gb_ptr= &s->gb;
            s->data_partitioning = 0;
            
            if(decode_slice_header(h) < 0) return -1;
            if(h->redundant_pic_count==0 && s->hurry_up < 5 )
                decode_slice(h);
            break;
        case NAL_DPA:
            init_get_bits(&s->gb, ptr, bit_length);
            h->intra_gb_ptr=
            h->inter_gb_ptr= NULL;
            s->data_partitioning = 1;
            
            if(decode_slice_header(h) < 0) return -1;
            break;
        case NAL_DPB:
            init_get_bits(&h->intra_gb, ptr, bit_length);
            h->intra_gb_ptr= &h->intra_gb;
            break;
        case NAL_DPC:
            init_get_bits(&h->inter_gb, ptr, bit_length);
            h->inter_gb_ptr= &h->inter_gb;

            if(h->redundant_pic_count==0 && h->intra_gb_ptr && s->data_partitioning && s->hurry_up < 5 )
                decode_slice(h);
            break;
        case NAL_SEI:
            break;
        case NAL_SPS:
            init_get_bits(&s->gb, ptr, bit_length);
            decode_seq_parameter_set(h);
            
            if(s->flags& CODEC_FLAG_LOW_DELAY)
                s->low_delay=1;
      
            avctx->has_b_frames= !s->low_delay;
            break;
        case NAL_PPS:
            init_get_bits(&s->gb, ptr, bit_length);
            
            decode_picture_parameter_set(h);

            break;
        case NAL_PICTURE_DELIMITER:
            break;
        case NAL_FILTER_DATA:
            break;
	default:
	    av_log(avctx, AV_LOG_ERROR, "Unknown NAL code: %d\n", h->nal_unit_type);
        }        

        //FIXME move after where irt is set
        s->current_picture.pict_type= s->pict_type;
        s->current_picture.key_frame= s->pict_type == I_TYPE;
    }
    
    if(!s->current_picture_ptr) return buf_index; //no frame
    
    h->prev_frame_num_offset= h->frame_num_offset;
    h->prev_frame_num= h->frame_num;
    if(s->current_picture_ptr->reference){
        h->prev_poc_msb= h->poc_msb;
        h->prev_poc_lsb= h->poc_lsb;
    }
    if(s->current_picture_ptr->reference)
        execute_ref_pic_marking(h, h->mmco, h->mmco_index);
    else
        assert(h->mmco_index==0);

    ff_er_frame_end(s);

    MPV_frame_end(s);

    return buf_index;
}

/**
 * retunrs the number of bytes consumed for building the current frame
 */
static int get_consumed_bytes(MpegEncContext *s, int pos, int buf_size){
    if(s->flags&CODEC_FLAG_TRUNCATED){
        pos -= s->parse_context.last_index;
        if(pos<0) pos=0; // FIXME remove (uneeded?)
        
        return pos;
    }else{
        if(pos==0) pos=1; //avoid infinite loops (i doubt thats needed but ...)
        if(pos+10>buf_size) pos=buf_size; // oops ;)

        return pos;
    }
}

static int decode_frame(AVCodecContext *avctx, 
                             void *data, int *data_size,
                             uint8_t *buf, int buf_size)
{
    H264Context *h = avctx->priv_data;
    MpegEncContext *s = &h->s;
    AVFrame *pict = data; 
    int buf_index;
    
    s->flags= avctx->flags;
    s->flags2= avctx->flags2;

   /* no supplementary picture */
    if (buf_size == 0) {
        return 0;
    }
    
    if(s->flags&CODEC_FLAG_TRUNCATED){
        int next= find_frame_end(h, buf, buf_size);
        
        if( ff_combine_frame(&s->parse_context, next, &buf, &buf_size) < 0 )
            return buf_size;
//printf("next:%d buf_size:%d last_index:%d\n", next, buf_size, s->parse_context.last_index);
    }

    if(h->is_avc && !h->got_avcC) {
        int i, cnt, nalsize;
        unsigned char *p = avctx->extradata;
        if(avctx->extradata_size < 7) {
            av_log(avctx, AV_LOG_ERROR, "avcC too short\n");
            return -1;
        }
        if(*p != 1) {
            av_log(avctx, AV_LOG_ERROR, "Unknown avcC version %d\n", *p);
            return -1;
        }
        /* sps and pps in the avcC always have length coded with 2 bytes,
           so put a fake nal_length_size = 2 while parsing them */
        h->nal_length_size = 2;
        // Decode sps from avcC
        cnt = *(p+5) & 0x1f; // Number of sps
        p += 6;
        for (i = 0; i < cnt; i++) {
            nalsize = BE_16(p) + 2;
            if(decode_nal_units(h, p, nalsize) != nalsize) {
                av_log(avctx, AV_LOG_ERROR, "Decoding sps %d from avcC failed\n", i);
                return -1;
            }
            p += nalsize;
        }        
        // Decode pps from avcC
        cnt = *(p++); // Number of pps
        for (i = 0; i < cnt; i++) {
            nalsize = BE_16(p) + 2;
            if(decode_nal_units(h, p, nalsize)  != nalsize) {
                av_log(avctx, AV_LOG_ERROR, "Decoding pps %d from avcC failed\n", i);
                return -1;
            }
            p += nalsize;
        }        
        // Now store right nal length size, that will be use to parse all other nals
        h->nal_length_size = ((*(((char*)(avctx->extradata))+4))&0x03)+1;
        // Do not reparse avcC
        h->got_avcC = 1;
    }

    if(!h->is_avc && s->avctx->extradata_size && s->picture_number==0){
        if(0 < decode_nal_units(h, s->avctx->extradata, s->avctx->extradata_size) ) 
            return -1;
    }

    buf_index=decode_nal_units(h, buf, buf_size);
    if(buf_index < 0) 
        return -1;

    //FIXME do something with unavailable reference frames    
 
//    if(ret==FRAME_SKIPED) return get_consumed_bytes(s, buf_index, buf_size);
    if(!s->current_picture_ptr){
        av_log(h->s.avctx, AV_LOG_DEBUG, "error, NO frame\n");
        return -1;
    }

    {
        /* Sort B-frames into display order
         * FIXME doesn't allow for multiple delayed frames */
        Picture *cur = s->current_picture_ptr;
        Picture *prev = h->delayed_pic[0];
        Picture *out;

        if(s->low_delay
           && (cur->pict_type == B_TYPE
           || (!h->sps.gaps_in_frame_num_allowed_flag
               && prev && cur->poc - prev->poc > 2))){
            s->low_delay = 0;
            s->avctx->has_b_frames = 1;
            if(prev && prev->poc > cur->poc)
                // too late to display this frame
                cur = prev;
        }

        if(s->low_delay || !prev || cur->pict_type == B_TYPE)
            out = cur;
        else
            out = prev;
        if(s->low_delay || !prev || out == prev){
            if(prev && prev->reference == 1)
                prev->reference = 0;
            h->delayed_pic[0] = cur;
        }

        *pict= *(AVFrame*)out;
    }

    ff_print_debug_info(s, pict);
    assert(pict->data[0]);
//printf("out %d\n", (int)pict->data[0]);
#if 0 //?

    /* Return the Picture timestamp as the frame number */
    /* we substract 1 because it is added on utils.c    */
    avctx->frame_number = s->picture_number - 1;
#endif
#if 0
    /* dont output the last pic after seeking */
    if(s->last_picture_ptr || s->low_delay)
    //Note this isnt a issue as a IDR pic should flush the buffers
#endif
        *data_size = sizeof(AVFrame);
    return get_consumed_bytes(s, buf_index, buf_size);
}
#if 0
static inline void fill_mb_avail(H264Context *h){
    MpegEncContext * const s = &h->s;
    const int mb_xy= s->mb_x + s->mb_y*s->mb_stride;

    if(s->mb_y){
        h->mb_avail[0]= s->mb_x                 && h->slice_table[mb_xy - s->mb_stride - 1] == h->slice_num;
        h->mb_avail[1]=                            h->slice_table[mb_xy - s->mb_stride    ] == h->slice_num;
        h->mb_avail[2]= s->mb_x+1 < s->mb_width && h->slice_table[mb_xy - s->mb_stride + 1] == h->slice_num;
    }else{
        h->mb_avail[0]=
        h->mb_avail[1]=
        h->mb_avail[2]= 0;
    }
    h->mb_avail[3]= s->mb_x && h->slice_table[mb_xy - 1] == h->slice_num;
    h->mb_avail[4]= 1; //FIXME move out
    h->mb_avail[5]= 0; //FIXME move out
}
#endif

#if 0 //selftest
#define COUNT 8000
#define SIZE (COUNT*40)
int main(){
    int i;
    uint8_t temp[SIZE];
    PutBitContext pb;
    GetBitContext gb;
//    int int_temp[10000];
    DSPContext dsp;
    AVCodecContext avctx;
    
    dsputil_init(&dsp, &avctx);

    init_put_bits(&pb, temp, SIZE);
    printf("testing unsigned exp golomb\n");
    for(i=0; i<COUNT; i++){
        START_TIMER
        set_ue_golomb(&pb, i);
        STOP_TIMER("set_ue_golomb");
    }
    flush_put_bits(&pb);
    
    init_get_bits(&gb, temp, 8*SIZE);
    for(i=0; i<COUNT; i++){
        int j, s;
        
        s= show_bits(&gb, 24);
        
        START_TIMER
        j= get_ue_golomb(&gb);
        if(j != i){
            printf("missmatch! at %d (%d should be %d) bits:%6X\n", i, j, i, s);
//            return -1;
        }
        STOP_TIMER("get_ue_golomb");
    }
    
    
    init_put_bits(&pb, temp, SIZE);
    printf("testing signed exp golomb\n");
    for(i=0; i<COUNT; i++){
        START_TIMER
        set_se_golomb(&pb, i - COUNT/2);
        STOP_TIMER("set_se_golomb");
    }
    flush_put_bits(&pb);
    
    init_get_bits(&gb, temp, 8*SIZE);
    for(i=0; i<COUNT; i++){
        int j, s;
        
        s= show_bits(&gb, 24);
        
        START_TIMER
        j= get_se_golomb(&gb);
        if(j != i - COUNT/2){
            printf("missmatch! at %d (%d should be %d) bits:%6X\n", i, j, i, s);
//            return -1;
        }
        STOP_TIMER("get_se_golomb");
    }

    printf("testing 4x4 (I)DCT\n");
    
    DCTELEM block[16];
    uint8_t src[16], ref[16];
    uint64_t error= 0, max_error=0;

    for(i=0; i<COUNT; i++){
        int j;
//        printf("%d %d %d\n", r1, r2, (r2-r1)*16);
        for(j=0; j<16; j++){
            ref[j]= random()%255;
            src[j]= random()%255;
        }

        h264_diff_dct_c(block, src, ref, 4);
        
        //normalize
        for(j=0; j<16; j++){
//            printf("%d ", block[j]);
            block[j]= block[j]*4;
            if(j&1) block[j]= (block[j]*4 + 2)/5;
            if(j&4) block[j]= (block[j]*4 + 2)/5;
        }
//        printf("\n");
        
        s->dsp.h264_idct_add(ref, block, 4);
/*        for(j=0; j<16; j++){
            printf("%d ", ref[j]);
        }
        printf("\n");*/
            
        for(j=0; j<16; j++){
            int diff= ABS(src[j] - ref[j]);
            
            error+= diff*diff;
            max_error= FFMAX(max_error, diff);
        }
    }
    printf("error=%f max_error=%d\n", ((float)error)/COUNT/16, (int)max_error );
#if 0
    printf("testing quantizer\n");
    for(qp=0; qp<52; qp++){
        for(i=0; i<16; i++)
            src1_block[i]= src2_block[i]= random()%255;
        
    }
#endif
    printf("Testing NAL layer\n");
    
    uint8_t bitstream[COUNT];
    uint8_t nal[COUNT*2];
    H264Context h;
    memset(&h, 0, sizeof(H264Context));
    
    for(i=0; i<COUNT; i++){
        int zeros= i;
        int nal_length;
        int consumed;
        int out_length;
        uint8_t *out;
        int j;
        
        for(j=0; j<COUNT; j++){
            bitstream[j]= (random() % 255) + 1;
        }
        
        for(j=0; j<zeros; j++){
            int pos= random() % COUNT;
            while(bitstream[pos] == 0){
                pos++;
                pos %= COUNT;
            }
            bitstream[pos]=0;
        }
        
        START_TIMER
        
        nal_length= encode_nal(&h, nal, bitstream, COUNT, COUNT*2);
        if(nal_length<0){
            printf("encoding failed\n");
            return -1;
        }
        
        out= decode_nal(&h, nal, &out_length, &consumed, nal_length);

        STOP_TIMER("NAL")
        
        if(out_length != COUNT){
            printf("incorrect length %d %d\n", out_length, COUNT);
            return -1;
        }
        
        if(consumed != nal_length){
            printf("incorrect consumed length %d %d\n", nal_length, consumed);
            return -1;
        }
        
        if(memcmp(bitstream, out, COUNT)){
            printf("missmatch\n");
            return -1;
        }
    }
    
    printf("Testing RBSP\n");
    
    
    return 0;
}
#endif


static int decode_end(AVCodecContext *avctx)
{
    H264Context *h = avctx->priv_data;
    MpegEncContext *s = &h->s;
    
    free_tables(h); //FIXME cleanup init stuff perhaps
    MPV_common_end(s);

//    memset(h, 0, sizeof(H264Context));
        
    return 0;
}


AVCodec h264_decoder = {
    "h264",
    CODEC_TYPE_VIDEO,
    CODEC_ID_H264,
    sizeof(H264Context),
    decode_init,
    NULL,
    decode_end,
    decode_frame,
    /*CODEC_CAP_DRAW_HORIZ_BAND |*/ CODEC_CAP_DR1 | CODEC_CAP_TRUNCATED,
};

AVCodecParser h264_parser = {
    { CODEC_ID_H264 },
    sizeof(H264Context),
    NULL,
    h264_parse,
    ff_parse_close,
};

#include "svq3.c"
