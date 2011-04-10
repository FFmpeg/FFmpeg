
static inline void FUNC(mc_dir_part)(H264Context *h, Picture *pic, int n, int square, int chroma_height, int delta, int list,
                           uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                           int src_x_offset, int src_y_offset,
                           qpel_mc_func *qpix_op, h264_chroma_mc_func chroma_op){
    MpegEncContext * const s = &h->s;
    const int mx= h->mv_cache[list][ scan8[n] ][0] + src_x_offset*8;
    int my=       h->mv_cache[list][ scan8[n] ][1] + src_y_offset*8;
    const int luma_xy= (mx&3) + ((my&3)<<2);
    uint8_t * src_y = pic->data[0] + ((mx>>2)<<PIXEL_SHIFT) + (my>>2)*h->mb_linesize;
    uint8_t * src_cb, * src_cr;
    int extra_width= h->emu_edge_width;
    int extra_height= h->emu_edge_height;
    int emu=0;
    const int full_mx= mx>>2;
    const int full_my= my>>2;
    const int pic_width  = 16*s->mb_width;
    const int pic_height = 16*s->mb_height >> MB_FIELD;

    if(mx&7) extra_width -= 3;
    if(my&7) extra_height -= 3;

    if(   full_mx < 0-extra_width
       || full_my < 0-extra_height
       || full_mx + 16/*FIXME*/ > pic_width + extra_width
       || full_my + 16/*FIXME*/ > pic_height + extra_height){
        s->dsp.emulated_edge_mc(s->edge_emu_buffer, src_y - (2<<PIXEL_SHIFT) - 2*h->mb_linesize, h->mb_linesize, 16+5, 16+5/*FIXME*/, full_mx-2, full_my-2, pic_width, pic_height);
            src_y= s->edge_emu_buffer + (2<<PIXEL_SHIFT) + 2*h->mb_linesize;
        emu=1;
    }

    qpix_op[luma_xy](dest_y, src_y, h->mb_linesize); //FIXME try variable height perhaps?
    if(!square){
        qpix_op[luma_xy](dest_y + delta, src_y + delta, h->mb_linesize);
    }

    if(CONFIG_GRAY && s->flags&CODEC_FLAG_GRAY) return;

    if(MB_FIELD){
        // chroma offset when predicting from a field of opposite parity
        my += 2 * ((s->mb_y & 1) - (pic->reference - 1));
        emu |= (my>>3) < 0 || (my>>3) + 8 >= (pic_height>>1);
    }
    src_cb= pic->data[1] + ((mx>>3)<<PIXEL_SHIFT) + (my>>3)*h->mb_uvlinesize;
    src_cr= pic->data[2] + ((mx>>3)<<PIXEL_SHIFT) + (my>>3)*h->mb_uvlinesize;

    if(emu){
        s->dsp.emulated_edge_mc(s->edge_emu_buffer, src_cb, h->mb_uvlinesize, 9, 9/*FIXME*/, (mx>>3), (my>>3), pic_width>>1, pic_height>>1);
            src_cb= s->edge_emu_buffer;
    }
    chroma_op(dest_cb, src_cb, h->mb_uvlinesize, chroma_height, mx&7, my&7);

    if(emu){
        s->dsp.emulated_edge_mc(s->edge_emu_buffer, src_cr, h->mb_uvlinesize, 9, 9/*FIXME*/, (mx>>3), (my>>3), pic_width>>1, pic_height>>1);
            src_cr= s->edge_emu_buffer;
    }
    chroma_op(dest_cr, src_cr, h->mb_uvlinesize, chroma_height, mx&7, my&7);
}

