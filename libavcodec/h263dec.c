/*
 * H.263 decoder
 * Copyright (c) 2001 Fabrice Bellard.
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
 */
 
/**
 * @file h263dec.c
 * H.263 decoder.
 */
 
#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"

//#define DEBUG
//#define PRINT_FRAME_TIME
#ifdef PRINT_FRAME_TIME
static inline long long rdtsc()
{
	long long l;
	asm volatile(	"rdtsc\n\t"
		: "=A" (l)
	);
//	printf("%d\n", int(l/1000));
	return l;
}
#endif

int ff_h263_decode_init(AVCodecContext *avctx)
{
    MpegEncContext *s = avctx->priv_data;

    s->avctx = avctx;
    s->out_format = FMT_H263;

    s->width = avctx->width;
    s->height = avctx->height;
    s->workaround_bugs= avctx->workaround_bugs;

    // set defaults
    s->quant_precision=5;
    s->progressive_sequence=1;
    s->decode_mb= ff_h263_decode_mb;
    s->low_delay= 1;
    avctx->pix_fmt= PIX_FMT_YUV420P;

    /* select sub codec */
    switch(avctx->codec->id) {
    case CODEC_ID_H263:
        s->gob_number = 0;
        break;
    case CODEC_ID_MPEG4:
        s->time_increment_bits = 4; /* default value for broken headers */
        s->h263_pred = 1;
        s->low_delay = 0; //default, might be overriden in the vol header during header parsing
        break;
    case CODEC_ID_MSMPEG4V1:
        s->h263_msmpeg4 = 1;
        s->h263_pred = 1;
        s->msmpeg4_version=1;
        break;
    case CODEC_ID_MSMPEG4V2:
        s->h263_msmpeg4 = 1;
        s->h263_pred = 1;
        s->msmpeg4_version=2;
        break;
    case CODEC_ID_MSMPEG4V3:
        s->h263_msmpeg4 = 1;
        s->h263_pred = 1;
        s->msmpeg4_version=3;
        break;
    case CODEC_ID_WMV1:
        s->h263_msmpeg4 = 1;
        s->h263_pred = 1;
        s->msmpeg4_version=4;
        break;
    case CODEC_ID_WMV2:
        s->h263_msmpeg4 = 1;
        s->h263_pred = 1;
        s->msmpeg4_version=5;
        break;
    case CODEC_ID_H263I:
        s->h263_intel = 1;
        break;
    default:
        return -1;
    }
    s->codec_id= avctx->codec->id;

    /* for h263, we allocate the images after having read the header */
    if (avctx->codec->id != CODEC_ID_H263 && avctx->codec->id != CODEC_ID_MPEG4)
        if (MPV_common_init(s) < 0)
            return -1;

    if (s->h263_msmpeg4)
        ff_msmpeg4_decode_init(s);
    else
        h263_decode_init_vlc(s);
    
    return 0;
}

int ff_h263_decode_end(AVCodecContext *avctx)
{
    MpegEncContext *s = avctx->priv_data;

    MPV_common_end(s);
    return 0;
}

/**
 * retunrs the number of bytes consumed for building the current frame
 */
static int get_consumed_bytes(MpegEncContext *s, int buf_size){
    int pos= (get_bits_count(&s->gb)+7)>>3;
    
    if(s->divx_packed){
        //we would have to scan through the whole buf to handle the weird reordering ...
        return buf_size; 
    }else if(s->flags&CODEC_FLAG_TRUNCATED){
        pos -= s->parse_context.last_index;
        if(pos<0) pos=0; // padding is not really read so this might be -1
        return pos;
    }else{
        if(pos==0) pos=1; //avoid infinite loops (i doubt thats needed but ...)
        if(pos+10>buf_size) pos=buf_size; // oops ;)

        return pos;
    }
}

