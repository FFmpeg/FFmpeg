/*
 * SGI image format
 * Todd Kirby <doubleshot@pacbell.net>
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

#include "avformat.h"
#include "avio.h"

/* #define DEBUG */

/* sgi image file signature */
#define SGI_MAGIC 474

#define SGI_HEADER_SIZE 512

#define SGI_GRAYSCALE 1
#define SGI_RGB 3
#define SGI_RGBA 4

#define SGI_SINGLE_CHAN 2
#define SGI_MULTI_CHAN 3

typedef struct SGIInfo{
    short magic;
    char rle;
    char bytes_per_channel;
    unsigned short dimension;
    unsigned short xsize;
    unsigned short ysize;
    unsigned short zsize;
} SGIInfo;


static int sgi_probe(AVProbeData *pd)
{
    /* test for sgi magic */
    if (pd->buf_size >= 2 && BE_16(&pd->buf[0]) == SGI_MAGIC) {
        return AVPROBE_SCORE_MAX;
    } else {
        return 0;
    }
}

/* read sgi header fields */
static void read_sgi_header(ByteIOContext *f, SGIInfo *info)
{
    info->magic = (unsigned short) get_be16(f);
    info->rle = get_byte(f);
    info->bytes_per_channel = get_byte(f);
    info->dimension = (unsigned short)get_be16(f);
    info->xsize = (unsigned short) get_be16(f);
    info->ysize = (unsigned short) get_be16(f);
    info->zsize = (unsigned short) get_be16(f);
    
    if(info->zsize > 4096) 
        info->zsize= 0;

#ifdef DEBUG
    printf("sgi header fields:\n");
    printf("  magic: %d\n", info->magic);
    printf("    rle: %d\n", info->rle);
    printf("    bpc: %d\n", info->bytes_per_channel);
    printf("    dim: %d\n", info->dimension);
    printf("  xsize: %d\n", info->xsize);
    printf("  ysize: %d\n", info->ysize);
    printf("  zsize: %d\n", info->zsize);
#endif

    return;
}


/* read an uncompressed sgi image */
static int read_uncompressed_sgi(const SGIInfo *si, 
        AVPicture *pict, ByteIOContext *f)
{
    int x, y, z, chan_offset, ret = 0;
    uint8_t *dest_row;

    /* skip header */ 
    url_fseek(f, SGI_HEADER_SIZE, SEEK_SET);

    pict->linesize[0] = si->xsize;

    for (z = 0; z < si->zsize; z++) {

#ifndef WORDS_BIGENDIAN
        /* rgba -> bgra for rgba32 on little endian cpus */
        if (si->zsize == 4 && z != 3) 
            chan_offset = 2 - z;
        else
#endif
            chan_offset = z;
            
        for (y = si->ysize - 1; y >= 0; y--) {
            dest_row = pict->data[0] + (y * si->xsize * si->zsize);

            for (x = 0; x < si->xsize; x++) {
                dest_row[chan_offset] = get_byte(f); 
                dest_row += si->zsize;
            }
        }
    }

    return ret;
}


/* expand an rle row into a channel */
static int expand_rle_row(ByteIOContext *f, unsigned char *optr,
        int chan_offset, int pixelstride)
{
    unsigned char pixel, count;
    int length = 0;
 
#ifndef WORDS_BIGENDIAN
    /* rgba -> bgra for rgba32 on little endian cpus */
    if (pixelstride == 4 && chan_offset != 3) {
       chan_offset = 2 - chan_offset;
    }
#endif
        
    optr += chan_offset;

    while (1) {
        pixel = get_byte(f);

        if (!(count = (pixel & 0x7f))) {
            return length;
        }
        if (pixel & 0x80) {
            while (count--) {
                *optr = get_byte(f);
                length++;
                optr += pixelstride;
            }
        } else {
            pixel = get_byte(f);

            while (count--) {
                *optr = pixel;
                length++;
                optr += pixelstride;
            }
        }
    }
}


