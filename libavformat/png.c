/*
 * PNG image format
 * Copyright (c) 2003 Fabrice Bellard.
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

#include "libpng/png.h"

extern const uint8_t png_sig[];

static int png_probe(AVProbeData *pd)
{
    if (pd->buf_size >= 8 &&
        memcmp(pd->buf, png_sig, 8) == 0)
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

png_voidp PNGAPI
png_memset_check (png_structp png_ptr, png_voidp s1, int value,
   png_uint_32 length)
{
   png_size_t size;

   size = (png_size_t)length;
   if ((png_uint_32)size != length)
      png_error(png_ptr,"Overflow in png_memset_check.");

   return (png_memset (s1, value, size));

}

png_voidp PNGAPI
png_memcpy_check (png_structp png_ptr, png_voidp s1, png_voidp s2,
   png_uint_32 length)
{
   png_size_t size;

   size = (png_size_t)length;
   if ((png_uint_32)size != length)
      png_error(png_ptr,"Overflow in png_memcpy_check.");

   return(png_memcpy (s1, s2, size));
}

void png_error (png_struct *png_ptr, const char *message)
{
    longjmp(png_ptr->jmpbuf, 0);
}

void png_warning (png_struct *png_ptr, const char *message)
{
}

void PNGAPI
png_chunk_error(png_structp png_ptr, png_const_charp error_message)
{
   char msg[18+64];
   //   png_format_buffer(png_ptr, msg, error_message);
   png_error(png_ptr, msg);
}

void PNGAPI
png_chunk_warning(png_structp png_ptr, png_const_charp warning_message)
{
   char msg[18+64];
   //   png_format_buffer(png_ptr, msg, warning_message);
   png_warning(png_ptr, msg);
}

void png_read_data (png_struct *png_ptr, png_byte *data, png_uint_32 length)
{
    int ret;

    ret = get_buffer(png_ptr->io_ptr, data, length);
    if (ret != length)
	png_error (png_ptr, "Read Error");
}

void *png_malloc (png_struct *png_ptr, png_uint_32 size)
{
    return av_malloc(size);
}

void png_free (png_struct *png_ptr, void *ptr)
{
    return av_free(ptr);
}

static int png_read(ByteIOContext *f, 
                    int (*alloc_cb)(void *opaque, AVImageInfo *info), void *opaque)
{
    png_struct png_struct1, *png_ptr;
    png_info png_info1, png_info2, *info_ptr, *end_info;
    AVImageInfo info1, *info = &info1;
    int y, height, ret, row_size;
    uint8_t *ptr;

    png_ptr = &png_struct1;
    png_read_init(png_ptr);

    info_ptr = &png_info1;
    end_info = &png_info2;
    
    png_info_init(info_ptr);
    png_info_init(end_info);
    
    png_ptr->io_ptr = f;

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_read_destroy (png_ptr, info_ptr, NULL);
        return -1;
    }
    
    png_read_info (png_ptr, info_ptr);
    
    /* init image info */
    info->width = info_ptr->width;
    info->height = info_ptr->height;
    
    if (info_ptr->bit_depth == 8 && 
        info_ptr->color_type == PNG_COLOR_TYPE_RGB) {
        info->pix_fmt = PIX_FMT_RGB24;
        row_size = info_ptr->width * 3;
    } else if (info_ptr->bit_depth == 8 && 
               info_ptr->color_type == PNG_COLOR_TYPE_GRAY) {
        info->pix_fmt = PIX_FMT_GRAY8;
        row_size = info_ptr->width;
    } else if (info_ptr->bit_depth == 1 && 
               info_ptr->color_type == PNG_COLOR_TYPE_GRAY) {
        info->pix_fmt = PIX_FMT_MONOBLACK;
        row_size = (info_ptr->width + 7) >> 3;
    } else {
        png_read_destroy (png_ptr, info_ptr, NULL);
        return -1;
    }
    ret = alloc_cb(opaque, info);
    if (ret) {
        png_read_destroy (png_ptr, info_ptr, NULL);
        return ret;
    }
    
    /* now read the whole image */
    png_start_read_image (png_ptr);

    height = info->height;
    ptr = info->pict.data[0];
    for (y = 0; y < height; y++) {
        png_read_row (png_ptr, NULL, NULL);
        memcpy(ptr, png_ptr->row_buf + 1, row_size);
        ptr += info->pict.linesize[0];
    }
    
    png_read_destroy (png_ptr, info_ptr, NULL);
    return 0;
}

void
png_write_data(png_structp png_ptr, png_bytep data, png_size_t length)
{
    put_buffer(png_ptr->io_ptr, data, length);
}

static int png_write(ByteIOContext *pb, AVImageInfo *info)
{
    png_struct png_struct1, *png_ptr;
    png_info png_info1, *info_ptr;
    int w, h, y;
    uint8_t *ptr;

    png_ptr = &png_struct1;
    info_ptr = &png_info1;

    png_write_init(png_ptr);
    png_info_init(info_ptr);
    
    png_ptr->io_ptr = pb;

    if (setjmp(png_ptr->jmpbuf)) {
        png_write_destroy(png_ptr);
        return -1;
    }

    w = info->width;
    h = info->height;

    info_ptr->width = w;
    info_ptr->height = h;
    switch(info->pix_fmt) {
    case PIX_FMT_RGB24:
        info_ptr->bit_depth = 8;
        info_ptr->color_type = PNG_COLOR_TYPE_RGB;
        break;
    case PIX_FMT_GRAY8:
        info_ptr->bit_depth = 8;
        info_ptr->color_type = PNG_COLOR_TYPE_GRAY;
        break;
    case PIX_FMT_MONOBLACK:
        info_ptr->bit_depth = 1;
        info_ptr->color_type = PNG_COLOR_TYPE_GRAY;
        break;
    default:
        return -1;
    }
    png_write_info(png_ptr, info_ptr);

    ptr = info->pict.data[0];
    for(y=0;y<h;y++) {
        png_write_row(png_ptr, ptr);
        ptr += info->pict.linesize[0];
    }
    png_write_end(png_ptr, info_ptr);
    png_write_destroy(png_ptr);
    put_flush_packet(pb);
    return 0;
}

AVImageFormat png_image_format = {
    "png",
    "png",
    png_probe,
    png_read,
    (1 << PIX_FMT_RGB24) | (1 << PIX_FMT_GRAY8) | (1 << PIX_FMT_MONOBLACK),
    png_write,
};