static int decode_slice(MpegEncContext *s){
    const int part_mask= s->partitioned_frame ? (AC_END|AC_ERROR) : 0x7F;
    s->last_resync_gb= s->gb;
    s->first_slice_line= 1;
        
    s->resync_mb_x= s->mb_x;
    s->resync_mb_y= s->mb_y;

    s->y_dc_scale= s->y_dc_scale_table[ s->qscale ];
    s->c_dc_scale= s->c_dc_scale_table[ s->qscale ];
    
    if(s->partitioned_frame){
        const int qscale= s->qscale;

        if(s->codec_id==CODEC_ID_MPEG4){
            if(ff_mpeg4_decode_partitions(s) < 0)
                return -1; 
        }
        
        /* restore variables which where modified */
        s->first_slice_line=1;
        s->mb_x= s->resync_mb_x;
        s->mb_y= s->resync_mb_y;
        s->qscale= qscale;
        s->y_dc_scale= s->y_dc_scale_table[ s->qscale ];
        s->c_dc_scale= s->c_dc_scale_table[ s->qscale ];
    }

    for(; s->mb_y < s->mb_height; s->mb_y++) {
        /* per-row end of slice checks */
        if(s->msmpeg4_version){
            if(s->resync_mb_y + s->slice_height == s->mb_y){
                ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x-1, s->mb_y, AC_END|DC_END|MV_END);

                return 0;
            }
        }
        
        if(s->msmpeg4_version==1){
            s->last_dc[0]=
            s->last_dc[1]=
            s->last_dc[2]= 128;
        }
    
        ff_init_block_index(s);
        for(; s->mb_x < s->mb_width; s->mb_x++) {
            int ret;

            ff_update_block_index(s);

            if(s->resync_mb_x == s->mb_x && s->resync_mb_y+1 == s->mb_y){
                s->first_slice_line=0; 
            }

            /* DCT & quantize */
	    s->dsp.clear_blocks(s->block[0]);
            
            s->mv_dir = MV_DIR_FORWARD;
            s->mv_type = MV_TYPE_16X16;
//            s->mb_skiped = 0;
//printf("%d %d %06X\n", ret, get_bits_count(&s->gb), show_bits(&s->gb, 24));
            ret= s->decode_mb(s, s->block);
            
            MPV_decode_mb(s, s->block);

            if(ret<0){
                const int xy= s->mb_x + s->mb_y*s->mb_stride;
                if(ret==SLICE_END){
//printf("%d %d %d %06X\n", s->mb_x, s->mb_y, s->gb.size*8 - get_bits_count(&s->gb), show_bits(&s->gb, 24));
                    ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x, s->mb_y, (AC_END|DC_END|MV_END)&part_mask);

                    s->padding_bug_score--;
                        
                    if(++s->mb_x >= s->mb_width){
                        s->mb_x=0;
                        ff_draw_horiz_band(s, s->mb_y*16, 16);
                        s->mb_y++;
                    }
                    return 0; 
                }else if(ret==SLICE_NOEND){
                    fprintf(stderr,"Slice mismatch at MB: %d\n", xy);
                    ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x+1, s->mb_y, (AC_END|DC_END|MV_END)&part_mask);
                    return -1;
                }
                fprintf(stderr,"Error at MB: %d\n", xy);
                ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x, s->mb_y, (AC_ERROR|DC_ERROR|MV_ERROR)&part_mask);
    
                return -1;
            }
        }
        
        ff_draw_horiz_band(s, s->mb_y*16, 16);
        
        s->mb_x= 0;
    }
    
    assert(s->mb_x==0 && s->mb_y==s->mb_height);

    /* try to detect the padding bug */
    if(      s->codec_id==CODEC_ID_MPEG4
       &&   (s->workaround_bugs&FF_BUG_AUTODETECT) 
       &&    s->gb.size_in_bits - get_bits_count(&s->gb) >=0
       &&    s->gb.size_in_bits - get_bits_count(&s->gb) < 48
//       &&   !s->resync_marker
       &&   !s->data_partitioning){
        
        const int bits_count= get_bits_count(&s->gb);
        const int bits_left = s->gb.size_in_bits - bits_count;
        
        if(bits_left==0){
            s->padding_bug_score+=16;
        }else if(bits_left>8){
            s->padding_bug_score++;
        } else if(bits_left != 1){
            int v= show_bits(&s->gb, 8);
            v|= 0x7F >> (7-(bits_count&7));

            if(v==0x7F)
                s->padding_bug_score--;
            else
                s->padding_bug_score++;            
        }                          
    }

    // handle formats which dont have unique end markers
    if(s->msmpeg4_version || (s->workaround_bugs&FF_BUG_NO_PADDING)){ //FIXME perhaps solve this more cleanly
        int left= s->gb.size_in_bits - get_bits_count(&s->gb);
        int max_extra=7;
        
        /* no markers in M$ crap */
        if(s->msmpeg4_version && s->pict_type==I_TYPE)
            max_extra+= 17;
        
        /* buggy padding but the frame should still end approximately at the bitstream end */
        if((s->workaround_bugs&FF_BUG_NO_PADDING) && s->error_resilience>=3)
            max_extra+= 48;
        else if((s->workaround_bugs&FF_BUG_NO_PADDING))
            max_extra+= 256*256*256*64;
        
        if(left>max_extra){
            fprintf(stderr, "discarding %d junk bits at end, next would be %X\n", left, show_bits(&s->gb, 24));
        }
        else if(left<0){
            fprintf(stderr, "overreading %d bits\n", -left);
        }else
            ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x-1, s->mb_y, AC_END|DC_END|MV_END);
        
        return 0;
    }

    fprintf(stderr, "slice end not reached but screenspace end (%d left %06X)\n", 
            s->gb.size_in_bits - get_bits_count(&s->gb),
            show_bits(&s->gb, 24));
            
    ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x, s->mb_y, (AC_END|DC_END|MV_END)&part_mask);

    return -1;
}

