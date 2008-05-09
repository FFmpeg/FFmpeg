/*
 * drawtext.c: print text over the screen
 ******************************************************************************
 * Options:
 * -f <filename>    font filename (MANDATORY!!!)
 * -s <pixel_size>  font size in pixels [default 16]
 * -b               print background
 * -o               outline glyphs (use the bg color)
 * -x <pos>         x position ( >= 0) [default 0]
 * -y <pos>         y position ( >= 0) [default 0]
 * -t <text>        text to print (will be passed to strftime())
 *                  MANDATORY: will be used even when -T is used.
 *                  in this case, -t will be used if some error
 *                  occurs
 * -T <filename>    file with the text (re-read every frame)
 * -c <#RRGGBB>     foreground color ('internet' way) [default #ffffff]
 * -C <#RRGGBB>     background color ('internet' way) [default #000000]
 *
 ******************************************************************************
 * Features:
 * - True Type, Type1 and others via FreeType2 library
 * - Font kerning (better output)
 * - Line Wrap (if the text doesn't fit, the next char go to the next line)
 * - Background box
 * - Outline
 ******************************************************************************
 * Author: Gustavo Sverzut Barbieri <gsbarbieri@yahoo.com.br>
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

#define MAXSIZE_TEXT 1024

#include "libavformat/framehook.h"

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#undef time
#include <sys/time.h>
#include <time.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

#define SCALEBITS 10
#define ONE_HALF  (1 << (SCALEBITS - 1))
#define FIX(x)    ((int) ((x) * (1<<SCALEBITS) + 0.5))

#define RGB_TO_YUV(rgb_color, yuv_color) do { \
  yuv_color[0] = (FIX(0.29900)    * rgb_color[0] + FIX(0.58700) * rgb_color[1] + FIX(0.11400) * rgb_color[2] + ONE_HALF) >> SCALEBITS; \
  yuv_color[2] = ((FIX(0.50000)   * rgb_color[0] - FIX(0.41869) * rgb_color[1] - FIX(0.08131) * rgb_color[2] + ONE_HALF - 1) >> SCALEBITS) + 128; \
  yuv_color[1] = ((- FIX(0.16874) * rgb_color[0] - FIX(0.33126) * rgb_color[1] + FIX(0.50000) * rgb_color[2] + ONE_HALF - 1) >> SCALEBITS) + 128; \
} while (0)

#define COPY_3(dst,src) { \
    dst[0]=src[0]; \
    dst[1]=src[1]; \
    dst[2]=src[2]; \
}



#define SET_PIXEL(picture, yuv_color, x, y) { \
    picture->data[0][ (x) + (y)*picture->linesize[0] ] = yuv_color[0]; \
    picture->data[1][ ((x/2) + (y/2)*picture->linesize[1]) ] = yuv_color[1]; \
    picture->data[2][ ((x/2) + (y/2)*picture->linesize[2]) ] = yuv_color[2]; \
}

#define GET_PIXEL(picture, yuv_color, x, y) { \
    yuv_color[0] = picture->data[0][ (x) + (y)*picture->linesize[0] ]; \
    yuv_color[1] = picture->data[1][ (x/2) + (y/2)*picture->linesize[1] ]; \
    yuv_color[2] = picture->data[2][ (x/2) + (y/2)*picture->linesize[2] ]; \
}


typedef struct {
  unsigned char *text;
  char *file;
  unsigned int x;
  unsigned int y;
  int bg;
  int outline;
  unsigned char bgcolor[3]; /* YUV */
  unsigned char fgcolor[3]; /* YUV */
  FT_Library library;
  FT_Face    face;
  FT_Glyph   glyphs[ 255 ];
  FT_Bitmap  bitmaps[ 255 ];
  int        advance[ 255 ];
  int        bitmap_left[ 255 ];
  int        bitmap_top[ 255 ];
  unsigned int glyphs_index[ 255 ];
  int        text_height;
  int        baseline;
  int use_kerning;
} ContextInfo;


