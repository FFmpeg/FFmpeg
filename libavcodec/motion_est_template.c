/*
 * Motion estimation 
 * Copyright (c) 2002 Michael Niedermayer
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

//lets hope gcc will remove the unused vars ...(gcc 3.2.2 seems to do it ...)
//Note, the last line is there to kill these ugly unused var warnings
#define LOAD_COMMON(x, y)\
    uint32_t * const score_map= s->me.score_map;\
    const int stride= s->linesize;\
    const int uvstride= s->uvlinesize;\
    const int time_pp= s->pp_time;\
    const int time_pb= s->pb_time;\
    uint8_t * const src_y= s->new_picture.data[0] + ((y) * stride) + (x);\
    uint8_t * const src_u= s->new_picture.data[1] + (((y)>>1) * uvstride) + ((x)>>1);\
    uint8_t * const src_v= s->new_picture.data[2] + (((y)>>1) * uvstride) + ((x)>>1);\
    uint8_t * const ref_y= ref_picture->data[0] + ((y) * stride) + (x);\
    uint8_t * const ref_u= ref_picture->data[1] + (((y)>>1) * uvstride) + ((x)>>1);\
    uint8_t * const ref_v= ref_picture->data[2] + (((y)>>1) * uvstride) + ((x)>>1);\
    uint8_t * const ref2_y= s->next_picture.data[0] + ((y) * stride) + (x);\
    op_pixels_func (*hpel_put)[4];\
    op_pixels_func (*hpel_avg)[4]= &s->dsp.avg_pixels_tab[size];\
    op_pixels_func (*chroma_hpel_put)[4];\
    qpel_mc_func (*qpel_put)[16];\
    qpel_mc_func (*qpel_avg)[16]= &s->dsp.avg_qpel_pixels_tab[size];\
    const __attribute__((unused)) int unu= time_pp + time_pb + (int)src_u + (int)src_v + (int)ref_u + (int)ref_v\
                                           + (int)ref2_y + (int)hpel_avg + (int)qpel_avg;\
    if(s->no_rounding /*FIXME b_type*/){\
        hpel_put= &s->dsp.put_no_rnd_pixels_tab[size];\
        chroma_hpel_put= &s->dsp.put_no_rnd_pixels_tab[size+1];\
        qpel_put= &s->dsp.put_no_rnd_qpel_pixels_tab[size];\
    }else{\
        hpel_put=& s->dsp.put_pixels_tab[size];\
        chroma_hpel_put= &s->dsp.put_pixels_tab[size+1];\
        qpel_put= &s->dsp.put_qpel_pixels_tab[size];\
    }


#ifdef CMP_HPEL
    
#define CHECK_HALF_MV(dx, dy, x, y)\
{\
    const int hx= 2*(x)+(dx);\
    const int hy= 2*(y)+(dy);\
    CMP_HPEL(d, dx, dy, x, y, size);\
    d += (mv_penalty[hx - pred_x] + mv_penalty[hy - pred_y])*penalty_factor;\
    COPY3_IF_LT(dmin, d, bx, hx, by, hy)\
}

