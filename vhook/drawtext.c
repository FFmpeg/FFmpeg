/*
 * drawtext.c: print text over the screen
 ******************************************************************
 * Options:
 * -f <filename>    font filename
 * -s <pixel_size>  font size in pixels
 * -b               print background
 * -o               outline glyphs (use the bg color)
 * -x <pos>         x position ( > 0)
 * -y <pos>         y position ( > 0)
 * -t <text>        text to print (will be passed to strftime())
 * -c <#RRGGBB>     foreground color ('internet' way)
 * -C <#RRGGBB>     background color ('internet' way)
 *
 ******************************************************************
 *
 * Author: Gustavo Sverzut Barbieri <gsbarbieri@yahoo.com.br>
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
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>

#include "framehook.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#define RGB_TO_YUV(rgb_color, yuv_color) { \
    yuv_color[0] = ( 0.257 * rgb_color[0]) + (0.504 * rgb_color[1]) + (0.098 * rgb_color[2]) +  16; \
    yuv_color[2] = ( 0.439 * rgb_color[0]) - (0.368 * rgb_color[1]) - (0.071 * rgb_color[2]) + 128; \
    yuv_color[1] = (-0.148 * rgb_color[0]) - (0.291 * rgb_color[1]) + (0.439 * rgb_color[2]) + 128; \
}

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
  char *text;
  unsigned int x;
  unsigned int y;
  int bg;
  int outline;
  unsigned char bgcolor[3]; /* YUV */
  unsigned char fgcolor[3]; /* YUV */
  FT_Library ft_lib;
  FT_Face ft_face;
} ContextInfo;


void Release(void *ctx)
{
    if (ctx)
        av_free(ctx);
}


int ParseColor(char *text, unsigned char yuv_color[3])
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

  printf("RGB=%d,%d,%d    YUV=%d,%d,%d\n",rgb_color[0],rgb_color[1],rgb_color[2],
	 yuv_color[0],yuv_color[1],yuv_color[2]);
  return 0;
}

int Configure(void **ctxp, int argc, char *argv[])
{
    int c;
    int error;
    ContextInfo *ci=NULL;
    char *font=NULL;
    unsigned int size=16;

    *ctxp = av_mallocz(sizeof(ContextInfo));
    ci = (ContextInfo *) *ctxp;

    /* configure Context Info */
    ci->text = NULL;
    ci->x = ci->y = 0;
    ci->fgcolor[0]=255;
    ci->fgcolor[1]=128;
    ci->fgcolor[2]=128;
    ci->bgcolor[0]=0;
    ci->fgcolor[1]=128;
    ci->fgcolor[2]=128;
    ci->bg = 0;
    ci->outline = 0;

    optind = 0;
    while ((c = getopt(argc, argv, "f:t:x:y:s:c:C:bo")) > 0) {
      switch (c) {
      case 'f':
	font = optarg;
	break;
      case 't':
	ci->text = av_strdup(optarg);
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
	    fprintf(stderr, "ERROR: Invalid foreground color: '%s'. You must specify the color in the internet way(packaged hex): #RRGGBB, ie: -c #ffffff (for white foreground)\n",optarg);
	    return -1;
	  }
	break;
      case 'C':
	if (ParseColor(optarg, ci->bgcolor) == -1)
	  {
	    fprintf(stderr, "ERROR: Invalid foreground color: '%s'. You must specify the color in the internet way(packaged hex): #RRGGBB, ie: -c #ffffff (for white foreground)\n",optarg);
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
	fprintf(stderr, "ERROR: Unrecognized argument '%s'\n", argv[optind]);
	return -1;
      }
    }

    if (!ci->text) 
      {
	fprintf(stderr,"ERROR: No text provided (-t text)\n");
	return -1;
      }

    if (!font)
      {
	fprintf(stderr,"ERROR: No font file provided! (-f filename)\n");
	return -1;
      }

    if ((error = FT_Init_FreeType(&(ci->ft_lib))) != 0)
      {
	fprintf(stderr,"ERROR: Could not load FreeType (error# %d)\n",error);
	return -1;
      }

    if ((error = FT_New_Face( ci->ft_lib, font, 0, &(ci->ft_face) )) != 0)
      {
	fprintf(stderr,"ERROR: Could not load face: %s  (error# %d)\n",font, error);
	return -1;
      }
    
    if ((error = FT_Set_Pixel_Sizes( ci->ft_face, 0, size)) != 0)
      {
	fprintf(stderr,"ERROR: Could not set font size to %d pixels (error# %d)\n",size, error);
	return -1;
      }
                 
    return 0;
}


inline void draw_glyph(AVPicture *picture, FT_Bitmap *bitmap, unsigned int x, unsigned int y, unsigned int width, unsigned int height, unsigned char yuv_fgcolor[3], unsigned char yuv_bgcolor[3], int outline)
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
		  else if (in_glyph) 
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


void Process(void *ctx, AVPicture *picture, enum PixelFormat pix_fmt, int width, int height, int64_t pts)
{
  ContextInfo *ci = (ContextInfo *) ctx;
  FT_Face face = ci->ft_face;
  FT_GlyphSlot  slot = face->glyph;  
  char *text = ci->text;
  int x = 0, y = 0, i=0, j=0, size=0, error;
  int str_w, str_h;
  char buff[1000];
  time_t now = time(0);

  strftime(buff, sizeof(buff), text, localtime(&now));

  text = buff;

  size = strlen(text);
  
  x = ci->x; 
  y = ci->y;


  /* measure string size */
  str_w = str_h = 0;
  for (i=0; i < size; i++)
    {
      /* load glyph image into the slot (erase previous one) */
      error = FT_Load_Char( face, text[i], FT_LOAD_RENDER | FT_LOAD_MONOCHROME );
      if (error) continue;  /* ignore errors */

      str_w += slot->advance.x >> 6;

      if (slot->bitmap_top > str_h)
	str_h = slot->bitmap_top;
      
    }


  if (ci->bg) 
    /* draw background */
    for (j = 0; (j < str_h) && (j+y < height); j++)
      for (i = 0; (i < str_w) && (i+x < width); i++) 
	{	
	  SET_PIXEL(picture, ci->bgcolor, (i+x), (y+j));
	}

  /* Draw Glyphs */
  for (i=0; i < size; i++)
    {
      /* load glyph image into the slot (erase previous one) */
      error = FT_Load_Char( face, text[i], FT_LOAD_RENDER | FT_LOAD_MONOCHROME );
      if (error) continue;  /* ignore errors */
      
      if (text[i] != '_') /* skip '_' (consider as space) */
	/* now, draw to our target surface */
	
	draw_glyph( picture, &slot->bitmap,
		    x + slot->bitmap_left,
		    y - slot->bitmap_top + str_h,
		    width, height,
		    ci->fgcolor, ci->bgcolor,
		    ci->outline);
		    
      /* increment pen position */
      x += slot->advance.x >> 6;
    }


}