void Release(void *ctx)
{
    if (ctx)
        av_free(ctx);
}


static int ParseColor(char *text, unsigned char yuv_color[3])
{
  char tmp[3];
  unsigned char rgb_color[3];
  int i;

  tmp[2] = '\0';

  if ((!text) || (strlen(text) != 7) || (text[0] != '#') )
    return -1;

  for (i=0; i < 3; i++)
    {
      tmp[0] = text[i*2+1];
      tmp[1] = text[i*2+2];

      rgb_color[i] = strtol(tmp, NULL, 16);
    }

  RGB_TO_YUV(rgb_color, yuv_color);

  return 0;
}

int Configure(void **ctxp, int argc, char *argv[])
{
    int c;
    int error;
    ContextInfo *ci=NULL;
    char *font=NULL;
    unsigned int size=16;
    FT_BBox bbox;
    int yMax, yMin;
    *ctxp = av_mallocz(sizeof(ContextInfo));
    ci = (ContextInfo *) *ctxp;

    /* configure Context Info */
    ci->text = NULL;
    ci->file = NULL;
    ci->x = ci->y = 0;
    ci->fgcolor[0]=255;
    ci->fgcolor[1]=128;
    ci->fgcolor[2]=128;
    ci->bgcolor[0]=0;
    ci->fgcolor[1]=128;
    ci->fgcolor[2]=128;
    ci->bg = 0;
    ci->outline = 0;
    ci->text_height = 0;

    optind = 1;
    while ((c = getopt(argc, argv, "f:t:T:x:y:s:c:C:bo")) > 0) {
      switch (c) {
      case 'f':
        font = optarg;
        break;
      case 't':
        ci->text = av_strdup(optarg);
        break;
      case 'T':
        ci->file = av_strdup(optarg);
        break;
      case 'x':
        ci->x = (unsigned int) atoi(optarg);
        break;
      case 'y':
        ci->y = (unsigned int) atoi(optarg);
        break;
      case 's':
        size = (unsigned int) atoi(optarg);
        break;
      case 'c':
        if (ParseColor(optarg, ci->fgcolor) == -1)
          {
            av_log(NULL, AV_LOG_ERROR, "Invalid foreground color: '%s'. You must specify the color in the internet way(packaged hex): #RRGGBB, ie: -c #ffffff (for white foreground)\n", optarg);
            return -1;
          }
        break;
      case 'C':
        if (ParseColor(optarg, ci->bgcolor) == -1)
          {
            av_log(NULL, AV_LOG_ERROR, "Invalid background color: '%s'. You must specify the color in the internet way(packaged hex): #RRGGBB, ie: -C #ffffff (for white background)\n", optarg);
            return -1;
          }
        break;
      case 'b':
        ci->bg=1;
        break;
      case 'o':
        ci->outline=1;
        break;
      case '?':
        av_log(NULL, AV_LOG_ERROR, "Unrecognized argument '%s'\n", argv[optind]);
        return -1;
      }
    }

    if (!ci->text)
      {
        av_log(NULL, AV_LOG_ERROR, "No text provided (-t text)\n");
        return -1;
      }

    if (ci->file)
      {
        FILE *fp;
        if ((fp=fopen(ci->file, "r")) == NULL)
          {
            av_log(NULL, AV_LOG_INFO, "WARNING: The file could not be opened. Using text provided with -t switch: %s", strerror(errno));
          }
        else
          {
            fclose(fp);
          }
      }

    if (!font)
      {
        av_log(NULL, AV_LOG_ERROR, "No font file provided! (-f filename)\n");
        return -1;
      }

    if ((error = FT_Init_FreeType(&(ci->library))) != 0)
      {
        av_log(NULL, AV_LOG_ERROR, "Could not load FreeType (error# %d).\n", error);
        return -1;
      }

    if ((error = FT_New_Face( ci->library, font, 0, &(ci->face) )) != 0)
      {
        av_log(NULL, AV_LOG_ERROR, "Could not load face: %s  (error# %d).\n", font, error);
        return -1;
      }

    if ((error = FT_Set_Pixel_Sizes( ci->face, 0, size)) != 0)
      {
        av_log(NULL, AV_LOG_ERROR, "Could not set font size to %d pixels (error# %d).\n", size, error);
        return -1;
      }

    ci->use_kerning = FT_HAS_KERNING(ci->face);

    /* load and cache glyphs */
    yMax = -32000;
    yMin =  32000;
    for (c=0; c < 256; c++)
      {
        /* Load char */
        error = FT_Load_Char( ci->face, (unsigned char) c, FT_LOAD_RENDER | FT_LOAD_MONOCHROME );
        if (error) continue;  /* ignore errors */

        /* Save bitmap */
        ci->bitmaps[c] = ci->face->glyph->bitmap;
        /* Save bitmap left */
        ci->bitmap_left[c] = ci->face->glyph->bitmap_left;
        /* Save bitmap top */
        ci->bitmap_top[c] = ci->face->glyph->bitmap_top;

        /* Save advance */
        ci->advance[c] = ci->face->glyph->advance.x >> 6;

        /* Save glyph */
        error = FT_Get_Glyph( ci->face->glyph, &(ci->glyphs[c]) );
        /* Save glyph index */
        ci->glyphs_index[c] = FT_Get_Char_Index( ci->face, (unsigned char) c );

        /* Measure text height to calculate text_height (or the maximum text height) */
        FT_Glyph_Get_CBox( ci->glyphs[ c ], ft_glyph_bbox_pixels, &bbox );
        if (bbox.yMax > yMax)
          yMax = bbox.yMax;
        if (bbox.yMin < yMin)
          yMin = bbox.yMin;

      }

    ci->text_height = yMax - yMin;
    ci->baseline = yMax;

    return 0;
}