#if 0
static int RENAME(hpel_motion_search)(MpegEncContext * s,
				  int *mx_ptr, int *my_ptr, int dmin,
				  int xmin, int ymin, int xmax, int ymax,
                                  int pred_x, int pred_y, Picture *ref_picture, 
                                  int n, int size)
{
    UINT8 *ptr;
    
    const int xx = 16 * s->mb_x + 8*(n&1);
    const int yy = 16 * s->mb_y + 8*(n>>1);
    const int mx = *mx_ptr;
    const int my = *my_ptr;
    
    LOAD_COMMON(xx, yy);
    
 //   INIT;
 //FIXME factorize
    me_cmp_func cmp, chroma_cmp, cmp_sub, chroma_cmp_sub;

    if(s->no_rounding /*FIXME b_type*/){
        hpel_put= &s->dsp.put_no_rnd_pixels_tab[size];
        chroma_hpel_put= &s->dsp.put_no_rnd_pixels_tab[size+1];
    }else{
        hpel_put=& s->dsp.put_pixels_tab[size];
        chroma_hpel_put= &s->dsp.put_pixels_tab[size+1];
    }
    cmp= s->dsp.me_cmp[size];
    chroma_cmp= s->dsp.me_cmp[size+1];
    cmp_sub= s->dsp.me_sub_cmp[size];
    chroma_cmp_sub= s->dsp.me_sub_cmp[size+1];

    if(s->me.skip){ //FIXME somehow move up (benchmark)
        *mx_ptr = 0;
        *my_ptr = 0;
        return dmin;
    }
        
    if(s->avctx->me_cmp != s->avctx->me_sub_cmp){
        CMP_HPEL(dmin, 0, 0, mx, my, size);
        if(mx || my)
            dmin += (mv_penalty[2*mx - pred_x] + mv_penalty[2*my - pred_y])*penalty_factor;
    }
        
    if (mx > xmin && mx < xmax && 
        my > ymin && my < ymax) {
        int bx=2*mx, by=2*my;
        int d= dmin;
        
        CHECK_HALF_MV(1, 1, mx-1, my-1)
        CHECK_HALF_MV(0, 1, mx  , my-1)        
        CHECK_HALF_MV(1, 1, mx  , my-1)
        CHECK_HALF_MV(1, 0, mx-1, my  )
        CHECK_HALF_MV(1, 0, mx  , my  )
        CHECK_HALF_MV(1, 1, mx-1, my  )
        CHECK_HALF_MV(0, 1, mx  , my  )        
        CHECK_HALF_MV(1, 1, mx  , my  )

        assert(bx < xmin*2 || bx > xmax*2 || by < ymin*2 || by > ymax*2);

        *mx_ptr = bx;
        *my_ptr = by;
    }else{
        *mx_ptr =2*mx;
        *my_ptr =2*my;
    }

    return dmin;
}

#else
static int RENAME(hpel_motion_search)(MpegEncContext * s,
				  int *mx_ptr, int *my_ptr, int dmin,
				  int xmin, int ymin, int xmax, int ymax,
                                  int pred_x, int pred_y, Picture *ref_picture, 
                                  int n, int size, uint16_t * const mv_penalty)
{
    const int xx = 16 * s->mb_x + 8*(n&1);
    const int yy = 16 * s->mb_y + 8*(n>>1);
    const int mx = *mx_ptr;
    const int my = *my_ptr;   
    const int penalty_factor= s->me.sub_penalty_factor;
    me_cmp_func cmp_sub, chroma_cmp_sub;

    LOAD_COMMON(xx, yy);
    
 //FIXME factorize

    cmp_sub= s->dsp.me_sub_cmp[size];
    chroma_cmp_sub= s->dsp.me_sub_cmp[size+1];

    if(s->me.skip){ //FIXME move out of hpel?
        *mx_ptr = 0;
        *my_ptr = 0;
        return dmin;
    }
        
    if(s->avctx->me_cmp != s->avctx->me_sub_cmp){
        CMP_HPEL(dmin, 0, 0, mx, my, size);
        if(mx || my)
            dmin += (mv_penalty[2*mx - pred_x] + mv_penalty[2*my - pred_y])*penalty_factor;
    }
        
    if (mx > xmin && mx < xmax && 
        my > ymin && my < ymax) {
        int bx=2*mx, by=2*my;
        int d= dmin;
        const int index= (my<<ME_MAP_SHIFT) + mx;
        const int t= score_map[(index-(1<<ME_MAP_SHIFT))&(ME_MAP_SIZE-1)] 
                     + (mv_penalty[bx   - pred_x] + mv_penalty[by-2 - pred_y])*penalty_factor;
        const int l= score_map[(index- 1               )&(ME_MAP_SIZE-1)]
                     + (mv_penalty[bx-2 - pred_x] + mv_penalty[by   - pred_y])*penalty_factor;
        const int r= score_map[(index+ 1               )&(ME_MAP_SIZE-1)]
                     + (mv_penalty[bx+2 - pred_x] + mv_penalty[by   - pred_y])*penalty_factor;
        const int b= score_map[(index+(1<<ME_MAP_SHIFT))&(ME_MAP_SIZE-1)]
                     + (mv_penalty[bx   - pred_x] + mv_penalty[by+2 - pred_y])*penalty_factor;
        
        if(t<=b){
            CHECK_HALF_MV(0, 1, mx  ,my-1)
            if(l<=r){
                CHECK_HALF_MV(1, 1, mx-1, my-1)
                if(t+r<=b+l){
                    CHECK_HALF_MV(1, 1, mx  , my-1)
                }else{
                    CHECK_HALF_MV(1, 1, mx-1, my  )
                }
                CHECK_HALF_MV(1, 0, mx-1, my  )
            }else{
                CHECK_HALF_MV(1, 1, mx  , my-1)
                if(t+l<=b+r){
                    CHECK_HALF_MV(1, 1, mx-1, my-1)
                }else{
                    CHECK_HALF_MV(1, 1, mx  , my  )
                }
                CHECK_HALF_MV(1, 0, mx  , my  )
            }
        }else{
            if(l<=r){
                if(t+l<=b+r){
                    CHECK_HALF_MV(1, 1, mx-1, my-1)
                }else{
                    CHECK_HALF_MV(1, 1, mx  , my  )
                }
                CHECK_HALF_MV(1, 0, mx-1, my)
                CHECK_HALF_MV(1, 1, mx-1, my)
            }else{
                if(t+r<=b+l){
                    CHECK_HALF_MV(1, 1, mx  , my-1)
                }else{
                    CHECK_HALF_MV(1, 1, mx-1, my)
                }
                CHECK_HALF_MV(1, 0, mx  , my)
                CHECK_HALF_MV(1, 1, mx  , my)
            }
            CHECK_HALF_MV(0, 1, mx  , my)
        }
        assert(bx >= xmin*2 && bx <= xmax*2 && by >= ymin*2 && by <= ymax*2);

        *mx_ptr = bx;
        *my_ptr = by;
    }else{
        *mx_ptr =2*mx;
        *my_ptr =2*my;
    }

    return dmin;
}
#endif