static inline void FUNC(mc_part_std)(H264Context *h, int n, int square, int chroma_height, int delta,
                           uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                           int x_offset, int y_offset,
                           qpel_mc_func *qpix_put, h264_chroma_mc_func chroma_put,
                           qpel_mc_func *qpix_avg, h264_chroma_mc_func chroma_avg,
                           int list0, int list1){
    MpegEncContext * const s = &h->s;
    qpel_mc_func *qpix_op=  qpix_put;
    h264_chroma_mc_func chroma_op= chroma_put;

    dest_y  += (2*x_offset<<PIXEL_SHIFT) + 2*y_offset*h->  mb_linesize;
    dest_cb += (  x_offset<<PIXEL_SHIFT) +   y_offset*h->mb_uvlinesize;
    dest_cr += (  x_offset<<PIXEL_SHIFT) +   y_offset*h->mb_uvlinesize;
    x_offset += 8*s->mb_x;
    y_offset += 8*(s->mb_y >> MB_FIELD);

    if(list0){
        Picture *ref= &h->ref_list[0][ h->ref_cache[0][ scan8[n] ] ];
        FUNC(mc_dir_part)(h, ref, n, square, chroma_height, delta, 0,
                           dest_y, dest_cb, dest_cr, x_offset, y_offset,
                           qpix_op, chroma_op);

        qpix_op=  qpix_avg;
        chroma_op= chroma_avg;
    }

    if(list1){
        Picture *ref= &h->ref_list[1][ h->ref_cache[1][ scan8[n] ] ];
        FUNC(mc_dir_part)(h, ref, n, square, chroma_height, delta, 1,
                           dest_y, dest_cb, dest_cr, x_offset, y_offset,
                           qpix_op, chroma_op);
    }
}

static inline void FUNC(mc_part_weighted)(H264Context *h, int n, int square, int chroma_height, int delta,
                           uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                           int x_offset, int y_offset,
                           qpel_mc_func *qpix_put, h264_chroma_mc_func chroma_put,
                           h264_weight_func luma_weight_op, h264_weight_func chroma_weight_op,
                           h264_biweight_func luma_weight_avg, h264_biweight_func chroma_weight_avg,
                           int list0, int list1){
    MpegEncContext * const s = &h->s;

    dest_y  += (2*x_offset<<PIXEL_SHIFT) + 2*y_offset*h->  mb_linesize;
    dest_cb += (  x_offset<<PIXEL_SHIFT) +   y_offset*h->mb_uvlinesize;
    dest_cr += (  x_offset<<PIXEL_SHIFT) +   y_offset*h->mb_uvlinesize;
    x_offset += 8*s->mb_x;
    y_offset += 8*(s->mb_y >> MB_FIELD);

    if(list0 && list1){
        /* don't optimize for luma-only case, since B-frames usually
         * use implicit weights => chroma too. */
        uint8_t *tmp_cb = s->obmc_scratchpad;
        uint8_t *tmp_cr = s->obmc_scratchpad + (8<<PIXEL_SHIFT);
        uint8_t *tmp_y  = s->obmc_scratchpad + 8*h->mb_uvlinesize;
        int refn0 = h->ref_cache[0][ scan8[n] ];
        int refn1 = h->ref_cache[1][ scan8[n] ];

        FUNC(mc_dir_part)(h, &h->ref_list[0][refn0], n, square, chroma_height, delta, 0,
                    dest_y, dest_cb, dest_cr,
                    x_offset, y_offset, qpix_put, chroma_put);
        FUNC(mc_dir_part)(h, &h->ref_list[1][refn1], n, square, chroma_height, delta, 1,
                    tmp_y, tmp_cb, tmp_cr,
                    x_offset, y_offset, qpix_put, chroma_put);

        if(h->use_weight == 2){
            int weight0 = h->implicit_weight[refn0][refn1][s->mb_y&1];
            int weight1 = 64 - weight0;
            luma_weight_avg(  dest_y,  tmp_y,  h->  mb_linesize, 5, weight0, weight1, 0);
            chroma_weight_avg(dest_cb, tmp_cb, h->mb_uvlinesize, 5, weight0, weight1, 0);
            chroma_weight_avg(dest_cr, tmp_cr, h->mb_uvlinesize, 5, weight0, weight1, 0);
        }else{
            luma_weight_avg(dest_y, tmp_y, h->mb_linesize, h->luma_log2_weight_denom,
                            h->luma_weight[refn0][0][0] , h->luma_weight[refn1][1][0],
                            h->luma_weight[refn0][0][1] + h->luma_weight[refn1][1][1]);
            chroma_weight_avg(dest_cb, tmp_cb, h->mb_uvlinesize, h->chroma_log2_weight_denom,
                            h->chroma_weight[refn0][0][0][0] , h->chroma_weight[refn1][1][0][0],
                            h->chroma_weight[refn0][0][0][1] + h->chroma_weight[refn1][1][0][1]);
            chroma_weight_avg(dest_cr, tmp_cr, h->mb_uvlinesize, h->chroma_log2_weight_denom,
                            h->chroma_weight[refn0][0][1][0] , h->chroma_weight[refn1][1][1][0],
                            h->chroma_weight[refn0][0][1][1] + h->chroma_weight[refn1][1][1][1]);
        }
    }else{
        int list = list1 ? 1 : 0;
        int refn = h->ref_cache[list][ scan8[n] ];
        Picture *ref= &h->ref_list[list][refn];
        FUNC(mc_dir_part)(h, ref, n, square, chroma_height, delta, list,
                    dest_y, dest_cb, dest_cr, x_offset, y_offset,
                    qpix_put, chroma_put);

        luma_weight_op(dest_y, h->mb_linesize, h->luma_log2_weight_denom,
                       h->luma_weight[refn][list][0], h->luma_weight[refn][list][1]);
        if(h->use_weight_chroma){
            chroma_weight_op(dest_cb, h->mb_uvlinesize, h->chroma_log2_weight_denom,
                             h->chroma_weight[refn][list][0][0], h->chroma_weight[refn][list][0][1]);
            chroma_weight_op(dest_cr, h->mb_uvlinesize, h->chroma_log2_weight_denom,
                             h->chroma_weight[refn][list][1][0], h->chroma_weight[refn][list][1][1]);
        }
    }
}