static inline void draw_glyph(AVPicture *picture, FT_Bitmap *bitmap, unsigned int x, unsigned int y, unsigned int width, unsigned int height, unsigned char yuv_fgcolor[3], unsigned char yuv_bgcolor[3], int outline)
{
  int r, c;
  int spixel, dpixel[3], in_glyph=0;

  if (bitmap->pixel_mode == ft_pixel_mode_mono)
    {
      in_glyph = 0;
      for (r=0; (r < bitmap->rows) && (r+y < height); r++)
        {
          for (c=0; (c < bitmap->width) && (c+x < width); c++)
            {
              /* pixel in the picture (destination) */
              GET_PIXEL(picture, dpixel, (c+x), (y+r));

              /* pixel in the glyph bitmap (source) */
              spixel = bitmap->buffer[r*bitmap->pitch +c/8] & (0x80>>(c%8));

              if (spixel)
                COPY_3(dpixel, yuv_fgcolor);

              if (outline)
                {
                  /* border detection: */
                  if ( (!in_glyph) && (spixel) )
                    /* left border detected */
                    {
                      in_glyph = 1;
                      /* draw left pixel border */
                      if (c-1 >= 0)
                        SET_PIXEL(picture, yuv_bgcolor, (c+x-1), (y+r));
                    }
                  else if ( (in_glyph) && (!spixel) )
                    /* right border detected */
                    {
                      in_glyph = 0;
                      /* 'draw' right pixel border */
                      COPY_3(dpixel, yuv_bgcolor);
                    }

                  if (in_glyph)
                    /* see if we have a top/bottom border */
                    {
                      /* top */
                      if ( (r-1 >= 0) && (! bitmap->buffer[(r-1)*bitmap->pitch +c/8] & (0x80>>(c%8))) )
                        /* we have a top border */
                        SET_PIXEL(picture, yuv_bgcolor, (c+x), (y+r-1));

                      /* bottom */
                      if ( (r+1 < height) && (! bitmap->buffer[(r+1)*bitmap->pitch +c/8] & (0x80>>(c%8))) )
                        /* we have a bottom border */
                        SET_PIXEL(picture, yuv_bgcolor, (c+x), (y+r+1));

                    }
                }

              SET_PIXEL(picture, dpixel, (c+x), (y+r));
            }
        }
    }
}