/* read a run length encoded sgi image */
static int read_rle_sgi(const SGIInfo *sgi_info, 
        AVPicture *pict, ByteIOContext *f)
{
    uint8_t *dest_row;
    unsigned long *start_table;
    int y, z, xsize, ysize, zsize, tablen; 
    long start_offset;
    int ret = 0;

    xsize = sgi_info->xsize;
    ysize = sgi_info->ysize;
    zsize = sgi_info->zsize;

    /* skip header */ 
    url_fseek(f, SGI_HEADER_SIZE, SEEK_SET);

    /* size of rle offset and length tables */
    tablen = ysize * zsize * sizeof(long);

    start_table = (unsigned long *)av_malloc(tablen);

    if (!get_buffer(f, (uint8_t *)start_table, tablen)) {
        ret = AVERROR_IO;
        goto fail;
    }

    /* skip run length table */ 
    url_fseek(f, tablen, SEEK_CUR);

    for (z = 0; z < zsize; z++) {
        for (y = 0; y < ysize; y++) {
            dest_row = pict->data[0] + (ysize - 1 - y) * (xsize * zsize);

            start_offset = BE_32(&start_table[y + z * ysize]);

            /* don't seek if already at the next rle start offset */
            if (url_ftell(f) != start_offset) {
                url_fseek(f, start_offset, SEEK_SET);
            }

            if (expand_rle_row(f, dest_row, z, zsize) != xsize) {
              ret =  AVERROR_INVALIDDATA;
              goto fail;
            }
        }
    }

fail:
    av_free(start_table);

    return ret;
}


static int sgi_read(ByteIOContext *f, 
        int (*alloc_cb)(void *opaque, AVImageInfo *info), void *opaque)
{
    SGIInfo sgi_info, *s = &sgi_info;
    AVImageInfo info1, *info = &info1;
    int ret;

    read_sgi_header(f, s);

    if (s->bytes_per_channel != 1) {
        return AVERROR_INVALIDDATA;
    }

    /* check for supported image dimensions */
    if (s->dimension != 2 && s->dimension != 3) {
        return AVERROR_INVALIDDATA;
    }

    if (s->zsize == SGI_GRAYSCALE) {
        info->pix_fmt = PIX_FMT_GRAY8;
    } else if (s->zsize == SGI_RGB) {
        info->pix_fmt = PIX_FMT_RGB24;
    } else if (s->zsize == SGI_RGBA) {
        info->pix_fmt = PIX_FMT_RGBA32;
    } else {
        return AVERROR_INVALIDDATA;
    }

    info->width = s->xsize;
    info->height = s->ysize;

    ret = alloc_cb(opaque, info);
    if (ret)
        return ret;

    if (s->rle) {
        return read_rle_sgi(s, &info->pict, f);
    } else {
        return read_uncompressed_sgi(s, &info->pict, f);
    }

    return 0; /* not reached */
}

#ifdef CONFIG_MUXERS
static void write_sgi_header(ByteIOContext *f, const SGIInfo *info)
{
    int i;

    put_be16(f, SGI_MAGIC);
    put_byte(f, info->rle);
    put_byte(f, info->bytes_per_channel); 
    put_be16(f, info->dimension);
    put_be16(f, info->xsize);
    put_be16(f, info->ysize);
    put_be16(f, info->zsize);

    /* The rest are constant in this implementation */
    put_be32(f, 0L); /* pixmin */ 
    put_be32(f, 255L); /* pixmax */ 
    put_be32(f, 0L); /* dummy */ 

    /* name */
    for (i = 0; i < 80; i++) {
        put_byte(f, 0);
    }

    put_be32(f, 0L); /* colormap */ 

    /* The rest of the 512 byte header is unused. */
    for (i = 0; i < 404; i++) {
        put_byte(f, 0);
    }
}