#endif /* CMP_HPEL */

#ifdef CMP_QPEL

#define CHECK_QUARTER_MV(dx, dy, x, y)\
{\
    const int hx= 4*(x)+(dx);\
    const int hy= 4*(y)+(dy);\
    CMP_QPEL(d, dx, dy, x, y, size);\
    d += (mv_penalty[hx - pred_x] + mv_penalty[hy - pred_y])*penalty_factor;\
    COPY3_IF_LT(dmin, d, bx, hx, by, hy)\
}

static int RENAME(qpel_motion_search)(MpegEncContext * s,
				  int *mx_ptr, int *my_ptr, int dmin,
				  int xmin, int ymin, int xmax, int ymax,
                                  int pred_x, int pred_y, Picture *ref_picture, 
                                  int n, int size, uint16_t * const mv_penalty)
{
    const int xx = 16 * s->mb_x + 8*(n&1);
    const int yy = 16 * s->mb_y + 8*(n>>1);
    const int mx = *mx_ptr;
    const int my = *my_ptr;   
    const int penalty_factor= s->me.sub_penalty_factor;
    const int map_generation= s->me.map_generation;
    uint32_t *map= s->me.map;
    me_cmp_func cmp, chroma_cmp;
    me_cmp_func cmp_sub, chroma_cmp_sub;

    LOAD_COMMON(xx, yy);
    
    cmp= s->dsp.me_cmp[size];
    chroma_cmp= s->dsp.me_cmp[size+1]; //factorize FIXME
 //FIXME factorize

    cmp_sub= s->dsp.me_sub_cmp[size];
    chroma_cmp_sub= s->dsp.me_sub_cmp[size+1];

    if(s->me.skip){ //FIXME somehow move up (benchmark)
        *mx_ptr = 0;
        *my_ptr = 0;
        return dmin;
    }
        
    if(s->avctx->me_cmp != s->avctx->me_sub_cmp){
        CMP_QPEL(dmin, 0, 0, mx, my, size);
        if(mx || my)
            dmin += (mv_penalty[4*mx - pred_x] + mv_penalty[4*my - pred_y])*penalty_factor;
    }
        
    if (mx > xmin && mx < xmax && 
        my > ymin && my < ymax) {
        int bx=4*mx, by=4*my;
        int d= dmin;
        int i, nx, ny;
        const int index= (my<<ME_MAP_SHIFT) + mx;
        const int t= score_map[(index-(1<<ME_MAP_SHIFT)  )&(ME_MAP_SIZE-1)];
        const int l= score_map[(index- 1                 )&(ME_MAP_SIZE-1)];
        const int r= score_map[(index+ 1                 )&(ME_MAP_SIZE-1)];
        const int b= score_map[(index+(1<<ME_MAP_SHIFT)  )&(ME_MAP_SIZE-1)];
        const int c= score_map[(index                    )&(ME_MAP_SIZE-1)];
        int best[8];
        int best_pos[8][2];
        
        memset(best, 64, sizeof(int)*8);
#if 1
        if(s->avctx->dia_size>=2){        
            const int tl= score_map[(index-(1<<ME_MAP_SHIFT)-1)&(ME_MAP_SIZE-1)];
            const int bl= score_map[(index+(1<<ME_MAP_SHIFT)-1)&(ME_MAP_SIZE-1)];
            const int tr= score_map[(index-(1<<ME_MAP_SHIFT)+1)&(ME_MAP_SIZE-1)];
            const int br= score_map[(index+(1<<ME_MAP_SHIFT)+1)&(ME_MAP_SIZE-1)];

            for(ny= -3; ny <= 3; ny++){
                for(nx= -3; nx <= 3; nx++){
                    const int t2= nx*nx*(tr + tl - 2*t) + 4*nx*(tr-tl) + 32*t;
                    const int c2= nx*nx*( r +  l - 2*c) + 4*nx*( r- l) + 32*c;
                    const int b2= nx*nx*(br + bl - 2*b) + 4*nx*(br-bl) + 32*b;
                    int score= ny*ny*(b2 + t2 - 2*c2) + 4*ny*(b2 - t2) + 32*c2;
                    int i;
                    
                    if((nx&3)==0 && (ny&3)==0) continue;
                    
                    score += 1024*(mv_penalty[4*mx + nx - pred_x] + mv_penalty[4*my + ny - pred_y])*penalty_factor;
                    
//                    if(nx&1) score-=1024*s->me.penalty_factor;
//                    if(ny&1) score-=1024*s->me.penalty_factor;
                    
                    for(i=0; i<8; i++){
                        if(score < best[i]){
                            memmove(&best[i+1], &best[i], sizeof(int)*(7-i));
                            memmove(&best_pos[i+1][0], &best_pos[i][0], sizeof(int)*2*(7-i));
                            best[i]= score;
                            best_pos[i][0]= nx + 4*mx;
                            best_pos[i][1]= ny + 4*my;
                            break;
                        }
                    }
                }
            }
        }else{
            int tl;
            const int cx = 4*(r - l);
            const int cx2= r + l - 2*c; 
            const int cy = 4*(b - t);
            const int cy2= b + t - 2*c;
            int cxy;
              
            if(map[(index-(1<<ME_MAP_SHIFT)-1)&(ME_MAP_SIZE-1)] == (my<<ME_MAP_MV_BITS) + mx + map_generation && 0){ //FIXME
                tl= score_map[(index-(1<<ME_MAP_SHIFT)-1)&(ME_MAP_SIZE-1)];
            }else{
                CMP(tl, mx-1, my-1, size); //FIXME wrong if chroma me is different
            }
            
            cxy= 2*tl + (cx + cy)/4 - (cx2 + cy2) - 2*c; 
           
            assert(16*cx2 + 4*cx + 32*c == 32*r);
            assert(16*cx2 - 4*cx + 32*c == 32*l);
            assert(16*cy2 + 4*cy + 32*c == 32*b);
            assert(16*cy2 - 4*cy + 32*c == 32*t);
            assert(16*cxy + 16*cy2 + 16*cx2 - 4*cy - 4*cx + 32*c == 32*tl);
            
            for(ny= -3; ny <= 3; ny++){
                for(nx= -3; nx <= 3; nx++){
                    int score= ny*nx*cxy + nx*nx*cx2 + ny*ny*cy2 + nx*cx + ny*cy + 32*c; //FIXME factor
                    int i;
                    
                    if((nx&3)==0 && (ny&3)==0) continue;
                
                    score += 32*(mv_penalty[4*mx + nx - pred_x] + mv_penalty[4*my + ny - pred_y])*penalty_factor;
//                    if(nx&1) score-=32*s->me.penalty_factor;
  //                  if(ny&1) score-=32*s->me.penalty_factor;
                    
                    for(i=0; i<8; i++){
                        if(score < best[i]){
                            memmove(&best[i+1], &best[i], sizeof(int)*(7-i));
                            memmove(&best_pos[i+1][0], &best_pos[i][0], sizeof(int)*2*(7-i));
                            best[i]= score;
                            best_pos[i][0]= nx + 4*mx;
                            best_pos[i][1]= ny + 4*my;
                            break;
                        }
                    }
                }
            }            
        }
        for(i=0; i<8; i++){
            nx= best_pos[i][0];
            ny= best_pos[i][1];
            CHECK_QUARTER_MV(nx&3, ny&3, nx>>2, ny>>2)
        }
#if 0
            nx= FFMAX(4*mx - bx, bx - 4*mx);
            ny= FFMAX(4*my - by, by - 4*my);
            
            static int stats[4][4];
            stats[nx][ny]++;
            if(256*256*256*64 % (stats[0][0]+1) ==0){
                for(i=0; i<16; i++){
                    if((i&3)==0) printf("\n");
                    printf("%6d ", stats[0][i]);
                }
                printf("\n");
            }
#endif
#else

        CHECK_QUARTER_MV(2, 2, mx-1, my-1)
        CHECK_QUARTER_MV(0, 2, mx  , my-1)        
        CHECK_QUARTER_MV(2, 2, mx  , my-1)
        CHECK_QUARTER_MV(2, 0, mx  , my  )
        CHECK_QUARTER_MV(2, 2, mx  , my  )
        CHECK_QUARTER_MV(0, 2, mx  , my  )
        CHECK_QUARTER_MV(2, 2, mx-1, my  )
        CHECK_QUARTER_MV(2, 0, mx-1, my  )
        
        nx= bx;
        ny= by;
        
        for(i=0; i<8; i++){
            int ox[8]= {0, 1, 1, 1, 0,-1,-1,-1};
            int oy[8]= {1, 1, 0,-1,-1,-1, 0, 1};
            CHECK_QUARTER_MV((nx + ox[i])&3, (ny + oy[i])&3, (nx + ox[i])>>2, (ny + oy[i])>>2)
        }
#endif
#if 0
        //outer ring
        CHECK_QUARTER_MV(1, 3, mx-1, my-1)
        CHECK_QUARTER_MV(1, 2, mx-1, my-1)
        CHECK_QUARTER_MV(1, 1, mx-1, my-1)
        CHECK_QUARTER_MV(2, 1, mx-1, my-1)
        CHECK_QUARTER_MV(3, 1, mx-1, my-1)
        CHECK_QUARTER_MV(0, 1, mx  , my-1)
        CHECK_QUARTER_MV(1, 1, mx  , my-1)
        CHECK_QUARTER_MV(2, 1, mx  , my-1)
        CHECK_QUARTER_MV(3, 1, mx  , my-1)
        CHECK_QUARTER_MV(3, 2, mx  , my-1)
        CHECK_QUARTER_MV(3, 3, mx  , my-1)
        CHECK_QUARTER_MV(3, 0, mx  , my  )
        CHECK_QUARTER_MV(3, 1, mx  , my  )
        CHECK_QUARTER_MV(3, 2, mx  , my  )
        CHECK_QUARTER_MV(3, 3, mx  , my  )
        CHECK_QUARTER_MV(2, 3, mx  , my  )
        CHECK_QUARTER_MV(1, 3, mx  , my  )
        CHECK_QUARTER_MV(0, 3, mx  , my  )
        CHECK_QUARTER_MV(3, 3, mx-1, my  )
        CHECK_QUARTER_MV(2, 3, mx-1, my  )
        CHECK_QUARTER_MV(1, 3, mx-1, my  )
        CHECK_QUARTER_MV(1, 2, mx-1, my  )
        CHECK_QUARTER_MV(1, 1, mx-1, my  )
        CHECK_QUARTER_MV(1, 0, mx-1, my  )
#endif
        assert(bx >= xmin*4 && bx <= xmax*4 && by >= ymin*4 && by <= ymax*4);

        *mx_ptr = bx;
        *my_ptr = by;
    }else{
        *mx_ptr =4*mx;
        *my_ptr =4*my;
    }

    return dmin;
}