static inline void draw_box(AVPicture *picture, unsigned int x, unsigned int y, unsigned int width, unsigned int height, unsigned char yuv_color[3])
{
  int i, j;

  for (j = 0; (j < height); j++)
    for (i = 0; (i < width); i++)
      {
        SET_PIXEL(picture, yuv_color, (i+x), (y+j));
      }

}




void Process(void *ctx, AVPicture *picture, enum PixelFormat pix_fmt, int width, int height, int64_t pts)
{
  ContextInfo *ci = (ContextInfo *) ctx;
  FT_Face face = ci->face;
  FT_GlyphSlot  slot = face->glyph;
  unsigned char *text = ci->text;
  unsigned char c;
  int x = 0, y = 0, i=0, size=0;
  unsigned char buff[MAXSIZE_TEXT];
  unsigned char tbuff[MAXSIZE_TEXT];
  time_t now = time(0);
  int str_w, str_w_max;
  FT_Vector pos[MAXSIZE_TEXT];
  FT_Vector delta;

  if (ci->file)
    {
      int fd = open(ci->file, O_RDONLY);

      if (fd < 0)
        {
          text = ci->text;
          av_log(NULL, AV_LOG_INFO, "WARNING: The file could not be opened. Using text provided with -t switch: %s", strerror(errno));
        }
      else
        {
          int l = read(fd, tbuff, sizeof(tbuff) - 1);

          if (l >= 0)
            {
              tbuff[l] = 0;
              text = tbuff;
            }
          else
            {
              text = ci->text;
              av_log(NULL, AV_LOG_INFO, "WARNING: The file could not be read. Using text provided with -t switch: %s", strerror(errno));
            }
          close(fd);
        }
    }
  else
    {
      text = ci->text;
    }

  strftime(buff, sizeof(buff), text, localtime(&now));

  text = buff;

  size = strlen(text);




  /* measure string size and save glyphs position*/
  str_w = str_w_max = 0;
  x = ci->x;
  y = ci->y;
  for (i=0; i < size; i++)
    {
      c = text[i];

      /* kerning */
      if ( (ci->use_kerning) && (i > 0) && (ci->glyphs_index[c]) )
        {
          FT_Get_Kerning( ci->face,
                          ci->glyphs_index[ text[i-1] ],
                          ci->glyphs_index[c],
                          ft_kerning_default,
                          &delta );

          x += delta.x >> 6;
        }

      if (( (x + ci->advance[ c ]) >= width ) || ( c == '\n' ))
        {
          str_w = width - ci->x - 1;

          y += ci->text_height;
          x = ci->x;
        }


      /* save position */
      pos[i].x = x + ci->bitmap_left[c];
      pos[i].y = y - ci->bitmap_top[c] + ci->baseline;


      x += ci->advance[c];


      if (str_w > str_w_max)
        str_w_max = str_w;

    }




  if (ci->bg)
    {
      /* Check if it doesn't pass the limits */
      if ( str_w_max + ci->x >= width )
        str_w_max = width - ci->x - 1;
      if ( y >= height )
        y = height - 1 - 2*ci->y;

      /* Draw Background */
      draw_box( picture, ci->x, ci->y, str_w_max, y - ci->y, ci->bgcolor );
    }



  /* Draw Glyphs */
  for (i=0; i < size; i++)
    {
      c = text[i];

      if (
          ( (c == '_') && (text == ci->text) ) || /* skip '_' (consider as space)
                                                     IF text was specified in cmd line
                                                     (which doesn't like nested quotes)  */
          ( c == '\n' ) /* Skip new line char, just go to new line */
          )
        continue;

        /* now, draw to our target surface */
        draw_glyph( picture,
                    &(ci->bitmaps[ c ]),
                    pos[i].x,
                    pos[i].y,
                    width,
                    height,
                    ci->fgcolor,
                    ci->bgcolor,
                    ci->outline );

      /* increment pen position */
      x += slot->advance.x >> 6;
    }


}