/**
 * finds the end of the current frame in the bitstream.
 * @return the position of the first byte of the next frame, or -1
 */
static int mpeg4_find_frame_end(MpegEncContext *s, uint8_t *buf, int buf_size){
    ParseContext *pc= &s->parse_context;
    int vop_found, i;
    uint32_t state;
    
    vop_found= pc->frame_start_found;
    state= pc->state;
    
    i=0;
    if(!vop_found){
        for(i=0; i<buf_size; i++){
            state= (state<<8) | buf[i];
            if(state == 0x1B6){
                i++;
                vop_found=1;
                break;
            }
        }
    }

    if(vop_found){    
      for(; i<buf_size; i++){
        state= (state<<8) | buf[i];
        if((state&0xFFFFFF00) == 0x100){
            pc->frame_start_found=0;
            pc->state=-1; 
            return i-3;
        }
      }
    }
    pc->frame_start_found= vop_found;
    pc->state= state;
    return END_NOT_FOUND;
}

static int h263_find_frame_end(MpegEncContext *s, uint8_t *buf, int buf_size){
    ParseContext *pc= &s->parse_context;
    int vop_found, i;
    uint32_t state;
    
    vop_found= pc->frame_start_found;
    state= pc->state;
    
    i=0;
    if(!vop_found){
        for(i=0; i<buf_size; i++){
            state= (state<<8) | buf[i];
            if(state>>(32-22) == 0x20){
                i++;
                vop_found=1;
                break;
            }
        }
    }

    if(vop_found){    
      for(; i<buf_size; i++){
        state= (state<<8) | buf[i];
        if(state>>(32-22) == 0x20){
            pc->frame_start_found=0;
            pc->state=-1; 
            return i-3;
        }
      }
    }
    pc->frame_start_found= vop_found;
    pc->state= state;
    
    return END_NOT_FOUND;
}