#endif /* CMP_QPEL */

#define CHECK_MV(x,y)\
{\
    const int key= ((y)<<ME_MAP_MV_BITS) + (x) + map_generation;\
    const int index= (((y)<<ME_MAP_SHIFT) + (x))&(ME_MAP_SIZE-1);\
    if(map[index]!=key){\
        CMP(d, x, y, size);\
        map[index]= key;\
        score_map[index]= d;\
        d += (mv_penalty[((x)<<shift)-pred_x] + mv_penalty[((y)<<shift)-pred_y])*penalty_factor;\
        COPY3_IF_LT(dmin, d, best[0], x, best[1], y)\
    }\
}

#define CHECK_MV_DIR(x,y,new_dir)\
{\
    const int key= ((y)<<ME_MAP_MV_BITS) + (x) + map_generation;\
    const int index= (((y)<<ME_MAP_SHIFT) + (x))&(ME_MAP_SIZE-1);\
    if(map[index]!=key){\
        CMP(d, x, y, size);\
        map[index]= key;\
        score_map[index]= d;\
        d += (mv_penalty[((x)<<shift)-pred_x] + mv_penalty[((y)<<shift)-pred_y])*penalty_factor;\
        if(d<dmin){\
            best[0]=x;\
            best[1]=y;\
            dmin=d;\
            next_dir= new_dir;\
        }\
    }\
}

