/*
 * Copyright (c) 2007 Michael Niedermayer
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

#define _XOPEN_SOURCE 600
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>

int main(int argc, char** argv)
{
    FILE *f;
    int count, maxburst, length;

    if (argc < 4){
        printf("USAGE: trasher <filename> <count> <maxburst>\n");
        return 1;
    }

    f= fopen(argv[1], "rb+");
    if (!f){
        perror(argv[1]);
        return 2;
    }
    count= atoi(argv[2]);
    maxburst= atoi(argv[3]);

    srandom (time (0));

    fseek(f, 0, SEEK_END);
    length= ftell(f);
    fseek(f, 0, SEEK_SET);

    while(count--){
        int burst= 1 + random() * (uint64_t) (abs(maxburst)-1) / RAND_MAX;
        int pos= random() * (uint64_t) length / RAND_MAX;
        fseek(f, pos, SEEK_SET);

        if(maxburst<0) burst= -maxburst;

        if(pos + burst > length)
            continue;

        while(burst--){
            int val= random() * 256ULL / RAND_MAX;

            if(maxburst<0) val=0;

            fwrite(&val, 1, 1, f);
        }
    }

    return 0;
}