int ff_h263_decode_frame(AVCodecContext *avctx, 
                             void *data, int *data_size,
                             uint8_t *buf, int buf_size)
{
    MpegEncContext *s = avctx->priv_data;
    int ret,i;
    AVFrame *pict = data; 
    float new_aspect;
    
#ifdef PRINT_FRAME_TIME
uint64_t time= rdtsc();
#endif
#ifdef DEBUG
    printf("*****frame %d size=%d\n", avctx->frame_number, buf_size);
    printf("bytes=%x %x %x %x\n", buf[0], buf[1], buf[2], buf[3]);
#endif
    s->flags= avctx->flags;

    *data_size = 0;
   
   /* no supplementary picture */
    if (buf_size == 0) {
        return 0;
    }

    if(s->flags&CODEC_FLAG_TRUNCATED){
        int next;
        
        if(s->codec_id==CODEC_ID_MPEG4){
            next= mpeg4_find_frame_end(s, buf, buf_size);
        }else if(s->codec_id==CODEC_ID_H263){
            next= h263_find_frame_end(s, buf, buf_size);
        }else{
            fprintf(stderr, "this codec doesnt support truncated bitstreams\n");
            return -1;
        }
        
        if( ff_combine_frame(s, next, &buf, &buf_size) < 0 )
            return buf_size;
    }

retry:
    
    if(s->bitstream_buffer_size && buf_size<20){ //divx 5.01+ frame reorder
        init_get_bits(&s->gb, s->bitstream_buffer, s->bitstream_buffer_size*8);
    }else
        init_get_bits(&s->gb, buf, buf_size*8);
    s->bitstream_buffer_size=0;

    if (!s->context_initialized) {
        if (MPV_common_init(s) < 0) //we need the idct permutaton for reading a custom matrix
            return -1;
    }
      
    /* let's go :-) */
    if (s->msmpeg4_version==5) {
        ret= ff_wmv2_decode_picture_header(s);
    } else if (s->msmpeg4_version) {
        ret = msmpeg4_decode_picture_header(s);
    } else if (s->h263_pred) {
        if(s->avctx->extradata_size && s->picture_number==0){
            GetBitContext gb;
            
            init_get_bits(&gb, s->avctx->extradata, s->avctx->extradata_size*8);
            ret = ff_mpeg4_decode_picture_header(s, &gb);
        }
        ret = ff_mpeg4_decode_picture_header(s, &s->gb);

        if(s->flags& CODEC_FLAG_LOW_DELAY)
            s->low_delay=1;
    } else if (s->h263_intel) {
        ret = intel_h263_decode_picture_header(s);
    } else {
        ret = h263_decode_picture_header(s);
    }
    avctx->has_b_frames= !s->low_delay;

    if(s->workaround_bugs&FF_BUG_AUTODETECT){
        if(s->padding_bug_score > -2 && !s->data_partitioning && !s->resync_marker)
            s->workaround_bugs |=  FF_BUG_NO_PADDING;
        else
            s->workaround_bugs &= ~FF_BUG_NO_PADDING;

        if(s->avctx->codec_tag == ff_get_fourcc("XVIX")) 
            s->workaround_bugs|= FF_BUG_XVID_ILACE;
#if 0
        if(s->avctx->codec_tag == ff_get_fourcc("MP4S")) 
            s->workaround_bugs|= FF_BUG_AC_VLC;
        
        if(s->avctx->codec_tag == ff_get_fourcc("M4S2")) 
            s->workaround_bugs|= FF_BUG_AC_VLC;
#endif
        if(s->avctx->codec_tag == ff_get_fourcc("UMP4")){
            s->workaround_bugs|= FF_BUG_UMP4;
            s->workaround_bugs|= FF_BUG_AC_VLC;
        }

        if(s->divx_version){
            s->workaround_bugs|= FF_BUG_QPEL_CHROMA;
        }

        if(s->divx_version>502){
            s->workaround_bugs|= FF_BUG_QPEL_CHROMA2;
        }

        if(s->avctx->codec_tag == ff_get_fourcc("XVID") && s->xvid_build==0)
            s->workaround_bugs|= FF_BUG_QPEL_CHROMA;
        
        if(s->avctx->codec_tag == ff_get_fourcc("XVID") && s->xvid_build==0)
            s->padding_bug_score= 256*256*256*64;
        
        if(s->xvid_build && s->xvid_build<=3)
            s->padding_bug_score= 256*256*256*64;
        
        if(s->xvid_build && s->xvid_build<=1)
            s->workaround_bugs|= FF_BUG_QPEL_CHROMA;

#define SET_QPEL_FUNC(postfix1, postfix2) \
    s->dsp.put_ ## postfix1 = ff_put_ ## postfix2;\
    s->dsp.put_no_rnd_ ## postfix1 = ff_put_no_rnd_ ## postfix2;\
    s->dsp.avg_ ## postfix1 = ff_avg_ ## postfix2;

        if(s->lavc_build && s->lavc_build<4653)
            s->workaround_bugs|= FF_BUG_STD_QPEL;
        
        if(s->lavc_build && s->lavc_build<4655)
            s->workaround_bugs|= FF_BUG_DIRECT_BLOCKSIZE;

        if(s->divx_version)
            s->workaround_bugs|= FF_BUG_DIRECT_BLOCKSIZE;
//printf("padding_bug_score: %d\n", s->padding_bug_score);
        if(s->divx_version==501 && s->divx_build==20020416)
            s->padding_bug_score= 256*256*256*64;

        if(s->divx_version>=500){
            s->workaround_bugs|= FF_BUG_EDGE;
        }

#if 0
        if(s->divx_version==500)
            s->padding_bug_score= 256*256*256*64;

        /* very ugly XVID padding bug detection FIXME/XXX solve this differently
         * lets hope this at least works
         */
        if(   s->resync_marker==0 && s->data_partitioning==0 && s->divx_version==0
           && s->codec_id==CODEC_ID_MPEG4 && s->vo_type==0)
            s->workaround_bugs|= FF_BUG_NO_PADDING;
        
        if(s->lavc_build && s->lavc_build<4609) //FIXME not sure about the version num but a 4609 file seems ok
            s->workaround_bugs|= FF_BUG_NO_PADDING;
#endif
    }
    
    if(s->workaround_bugs& FF_BUG_STD_QPEL){
        SET_QPEL_FUNC(qpel_pixels_tab[0][ 5], qpel16_mc11_old_c)
        SET_QPEL_FUNC(qpel_pixels_tab[0][ 7], qpel16_mc31_old_c)
        SET_QPEL_FUNC(qpel_pixels_tab[0][ 9], qpel16_mc12_old_c)
        SET_QPEL_FUNC(qpel_pixels_tab[0][11], qpel16_mc32_old_c)
        SET_QPEL_FUNC(qpel_pixels_tab[0][13], qpel16_mc13_old_c)
        SET_QPEL_FUNC(qpel_pixels_tab[0][15], qpel16_mc33_old_c)

        SET_QPEL_FUNC(qpel_pixels_tab[1][ 5], qpel8_mc11_old_c)
        SET_QPEL_FUNC(qpel_pixels_tab[1][ 7], qpel8_mc31_old_c)
        SET_QPEL_FUNC(qpel_pixels_tab[1][ 9], qpel8_mc12_old_c)
        SET_QPEL_FUNC(qpel_pixels_tab[1][11], qpel8_mc32_old_c)
        SET_QPEL_FUNC(qpel_pixels_tab[1][13], qpel8_mc13_old_c)
        SET_QPEL_FUNC(qpel_pixels_tab[1][15], qpel8_mc33_old_c)
    }

#if 0 // dump bits per frame / qp / complexity
{
    static FILE *f=NULL;
    if(!f) f=fopen("rate_qp_cplx.txt", "w");
    fprintf(f, "%d %d %f\n", buf_size, s->qscale, buf_size*(double)s->qscale);
}
#endif
       
        /* After H263 & mpeg4 header decode we have the height, width,*/
        /* and other parameters. So then we could init the picture   */
        /* FIXME: By the way H263 decoder is evolving it should have */
        /* an H263EncContext                                         */
    if(s->aspected_height)
        new_aspect= s->aspected_width*s->width / (float)(s->height*s->aspected_height);
    else
        new_aspect=0;
    
    if (   s->width != avctx->width || s->height != avctx->height 
        || ABS(new_aspect - avctx->aspect_ratio) > 0.001) {
        /* H.263 could change picture size any time */
        MPV_common_end(s);
    }
    if (!s->context_initialized) {
        avctx->width = s->width;
        avctx->height = s->height;
        avctx->aspect_ratio= new_aspect;

        goto retry;
    }

    if((s->codec_id==CODEC_ID_H263 || s->codec_id==CODEC_ID_H263P))
        s->gob_index = ff_h263_get_gob_height(s);

    if(ret==FRAME_SKIPED) return get_consumed_bytes(s, buf_size);
    /* skip if the header was thrashed */
    if (ret < 0){
        fprintf(stderr, "header damaged\n");
        return -1;
    }
    
    // for hurry_up==5
    s->current_picture.pict_type= s->pict_type;
    s->current_picture.key_frame= s->pict_type == I_TYPE;

    /* skip b frames if we dont have reference frames */
    if(s->last_picture_ptr==NULL && s->pict_type==B_TYPE) return get_consumed_bytes(s, buf_size);
    /* skip b frames if we are in a hurry */
    if(avctx->hurry_up && s->pict_type==B_TYPE) return get_consumed_bytes(s, buf_size);
    /* skip everything if we are in a hurry>=5 */
    if(avctx->hurry_up>=5) return get_consumed_bytes(s, buf_size);
    
    if(s->next_p_frame_damaged){
        if(s->pict_type==B_TYPE)
            return get_consumed_bytes(s, buf_size);
        else
            s->next_p_frame_damaged=0;
    }

    if(MPV_frame_start(s, avctx) < 0)
        return -1;

#ifdef DEBUG
    printf("qscale=%d\n", s->qscale);
#endif

    ff_er_frame_start(s);
    
    //the second part of the wmv2 header contains the MB skip bits which are stored in current_picture->mb_type
    //which isnt available before MPV_frame_start()
    if (s->msmpeg4_version==5){
        if(ff_wmv2_decode_secondary_picture_header(s) < 0)
            return -1;
    }

    /* decode each macroblock */
    s->mb_x=0; 
    s->mb_y=0;
    
    decode_slice(s);
    while(s->mb_y<s->mb_height){
        if(s->msmpeg4_version){
            if(s->mb_x!=0 || (s->mb_y%s->slice_height)!=0 || get_bits_count(&s->gb) > s->gb.size_in_bits)
                break;
        }else{
            if(ff_h263_resync(s)<0)
                break;
        }
        
        if(s->msmpeg4_version<4 && s->h263_pred)
            ff_mpeg4_clean_buffers(s);

        decode_slice(s);
    }

    if (s->h263_msmpeg4 && s->msmpeg4_version<4 && s->pict_type==I_TYPE)
        if(msmpeg4_decode_ext_header(s, buf_size) < 0){
            s->error_status_table[s->mb_num-1]= AC_ERROR|DC_ERROR|MV_ERROR;
        }
    
    /* divx 5.01+ bistream reorder stuff */
    if(s->codec_id==CODEC_ID_MPEG4 && s->bitstream_buffer_size==0 && s->divx_packed){
        int current_pos= get_bits_count(&s->gb)>>3;

        if(   buf_size - current_pos > 5 
           && buf_size - current_pos < BITSTREAM_BUFFER_SIZE){
            int i;
            int startcode_found=0;
            for(i=current_pos; i<buf_size-3; i++){
                if(buf[i]==0 && buf[i+1]==0 && buf[i+2]==1 && buf[i+3]==0xB6){
                    startcode_found=1;
                    break;
                }
            }
            if(startcode_found){
                memcpy(s->bitstream_buffer, buf + current_pos, buf_size - current_pos);
                s->bitstream_buffer_size= buf_size - current_pos;
            }
        }
    }

    ff_er_frame_end(s);

    MPV_frame_end(s);

assert(s->current_picture.pict_type == s->current_picture_ptr->pict_type);
assert(s->current_picture.pict_type == s->pict_type);
    if(s->pict_type==B_TYPE || s->low_delay){
        *pict= *(AVFrame*)&s->current_picture;
        ff_print_debug_info(s, s->current_picture_ptr);
    } else {
        *pict= *(AVFrame*)&s->last_picture;
        ff_print_debug_info(s, s->last_picture_ptr);
    }

    /* Return the Picture timestamp as the frame number */
    /* we substract 1 because it is added on utils.c    */
    avctx->frame_number = s->picture_number - 1;

    /* dont output the last pic after seeking */
    if(s->last_picture_ptr || s->low_delay)
        *data_size = sizeof(AVFrame);
#ifdef PRINT_FRAME_TIME
printf("%Ld\n", rdtsc()-time);
#endif

    return get_consumed_bytes(s, buf_size);
}