#define check(x,y,S,v)\
if( (x)<(xmin<<(S)) ) printf("%d %d %d %d %d xmin" #v, xmin, (x), (y), s->mb_x, s->mb_y);\
if( (x)>(xmax<<(S)) ) printf("%d %d %d %d %d xmax" #v, xmax, (x), (y), s->mb_x, s->mb_y);\
if( (y)<(ymin<<(S)) ) printf("%d %d %d %d %d ymin" #v, ymin, (x), (y), s->mb_x, s->mb_y);\
if( (y)>(ymax<<(S)) ) printf("%d %d %d %d %d ymax" #v, ymax, (x), (y), s->mb_x, s->mb_y);\


static inline int RENAME(small_diamond_search)(MpegEncContext * s, int *best, int dmin,
                                       Picture *ref_picture,
                                       int const pred_x, int const pred_y, int const penalty_factor,
                                       int const xmin, int const ymin, int const xmax, int const ymax, int const shift,
                                       uint32_t *map, int map_generation, int size, uint16_t * const mv_penalty
                                       )
{
    me_cmp_func cmp, chroma_cmp;
    int next_dir=-1;
    LOAD_COMMON(s->mb_x*16, s->mb_y*16);
    
    cmp= s->dsp.me_cmp[size];
    chroma_cmp= s->dsp.me_cmp[size+1];

    for(;;){
        int d;
        const int dir= next_dir;
        const int x= best[0];
        const int y= best[1];
        next_dir=-1;

//printf("%d", dir);
        if(dir!=2 && x>xmin) CHECK_MV_DIR(x-1, y  , 0)
        if(dir!=3 && y>ymin) CHECK_MV_DIR(x  , y-1, 1)
        if(dir!=0 && x<xmax) CHECK_MV_DIR(x+1, y  , 2)
        if(dir!=1 && y<ymax) CHECK_MV_DIR(x  , y+1, 3)

        if(next_dir==-1){
            return dmin;
        }
    }
}

static inline int RENAME(var_diamond_search)(MpegEncContext * s, int *best, int dmin,
                                       Picture *ref_picture,
                                       int const pred_x, int const pred_y, int const penalty_factor,
                                       int const xmin, int const ymin, int const xmax, int const ymax, int const shift,
                                       uint32_t *map, int map_generation, int size, uint16_t * const mv_penalty
                                       )
{
    me_cmp_func cmp, chroma_cmp;
    int dia_size=1;
    LOAD_COMMON(s->mb_x*16, s->mb_y*16);
    
    cmp= s->dsp.me_cmp[size];
    chroma_cmp= s->dsp.me_cmp[size+1];

    for(dia_size=1; dia_size<=s->avctx->dia_size; dia_size++){
        int dir, start, end;
        const int x= best[0];
        const int y= best[1];

        start= FFMAX(0, y + dia_size - ymax);
        end  = FFMIN(dia_size, xmax - x);
        for(dir= start; dir<end; dir++){
            int d;

//check(x + dir,y + dia_size - dir,0, a0)
            CHECK_MV(x + dir           , y + dia_size - dir);
        }

        start= FFMAX(0, x + dia_size - xmax);
        end  = FFMIN(dia_size, y - ymin);
        for(dir= start; dir<end; dir++){
            int d;

//check(x + dia_size - dir, y - dir,0, a1)
            CHECK_MV(x + dia_size - dir, y - dir           );
        }

        start= FFMAX(0, -y + dia_size + ymin );
        end  = FFMIN(dia_size, x - xmin);
        for(dir= start; dir<end; dir++){
            int d;

//check(x - dir,y - dia_size + dir,0, a2)
            CHECK_MV(x - dir           , y - dia_size + dir);
        }

        start= FFMAX(0, -x + dia_size + xmin );
        end  = FFMIN(dia_size, ymax - y);
        for(dir= start; dir<end; dir++){
            int d;

//check(x - dia_size + dir, y + dir,0, a3)
            CHECK_MV(x - dia_size + dir, y + dir           );
        }

        if(x!=best[0] || y!=best[1])
            dia_size=0;
    }
    return dmin;    
}

static int RENAME(epzs_motion_search)(MpegEncContext * s, int block,
                             int *mx_ptr, int *my_ptr,
                             int P[10][2], int pred_x, int pred_y,
                             int xmin, int ymin, int xmax, int ymax, Picture *ref_picture, uint16_t * const mv_penalty)
{
    int best[2]={0, 0};
    int d, dmin; 
    const int shift= 1+s->quarter_sample;
    uint32_t *map= s->me.map;
    int map_generation;
    const int penalty_factor= s->me.penalty_factor;
    const int size=0;
    me_cmp_func cmp, chroma_cmp;
    LOAD_COMMON(s->mb_x*16, s->mb_y*16);
    
    cmp= s->dsp.me_cmp[size];
    chroma_cmp= s->dsp.me_cmp[size+1];
    
    map_generation= update_map_generation(s);

    CMP(dmin, 0, 0, size);
    map[0]= map_generation;
    score_map[0]= dmin;

    /* first line */
    if ((s->mb_y == 0 || s->first_slice_line)) {
        CHECK_MV(P_LEFT[0]>>shift, P_LEFT[1]>>shift)
        CHECK_MV(P_LAST[0]>>shift, P_LAST[1]>>shift)
    }else{
        if(dmin<256 && ( P_LEFT[0]    |P_LEFT[1]
                        |P_TOP[0]     |P_TOP[1]
                        |P_TOPRIGHT[0]|P_TOPRIGHT[1])==0 && s->avctx->dia_size==0){
            *mx_ptr= 0;
            *my_ptr= 0;
            s->me.skip=1;
            return dmin;
        }
        CHECK_MV(P_MEDIAN[0]>>shift, P_MEDIAN[1]>>shift)
        if(dmin>256*2){
            CHECK_MV(P_LAST[0]    >>shift, P_LAST[1]    >>shift)
            CHECK_MV(P_LEFT[0]    >>shift, P_LEFT[1]    >>shift)
            CHECK_MV(P_TOP[0]     >>shift, P_TOP[1]     >>shift)
            CHECK_MV(P_TOPRIGHT[0]>>shift, P_TOPRIGHT[1]>>shift)
        }
    }
    if(dmin>256*4){
        CHECK_MV(P_LAST_RIGHT[0] >>shift, P_LAST_RIGHT[1] >>shift)
        CHECK_MV(P_LAST_BOTTOM[0]>>shift, P_LAST_BOTTOM[1]>>shift)
    }
#if 0 //doest only slow things down
    if(dmin>512*3){
        int step;
        dmin= score_map[0];
        best[0]= best[1]=0;
        for(step=128; step>0; step>>=1){
            const int step2= step;
            int y;
            for(y=-step2+best[1]; y<=step2+best[1]; y+=step){
                int x;
                if(y<ymin || y>ymax) continue;

                for(x=-step2+best[0]; x<=step2+best[0]; x+=step){
                    if(x<xmin || x>xmax) continue;
                    if(x==best[0] && y==best[1]) continue;
                    CHECK_MV(x,y)
                }
            }
        }
    }
#endif
//check(best[0],best[1],0, b0)
    if(s->avctx->dia_size<2)
        dmin= RENAME(small_diamond_search)(s, best, dmin, ref_picture,
                                   pred_x, pred_y, penalty_factor, xmin, ymin, xmax, ymax, 
				   shift, map, map_generation, size, mv_penalty);
    else
        dmin= RENAME(var_diamond_search)(s, best, dmin, ref_picture,
                                   pred_x, pred_y, penalty_factor, xmin, ymin, xmax, ymax, 
				   shift, map, map_generation, size, mv_penalty);

//check(best[0],best[1],0, b1)
    *mx_ptr= best[0];
    *my_ptr= best[1];    

//    printf("%d %d %d \n", best[0], best[1], dmin);
    return dmin;
}

#ifndef CMP_DIRECT /* no 4mv search needed in direct mode */
static int RENAME(epzs_motion_search4)(MpegEncContext * s, int block,
                             int *mx_ptr, int *my_ptr,
                             int P[10][2], int pred_x, int pred_y,
                             int xmin, int ymin, int xmax, int ymax, Picture *ref_picture, uint16_t * const mv_penalty)
{
    int best[2]={0, 0};
    int d, dmin; 
    const int shift= 1+s->quarter_sample;
    uint32_t *map= s->me.map;
    int map_generation;
    const int penalty_factor= s->me.penalty_factor;
    const int size=1;
    me_cmp_func cmp, chroma_cmp;
    LOAD_COMMON((s->mb_x*2 + (block&1))*8, (s->mb_y*2 + (block>>1))*8);
    
    cmp= s->dsp.me_cmp[size];
    chroma_cmp= s->dsp.me_cmp[size+1];

    map_generation= update_map_generation(s);

    dmin = 1000000;
//printf("%d %d %d %d //",xmin, ymin, xmax, ymax); 
    /* first line */
    if ((s->mb_y == 0 || s->first_slice_line) && block<2) {
	CHECK_MV(P_LEFT[0]>>shift, P_LEFT[1]>>shift)
        CHECK_MV(P_LAST[0]>>shift, P_LAST[1]>>shift)
        CHECK_MV(P_MV1[0]>>shift, P_MV1[1]>>shift)
    }else{
        CHECK_MV(P_MV1[0]>>shift, P_MV1[1]>>shift)
        //FIXME try some early stop
        if(dmin>64*2){
            CHECK_MV(P_MEDIAN[0]>>shift, P_MEDIAN[1]>>shift)
            CHECK_MV(P_LEFT[0]>>shift, P_LEFT[1]>>shift)
            CHECK_MV(P_TOP[0]>>shift, P_TOP[1]>>shift)
            CHECK_MV(P_TOPRIGHT[0]>>shift, P_TOPRIGHT[1]>>shift)
            CHECK_MV(P_LAST[0]>>shift, P_LAST[1]>>shift)
        }
    }
    if(dmin>64*4){
        CHECK_MV(P_LAST_RIGHT[0]>>shift, P_LAST_RIGHT[1]>>shift)
        CHECK_MV(P_LAST_BOTTOM[0]>>shift, P_LAST_BOTTOM[1]>>shift)
    }

    if(s->avctx->dia_size<2)
        dmin= RENAME(small_diamond_search)(s, best, dmin, ref_picture,
                                   pred_x, pred_y, penalty_factor, xmin, ymin, xmax, ymax, 
				   shift, map, map_generation, size, mv_penalty);
    else
        dmin= RENAME(var_diamond_search)(s, best, dmin, ref_picture,
                                   pred_x, pred_y, penalty_factor, xmin, ymin, xmax, ymax, 
				   shift, map, map_generation, size, mv_penalty);
    *mx_ptr= best[0];
    *my_ptr= best[1];    

//    printf("%d %d %d \n", best[0], best[1], dmin);
    return dmin;
}
#endif /* !CMP_DIRECT */
