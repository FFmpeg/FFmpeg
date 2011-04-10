
#include "h264.h"
#include "thread.h"

static inline int get_lowest_part_list_y(H264Context *h, Picture *pic, int n, int height,
                                 int y_offset, int list){
    int raw_my= h->mv_cache[list][ scan8[n] ][1];
    int filter_height= (raw_my&3) ? 2 : 0;
    int full_my= (raw_my>>2) + y_offset;
    int top = full_my - filter_height, bottom = full_my + height + filter_height;

    return FFMAX(abs(top), bottom);
}

static inline void get_lowest_part_y(H264Context *h, int refs[2][48], int n, int height,
                               int y_offset, int list0, int list1, int *nrefs){
    MpegEncContext * const s = &h->s;
    int my;

    y_offset += 16*(s->mb_y >> MB_FIELD);

    if(list0){
        int ref_n = h->ref_cache[0][ scan8[n] ];
        Picture *ref= &h->ref_list[0][ref_n];

        // Error resilience puts the current picture in the ref list.
        // Don't try to wait on these as it will cause a deadlock.
        // Fields can wait on each other, though.
        if(ref->thread_opaque != s->current_picture.thread_opaque ||
           (ref->reference&3) != s->picture_structure) {
            my = get_lowest_part_list_y(h, ref, n, height, y_offset, 0);
            if (refs[0][ref_n] < 0) nrefs[0] += 1;
            refs[0][ref_n] = FFMAX(refs[0][ref_n], my);
        }
    }

    if(list1){
        int ref_n = h->ref_cache[1][ scan8[n] ];
        Picture *ref= &h->ref_list[1][ref_n];

        if(ref->thread_opaque != s->current_picture.thread_opaque ||
           (ref->reference&3) != s->picture_structure) {
            my = get_lowest_part_list_y(h, ref, n, height, y_offset, 1);
            if (refs[1][ref_n] < 0) nrefs[1] += 1;
            refs[1][ref_n] = FFMAX(refs[1][ref_n], my);
        }
    }
}

/**
 * Wait until all reference frames are available for MC operations.
 *
 * @param h the H264 context
 */
static void await_references(H264Context *h){
    MpegEncContext * const s = &h->s;
    const int mb_xy= h->mb_xy;
    const int mb_type= s->current_picture.mb_type[mb_xy];
    int refs[2][48];
    int nrefs[2] = {0};
    int ref, list;

    memset(refs, -1, sizeof(refs));

    if(IS_16X16(mb_type)){
        get_lowest_part_y(h, refs, 0, 16, 0,
                  IS_DIR(mb_type, 0, 0), IS_DIR(mb_type, 0, 1), nrefs);
    }else if(IS_16X8(mb_type)){
        get_lowest_part_y(h, refs, 0, 8, 0,
                  IS_DIR(mb_type, 0, 0), IS_DIR(mb_type, 0, 1), nrefs);
        get_lowest_part_y(h, refs, 8, 8, 8,
                  IS_DIR(mb_type, 1, 0), IS_DIR(mb_type, 1, 1), nrefs);
    }else if(IS_8X16(mb_type)){
        get_lowest_part_y(h, refs, 0, 16, 0,
                  IS_DIR(mb_type, 0, 0), IS_DIR(mb_type, 0, 1), nrefs);
        get_lowest_part_y(h, refs, 4, 16, 0,
                  IS_DIR(mb_type, 1, 0), IS_DIR(mb_type, 1, 1), nrefs);
    }else{
        int i;

        assert(IS_8X8(mb_type));

        for(i=0; i<4; i++){
            const int sub_mb_type= h->sub_mb_type[i];
            const int n= 4*i;
            int y_offset= (i&2)<<2;

            if(IS_SUB_8X8(sub_mb_type)){
                get_lowest_part_y(h, refs, n  , 8, y_offset,
                          IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1), nrefs);
            }else if(IS_SUB_8X4(sub_mb_type)){
                get_lowest_part_y(h, refs, n  , 4, y_offset,
                          IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1), nrefs);
                get_lowest_part_y(h, refs, n+2, 4, y_offset+4,
                          IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1), nrefs);
            }else if(IS_SUB_4X8(sub_mb_type)){
                get_lowest_part_y(h, refs, n  , 8, y_offset,
                          IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1), nrefs);
                get_lowest_part_y(h, refs, n+1, 8, y_offset,
                          IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1), nrefs);
            }else{
                int j;
                assert(IS_SUB_4X4(sub_mb_type));
                for(j=0; j<4; j++){
                    int sub_y_offset= y_offset + 2*(j&2);
                    get_lowest_part_y(h, refs, n+j, 4, sub_y_offset,
                              IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1), nrefs);
                }
            }
        }
    }

    for(list=h->list_count-1; list>=0; list--){
        for(ref=0; ref<48 && nrefs[list]; ref++){
            int row = refs[list][ref];
            if(row >= 0){
                Picture *ref_pic = &h->ref_list[list][ref];
                int ref_field = ref_pic->reference - 1;
                int ref_field_picture = ref_pic->field_picture;
                int pic_height = 16*s->mb_height >> ref_field_picture;

                row <<= MB_MBAFF;
                nrefs[list]--;

                if(!FIELD_PICTURE && ref_field_picture){ // frame referencing two fields
                    ff_thread_await_progress((AVFrame*)ref_pic, FFMIN((row >> 1) - !(row&1), pic_height-1), 1);
                    ff_thread_await_progress((AVFrame*)ref_pic, FFMIN((row >> 1)           , pic_height-1), 0);
                }else if(FIELD_PICTURE && !ref_field_picture){ // field referencing one field of a frame
                    ff_thread_await_progress((AVFrame*)ref_pic, FFMIN(row*2 + ref_field    , pic_height-1), 0);
                }else if(FIELD_PICTURE){
                    ff_thread_await_progress((AVFrame*)ref_pic, FFMIN(row, pic_height-1), ref_field);
                }else{
                    ff_thread_await_progress((AVFrame*)ref_pic, FFMIN(row, pic_height-1), 0);
                }
            }
        }
    }
}

#define FUNC(a) a ## _8
#define PIXEL_SHIFT 0
#include "h264_hl_motion.h"

#undef PIXEL_SHIFT
#undef FUNC
#define FUNC(a) a ## _16
#define PIXEL_SHIFT 1
#include "h264_hl_motion.h"

void ff_hl_motion(H264Context *h, uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                      qpel_mc_func (*qpix_put)[16], h264_chroma_mc_func (*chroma_put),
                      qpel_mc_func (*qpix_avg)[16], h264_chroma_mc_func (*chroma_avg),
                      h264_weight_func *weight_op, h264_biweight_func *weight_avg){
    if(h->pixel_shift){
        hl_motion_16(h, dest_y, dest_cb, dest_cr,
                      qpix_put, chroma_put,
                      qpix_avg, chroma_avg,
                      weight_op, weight_avg);
    }else
        hl_motion_8(h, dest_y, dest_cb, dest_cr,
                      qpix_put, chroma_put,
                      qpix_avg, chroma_avg,
                      weight_op, weight_avg);
}