static const AVOption mpeg4_decoptions[] =
{
    AVOPTION_SUB(avoptions_workaround_bug),
    AVOPTION_END()
};

AVCodec mpeg4_decoder = {
    "mpeg4",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MPEG4,
    sizeof(MpegEncContext),
    ff_h263_decode_init,
    NULL,
    ff_h263_decode_end,
    ff_h263_decode_frame,
    CODEC_CAP_DRAW_HORIZ_BAND | CODEC_CAP_DR1 | CODEC_CAP_TRUNCATED,
    .options = mpeg4_decoptions,
};

AVCodec h263_decoder = {
    "h263",
    CODEC_TYPE_VIDEO,
    CODEC_ID_H263,
    sizeof(MpegEncContext),
    ff_h263_decode_init,
    NULL,
    ff_h263_decode_end,
    ff_h263_decode_frame,
    CODEC_CAP_DRAW_HORIZ_BAND | CODEC_CAP_DR1 | CODEC_CAP_TRUNCATED,
};

AVCodec msmpeg4v1_decoder = {
    "msmpeg4v1",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MSMPEG4V1,
    sizeof(MpegEncContext),
    ff_h263_decode_init,
    NULL,
    ff_h263_decode_end,
    ff_h263_decode_frame,
    CODEC_CAP_DRAW_HORIZ_BAND | CODEC_CAP_DR1,
    mpeg4_decoptions,
};