static inline void FUNC(mc_part)(H264Context *h, int n, int square, int chroma_height, int delta,
                           uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                           int x_offset, int y_offset,
                           qpel_mc_func *qpix_put, h264_chroma_mc_func chroma_put,
                           qpel_mc_func *qpix_avg, h264_chroma_mc_func chroma_avg,
                           h264_weight_func *weight_op, h264_biweight_func *weight_avg,
                           int list0, int list1){
    if((h->use_weight==2 && list0 && list1
        && (h->implicit_weight[ h->ref_cache[0][scan8[n]] ][ h->ref_cache[1][scan8[n]] ][h->s.mb_y&1] != 32))
       || h->use_weight==1)
        FUNC(mc_part_weighted)(h, n, square, chroma_height, delta, dest_y, dest_cb, dest_cr,
                         x_offset, y_offset, qpix_put, chroma_put,
                         weight_op[0], weight_op[3], weight_avg[0], weight_avg[3], list0, list1);
    else
        FUNC(mc_part_std)(h, n, square, chroma_height, delta, dest_y, dest_cb, dest_cr,
                    x_offset, y_offset, qpix_put, chroma_put, qpix_avg, chroma_avg, list0, list1);
}

static inline void FUNC(prefetch_motion)(H264Context *h, int list){
    /* fetch pixels for estimated mv 4 macroblocks ahead
     * optimized for 64byte cache lines */
    MpegEncContext * const s = &h->s;
    const int refn = h->ref_cache[list][scan8[0]];
    if(refn >= 0){
        const int mx= (h->mv_cache[list][scan8[0]][0]>>2) + 16*s->mb_x + 8;
        const int my= (h->mv_cache[list][scan8[0]][1]>>2) + 16*s->mb_y;
        uint8_t **src= h->ref_list[list][refn].data;
        int off= ((mx+64)<<PIXEL_SHIFT) + (my + (s->mb_x&3)*4)*h->mb_linesize;
        s->dsp.prefetch(src[0]+off, s->linesize, 4);
        off= (((mx>>1)+64)<<PIXEL_SHIFT) + ((my>>1) + (s->mb_x&7))*s->uvlinesize;
        s->dsp.prefetch(src[1]+off, src[2]-src[1], 2);
    }
}

