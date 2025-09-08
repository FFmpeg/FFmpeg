/*
 * Copyright (C) 2004 Michael Niedermayer <michaelni@gmx.at>
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

#include "libavcodec/snowenc.c"

#undef malloc
#undef free
#undef printf

#include "libavutil/lfg.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem.h"

int main(void){
#define width  256
#define height 256
    int buffer[2][width*height];
    short obuffer[width*height];
    SnowContext s;
    int i;
    AVLFG prng;
    int ret = 0;
    s.spatial_decomposition_count=6;
    s.spatial_decomposition_type=1;

    s.temp_dwt_buffer  = av_calloc(width, sizeof(*s.temp_dwt_buffer));
    s.temp_idwt_buffer = av_calloc(width, sizeof(*s.temp_idwt_buffer));

    if (!s.temp_dwt_buffer || !s.temp_idwt_buffer) {
        fprintf(stderr, "Failed to allocate memory\n");
        ret = 1;
        goto end;
    }

    av_lfg_init(&prng, 1);

    printf("testing 5/3 DWT\n");
    for(i=0; i<width*height; i++)
        buffer[0][i] = buffer[1][i] = av_lfg_get(&prng) % 19000 - 9000;

    ff_spatial_dwt(buffer[0], s.temp_dwt_buffer, width, height, width, s.spatial_decomposition_type, s.spatial_decomposition_count);
    for(i=0; i<width*height; i++)
        obuffer[i] = buffer[0][i];
    ff_spatial_idwt(obuffer, s.temp_idwt_buffer, width, height, width, s.spatial_decomposition_type, s.spatial_decomposition_count);

    for(i=0; i<width*height; i++)
        if(buffer[1][i]!= obuffer[i]) {
            printf("fsck: %4dx%4dx %12d %7d\n",i%width, i/width, buffer[1][i], obuffer[i]);
            ret = 1;
        }

    printf("testing 9/7 DWT\n");
    s.spatial_decomposition_type=0;
    for(i=0; i<width*height; i++)
        buffer[0][i] = buffer[1][i] = av_lfg_get(&prng) % 11000 - 5000;

    ff_spatial_dwt(buffer[0], s.temp_dwt_buffer, width, height, width, s.spatial_decomposition_type, s.spatial_decomposition_count);
    for(i=0; i<width*height; i++)
        obuffer[i] = buffer[0][i];
    ff_spatial_idwt(obuffer, s.temp_idwt_buffer, width, height, width, s.spatial_decomposition_type, s.spatial_decomposition_count);

    for(i=0; i<width*height; i++)
        if(FFABS(buffer[1][i] - obuffer[i])>20) {
            printf("fsck: %4dx%4d %12d %7d\n",i%width, i/width, buffer[1][i], obuffer[i]);
            ret = 1;
        }

    {
    int level, orientation, x, y;
    int64_t errors[8][4];
    int64_t g=0;

        memset(errors, 0, sizeof(errors));
        s.spatial_decomposition_count=3;
        s.spatial_decomposition_type=0;
        for(level=0; level<s.spatial_decomposition_count; level++){
            for(orientation=level ? 1 : 0; orientation<4; orientation++){
                int w= width  >> (s.spatial_decomposition_count-level);
                int h= height >> (s.spatial_decomposition_count-level);
                int stride= width  << (s.spatial_decomposition_count-level);
                IDWTELEM *buf= obuffer;
                int64_t error=0;

                if(orientation&1) buf+=w;
                if(orientation>1) buf+=stride>>1;

                memset(obuffer, 0, sizeof(short)*width*height);
                buf[w/2 + h/2*stride]= 8*256;
                ff_spatial_idwt(obuffer, s.temp_idwt_buffer, width, height, width, s.spatial_decomposition_type, s.spatial_decomposition_count);
                for(y=0; y<height; y++){
                    for(x=0; x<width; x++){
                        int64_t d= obuffer[x + y*width];
                        error += d*d;
                        if(FFABS(width/2-x)<9 && FFABS(height/2-y)<9 && level==2) printf("%8"PRId64" ", d);
                    }
                    if(FFABS(height/2-y)<9 && level==2) printf("\n");
                }
                error= (int)(sqrt(error)+0.5);
                errors[level][orientation]= error;
                if(g) g=av_gcd(g, error);
                else g= error;
            }
        }
        printf("static int const visual_weight[][4]={\n");
        for(level=0; level<s.spatial_decomposition_count; level++){
            printf("  {");
            for(orientation=0; orientation<4; orientation++){
                printf("%8"PRId64",", errors[level][orientation]/g);
            }
            printf("},\n");
        }
        printf("};\n");
        {
            memset(buffer[0], 0, sizeof(int)*width*height);
            for(y=0; y<height; y++){
                for(x=0; x<width; x++){
                    int tab[4]={0,2,3,1};
                    buffer[0][x+width*y]= 256*256*tab[(x&1) + 2*(y&1)];
                }
            }
            ff_spatial_dwt(buffer[0], s.temp_dwt_buffer, width, height, width, s.spatial_decomposition_type, s.spatial_decomposition_count);
            for(y=0; y<height; y++){
                for(x=0; x<width; x++){
                    int64_t d= buffer[0][x + y*width];
                    if(FFABS(width/2-x)<9 && FFABS(height/2-y)<9) printf("%8"PRId64" ", d);
                }
                if(FFABS(height/2-y)<9) printf("\n");
            }
        }

    }

end:
    av_free(s.temp_dwt_buffer);
    av_free(s.temp_idwt_buffer);
    return ret;
}