AVCodec msmpeg4v2_decoder = {
    "msmpeg4v2",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MSMPEG4V2,
    sizeof(MpegEncContext),
    ff_h263_decode_init,
    NULL,
    ff_h263_decode_end,
    ff_h263_decode_frame,
    CODEC_CAP_DRAW_HORIZ_BAND | CODEC_CAP_DR1,
    mpeg4_decoptions,
};

AVCodec msmpeg4v3_decoder = {
    "msmpeg4",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MSMPEG4V3,
    sizeof(MpegEncContext),
    ff_h263_decode_init,
    NULL,
    ff_h263_decode_end,
    ff_h263_decode_frame,
    CODEC_CAP_DRAW_HORIZ_BAND | CODEC_CAP_DR1,
    .options = mpeg4_decoptions,
};

AVCodec wmv1_decoder = {
    "wmv1",
    CODEC_TYPE_VIDEO,
    CODEC_ID_WMV1,
    sizeof(MpegEncContext),
    ff_h263_decode_init,
    NULL,
    ff_h263_decode_end,
    ff_h263_decode_frame,
    CODEC_CAP_DRAW_HORIZ_BAND | CODEC_CAP_DR1,
    mpeg4_decoptions,
};

AVCodec h263i_decoder = {
    "h263i",
    CODEC_TYPE_VIDEO,
    CODEC_ID_H263I,
    sizeof(MpegEncContext),
    ff_h263_decode_init,
    NULL,
    ff_h263_decode_end,
    ff_h263_decode_frame,
    CODEC_CAP_DRAW_HORIZ_BAND | CODEC_CAP_DR1,
    mpeg4_decoptions,
};