static int rle_row(ByteIOContext *f, char *row, int stride, int rowsize)
{
    int length, count, i, x;
    char *start, repeat = 0;

    for (x = rowsize, length = 0; x > 0;) {
        start = row;
        row += (2 * stride);
        x -= 2;

        while (x > 0 && (row[-2 * stride] != row[-1 * stride] || 
                    row[-1 * stride] != row[0])) {
            row += stride;
            x--;
        };

        row -= (2 * stride);
        x += 2;

        count = (row - start) / stride;
        while (count > 0) {
            i = count > 126 ? 126 : count;
            count -= i;

            put_byte(f, 0x80 | i); 
            length++;

            while (i > 0) {
                put_byte(f, *start);
                start += stride;
                i--;
                length++;
            };
        };

        if (x <= 0) {
            break;
        }

        start = row;
        repeat = row[0];

        row += stride;
        x--;

        while (x > 0 && *row == repeat) {
            row += stride;
            x--;
        };

        count = (row - start) / stride;
        while (count > 0) {
            i = count > 126 ? 126 : count;
            count -= i;

            put_byte(f, i);
            length++;

            put_byte(f, repeat); 
            length++;
        };
    };

    length++;

    put_byte(f, 0); 
    return (length);
}


static int sgi_write(ByteIOContext *pb, AVImageInfo *info)
{
    SGIInfo sgi_info, *si = &sgi_info;
    long *offsettab, *lengthtab;
    int i, y, z;
    int tablesize, chan_offset;
    uint8_t *srcrow;

    si->xsize = info->width;
    si->ysize = info->height;
    si->rle = 1;
    si->bytes_per_channel = 1;
    
    switch(info->pix_fmt) {
        case PIX_FMT_GRAY8:
            si->dimension = SGI_SINGLE_CHAN;
            si->zsize = SGI_GRAYSCALE;
            break;
        case PIX_FMT_RGB24:
            si->dimension = SGI_MULTI_CHAN;
            si->zsize = SGI_RGB;
            break;
         case PIX_FMT_RGBA32:
            si->dimension = SGI_MULTI_CHAN;
            si->zsize = SGI_RGBA;
            break;
        default:
            return AVERROR_INVALIDDATA;
    }

    write_sgi_header(pb, si); 

    tablesize = si->zsize * si->ysize * sizeof(long);
    
    /* skip rle offset and length tables, write them at the end. */
    url_fseek(pb, tablesize * 2, SEEK_CUR);
    put_flush_packet(pb);
    
    lengthtab = av_malloc(tablesize);
    offsettab = av_malloc(tablesize);

    for (z = 0; z < si->zsize; z++) {

#ifndef WORDS_BIGENDIAN
        /* rgba -> bgra for rgba32 on little endian cpus */
        if (si->zsize == 4 && z != 3) 
            chan_offset = 2 - z;
        else
#endif
            chan_offset = z;
        
        srcrow = info->pict.data[0] + chan_offset;
        
        for (y = si->ysize -1; y >= 0; y--) {
            offsettab[(z * si->ysize) + y] = url_ftell(pb);
            lengthtab[(z * si->ysize) + y] = rle_row(pb, srcrow,
                    si->zsize, si->xsize);
            srcrow += info->pict.linesize[0]; 
        }
    }

    url_fseek(pb, 512, SEEK_SET);
    
    /* write offset table */
    for (i = 0; i < (si->ysize * si->zsize); i++) {
        put_be32(pb, offsettab[i]);
    }
 
    /* write length table */
    for (i = 0; i < (si->ysize * si->zsize); i++) {
        put_be32(pb, lengthtab[i]);
    }

    put_flush_packet(pb);
    
    av_free(lengthtab);
    av_free(offsettab);

    return 0;
}
#endif // CONFIG_MUXERS

AVImageFormat sgi_image_format = {
    "sgi",
    "sgi,rgb,rgba,bw",
    sgi_probe,
    sgi_read,
    (1 << PIX_FMT_GRAY8) | (1 << PIX_FMT_RGB24) | (1 << PIX_FMT_RGBA32), 
#ifdef CONFIG_MUXERS
    sgi_write,
#else
    NULL,
#endif // CONFIG_MUXERS
};