static void FUNC(hl_motion)(H264Context *h, uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                      qpel_mc_func (*qpix_put)[16], h264_chroma_mc_func (*chroma_put),
                      qpel_mc_func (*qpix_avg)[16], h264_chroma_mc_func (*chroma_avg),
                      h264_weight_func *weight_op, h264_biweight_func *weight_avg){
    MpegEncContext * const s = &h->s;
    const int mb_xy= h->mb_xy;
    const int mb_type= s->current_picture.mb_type[mb_xy];

    assert(IS_INTER(mb_type));

    if(HAVE_PTHREADS && s->avctx->active_thread_type&FF_THREAD_FRAME)
        await_references(h);
    FUNC(prefetch_motion)(h, 0);

    if(IS_16X16(mb_type)){
        FUNC(mc_part)(h, 0, 1, 8, 0, dest_y, dest_cb, dest_cr, 0, 0,
                qpix_put[0], chroma_put[0], qpix_avg[0], chroma_avg[0],
                weight_op, weight_avg,
                IS_DIR(mb_type, 0, 0), IS_DIR(mb_type, 0, 1));
    }else if(IS_16X8(mb_type)){
        FUNC(mc_part)(h, 0, 0, 4, (8<<PIXEL_SHIFT), dest_y, dest_cb, dest_cr, 0, 0,
                qpix_put[1], chroma_put[0], qpix_avg[1], chroma_avg[0],
                &weight_op[1], &weight_avg[1],
                IS_DIR(mb_type, 0, 0), IS_DIR(mb_type, 0, 1));
        FUNC(mc_part)(h, 8, 0, 4, (8<<PIXEL_SHIFT), dest_y, dest_cb, dest_cr, 0, 4,
                qpix_put[1], chroma_put[0], qpix_avg[1], chroma_avg[0],
                &weight_op[1], &weight_avg[1],
                IS_DIR(mb_type, 1, 0), IS_DIR(mb_type, 1, 1));
    }else if(IS_8X16(mb_type)){
        FUNC(mc_part)(h, 0, 0, 8, 8*h->mb_linesize, dest_y, dest_cb, dest_cr, 0, 0,
                qpix_put[1], chroma_put[1], qpix_avg[1], chroma_avg[1],
                &weight_op[2], &weight_avg[2],
                IS_DIR(mb_type, 0, 0), IS_DIR(mb_type, 0, 1));
        FUNC(mc_part)(h, 4, 0, 8, 8*h->mb_linesize, dest_y, dest_cb, dest_cr, 4, 0,
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
                FUNC(mc_part)(h, n, 1, 4, 0, dest_y, dest_cb, dest_cr, x_offset, y_offset,
                    qpix_put[1], chroma_put[1], qpix_avg[1], chroma_avg[1],
                    &weight_op[3], &weight_avg[3],
                    IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
            }else if(IS_SUB_8X4(sub_mb_type)){
                FUNC(mc_part)(h, n  , 0, 2, (4<<PIXEL_SHIFT), dest_y, dest_cb, dest_cr, x_offset, y_offset,
                    qpix_put[2], chroma_put[1], qpix_avg[2], chroma_avg[1],
                    &weight_op[4], &weight_avg[4],
                    IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
                FUNC(mc_part)(h, n+2, 0, 2, (4<<PIXEL_SHIFT), dest_y, dest_cb, dest_cr, x_offset, y_offset+2,
                    qpix_put[2], chroma_put[1], qpix_avg[2], chroma_avg[1],
                    &weight_op[4], &weight_avg[4],
                    IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
            }else if(IS_SUB_4X8(sub_mb_type)){
                FUNC(mc_part)(h, n  , 0, 4, 4*h->mb_linesize, dest_y, dest_cb, dest_cr, x_offset, y_offset,
                    qpix_put[2], chroma_put[2], qpix_avg[2], chroma_avg[2],
                    &weight_op[5], &weight_avg[5],
                    IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
                FUNC(mc_part)(h, n+1, 0, 4, 4*h->mb_linesize, dest_y, dest_cb, dest_cr, x_offset+2, y_offset,
                    qpix_put[2], chroma_put[2], qpix_avg[2], chroma_avg[2],
                    &weight_op[5], &weight_avg[5],
                    IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
            }else{
                int j;
                assert(IS_SUB_4X4(sub_mb_type));
                for(j=0; j<4; j++){
                    int sub_x_offset= x_offset + 2*(j&1);
                    int sub_y_offset= y_offset +   (j&2);
                    FUNC(mc_part)(h, n+j, 1, 2, 0, dest_y, dest_cb, dest_cr, sub_x_offset, sub_y_offset,
                        qpix_put[2], chroma_put[2], qpix_avg[2], chroma_avg[2],
                        &weight_op[6], &weight_avg[6],
                        IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
                }
            }
        }
    }

    FUNC(prefetch_motion)(h, 1);
}
