
/* pngwrite.c - general routines to write a PNG file
 *
 * libpng 1.0.15 - October 3, 2002
 * For conditions of distribution and use, see copyright notice in png.h
 * Copyright (c) 1998-2002 Glenn Randers-Pehrson
 * (Version 0.96 Copyright (c) 1996, 1997 Andreas Dilger)
 * (Version 0.88 Copyright (c) 1995, 1996 Guy Eric Schalnat, Group 42, Inc.)
 */

/* get internal access to png.h */
#define PNG_INTERNAL
#include "png.h"
#ifdef PNG_WRITE_SUPPORTED

void png_write_init(png_struct *png_ptr)
{
   png_debug(1, "in png_write_init_3\n");

   /* reset all variables to 0 */
   png_memset(png_ptr, 0, sizeof (png_struct));

   /* initialize zbuf - compression buffer */
   png_ptr->zbuf_size = PNG_ZBUF_SIZE;
   png_ptr->zbuf = (png_bytep)png_malloc(png_ptr,
      (png_uint_32)png_ptr->zbuf_size);

#if defined(PNG_WRITE_WEIGHTED_FILTER_SUPPORTED)
   png_set_filter_heuristics(png_ptr, PNG_FILTER_HEURISTIC_DEFAULT,
      1, png_doublep_NULL, png_doublep_NULL);
#endif
}

/* Writes all the PNG information.  This is the suggested way to use the
 * library.  If you have a new chunk to add, make a function to write it,
 * and put it in the correct location here.  If you want the chunk written
 * after the image data, put it in png_write_end().  I strongly encourage
 * you to supply a PNG_INFO_ flag, and check info_ptr->valid before writing
 * the chunk, as that will keep the code from breaking if you want to just
 * write a plain PNG file.  If you have long comments, I suggest writing
 * them in png_write_end(), and compressing them.
 */
void PNGAPI
png_write_info_before_PLTE(png_structp png_ptr, png_infop info_ptr)
{
   png_debug(1, "in png_write_info_before_PLTE\n");
   if (!(png_ptr->mode & PNG_WROTE_INFO_BEFORE_PLTE))
   {
   png_write_sig(png_ptr); /* write PNG signature */
#if defined(PNG_MNG_FEATURES_SUPPORTED)
   if((png_ptr->mode&PNG_HAVE_PNG_SIGNATURE)&&(png_ptr->mng_features_permitted))
   {
      png_warning(png_ptr,"MNG features are not allowed in a PNG datastream\n");
      png_ptr->mng_features_permitted=0;
   }
#endif
   /* write IHDR information. */
   png_write_IHDR(png_ptr, info_ptr->width, info_ptr->height,
      info_ptr->bit_depth, info_ptr->color_type, info_ptr->compression_type,
      info_ptr->filter_type,
#if defined(PNG_WRITE_INTERLACING_SUPPORTED)
      info_ptr->interlace_type);
#else
      0);
#endif
   /* the rest of these check to see if the valid field has the appropriate
      flag set, and if it does, writes the chunk. */
#if defined(PNG_WRITE_gAMA_SUPPORTED)
   if (info_ptr->valid & PNG_INFO_gAMA)
   {
#  ifdef PNG_FLOATING_POINT_SUPPORTED
      png_write_gAMA(png_ptr, info_ptr->gamma);
#else
#ifdef PNG_FIXED_POINT_SUPPORTED
      png_write_gAMA_fixed(png_ptr, info_ptr->int_gamma);
#  endif
#endif
   }
#endif
#if defined(PNG_WRITE_sRGB_SUPPORTED)
   if (info_ptr->valid & PNG_INFO_sRGB)
      png_write_sRGB(png_ptr, (int)info_ptr->srgb_intent);
#endif
#if defined(PNG_WRITE_iCCP_SUPPORTED)
   if (info_ptr->valid & PNG_INFO_iCCP)
      png_write_iCCP(png_ptr, info_ptr->iccp_name, PNG_COMPRESSION_TYPE_BASE,
                     info_ptr->iccp_profile, (int)info_ptr->iccp_proflen);
#endif
#if defined(PNG_WRITE_sBIT_SUPPORTED)
   if (info_ptr->valid & PNG_INFO_sBIT)
      png_write_sBIT(png_ptr, &(info_ptr->sig_bit), info_ptr->color_type);
#endif
#if defined(PNG_WRITE_cHRM_SUPPORTED)
   if (info_ptr->valid & PNG_INFO_cHRM)
   {
#ifdef PNG_FLOATING_POINT_SUPPORTED
      png_write_cHRM(png_ptr,
         info_ptr->x_white, info_ptr->y_white,
         info_ptr->x_red, info_ptr->y_red,
         info_ptr->x_green, info_ptr->y_green,
         info_ptr->x_blue, info_ptr->y_blue);
#else
#  ifdef PNG_FIXED_POINT_SUPPORTED
      png_write_cHRM_fixed(png_ptr,
         info_ptr->int_x_white, info_ptr->int_y_white,
         info_ptr->int_x_red, info_ptr->int_y_red,
         info_ptr->int_x_green, info_ptr->int_y_green,
         info_ptr->int_x_blue, info_ptr->int_y_blue);
#  endif
#endif
   }
#endif
#if defined(PNG_WRITE_UNKNOWN_CHUNKS_SUPPORTED)
   if (info_ptr->unknown_chunks_num)
   {
       png_unknown_chunk *up;

       png_debug(5, "writing extra chunks\n");

       for (up = info_ptr->unknown_chunks;
            up < info_ptr->unknown_chunks + info_ptr->unknown_chunks_num;
            up++)
       {
         int keep=png_handle_as_unknown(png_ptr, up->name);
         if (keep != HANDLE_CHUNK_NEVER &&
            up->location && (!(up->location & PNG_HAVE_PLTE)) &&
            ((up->name[3] & 0x20) || keep == HANDLE_CHUNK_ALWAYS ||
            (png_ptr->flags & PNG_FLAG_KEEP_UNSAFE_CHUNKS)))
         {
            png_write_chunk(png_ptr, up->name, up->data, up->size);
         }
       }
   }
#endif
      png_ptr->mode |= PNG_WROTE_INFO_BEFORE_PLTE;
   }
}

void PNGAPI
png_write_info(png_structp png_ptr, png_infop info_ptr)
{
#if defined(PNG_WRITE_TEXT_SUPPORTED) || defined(PNG_WRITE_sPLT_SUPPORTED)
   int i;
#endif

   png_debug(1, "in png_write_info\n");

   png_write_info_before_PLTE(png_ptr, info_ptr);

   if (info_ptr->valid & PNG_INFO_PLTE)
      png_write_PLTE(png_ptr, info_ptr->palette,
         (png_uint_32)info_ptr->num_palette);
   else if (info_ptr->color_type == PNG_COLOR_TYPE_PALETTE)
      png_error(png_ptr, "Valid palette required for paletted images\n");

#if defined(PNG_WRITE_tRNS_SUPPORTED)
   if (info_ptr->valid & PNG_INFO_tRNS)
      {
#if defined(PNG_WRITE_INVERT_ALPHA_SUPPORTED)
         /* invert the alpha channel (in tRNS) */
         if ((png_ptr->transformations & PNG_INVERT_ALPHA) &&
            info_ptr->color_type == PNG_COLOR_TYPE_PALETTE)
         {
            int j;
            for (j=0; j<(int)info_ptr->num_trans; j++)
               info_ptr->trans[j] = (png_byte)(255 - info_ptr->trans[j]);
         }
#endif
      png_write_tRNS(png_ptr, info_ptr->trans, &(info_ptr->trans_values),
         info_ptr->num_trans, info_ptr->color_type);
      }
#endif
#if defined(PNG_WRITE_bKGD_SUPPORTED)
   if (info_ptr->valid & PNG_INFO_bKGD)
      png_write_bKGD(png_ptr, &(info_ptr->background), info_ptr->color_type);
#endif
#if defined(PNG_WRITE_hIST_SUPPORTED)
   if (info_ptr->valid & PNG_INFO_hIST)
      png_write_hIST(png_ptr, info_ptr->hist, info_ptr->num_palette);
#endif
#if defined(PNG_WRITE_oFFs_SUPPORTED)
   if (info_ptr->valid & PNG_INFO_oFFs)
      png_write_oFFs(png_ptr, info_ptr->x_offset, info_ptr->y_offset,
         info_ptr->offset_unit_type);
#endif
#if defined(PNG_WRITE_pCAL_SUPPORTED)
   if (info_ptr->valid & PNG_INFO_pCAL)
      png_write_pCAL(png_ptr, info_ptr->pcal_purpose, info_ptr->pcal_X0,
         info_ptr->pcal_X1, info_ptr->pcal_type, info_ptr->pcal_nparams,
         info_ptr->pcal_units, info_ptr->pcal_params);
#endif
#if defined(PNG_WRITE_sCAL_SUPPORTED)
   if (info_ptr->valid & PNG_INFO_sCAL)
#if defined(PNG_FLOATING_POINT_SUPPORTED) && !defined(PNG_NO_STDIO)
      png_write_sCAL(png_ptr, (int)info_ptr->scal_unit,
          info_ptr->scal_pixel_width, info_ptr->scal_pixel_height);
#else
#ifdef PNG_FIXED_POINT_SUPPORTED
      png_write_sCAL_s(png_ptr, (int)info_ptr->scal_unit,
          info_ptr->scal_s_width, info_ptr->scal_s_height);
#else
      png_warning(png_ptr,
          "png_write_sCAL not supported; sCAL chunk not written.\n");
#endif
#endif
#endif
#if defined(PNG_WRITE_pHYs_SUPPORTED)
   if (info_ptr->valid & PNG_INFO_pHYs)
      png_write_pHYs(png_ptr, info_ptr->x_pixels_per_unit,
         info_ptr->y_pixels_per_unit, info_ptr->phys_unit_type);
#endif
#if defined(PNG_WRITE_tIME_SUPPORTED)
   if (info_ptr->valid & PNG_INFO_tIME)
   {
      png_write_tIME(png_ptr, &(info_ptr->mod_time));
      png_ptr->mode |= PNG_WROTE_tIME;
   }
#endif
#if defined(PNG_WRITE_sPLT_SUPPORTED)
   if (info_ptr->valid & PNG_INFO_sPLT)
     for (i = 0; i < (int)info_ptr->splt_palettes_num; i++)
       png_write_sPLT(png_ptr, info_ptr->splt_palettes + i);
#endif
#if defined(PNG_WRITE_TEXT_SUPPORTED)
   /* Check to see if we need to write text chunks */
   for (i = 0; i < info_ptr->num_text; i++)
   {
      png_debug2(2, "Writing header text chunk %d, type %d\n", i,
         info_ptr->text[i].compression);
      /* an internationalized chunk? */
      if (info_ptr->text[i].compression > 0)
      {
#if defined(PNG_WRITE_iTXt_SUPPORTED)
          /* write international chunk */
          png_write_iTXt(png_ptr,
                         info_ptr->text[i].compression,
                         info_ptr->text[i].key,
                         info_ptr->text[i].lang,
                         info_ptr->text[i].lang_key,
                         info_ptr->text[i].text);
#else
          png_warning(png_ptr, "Unable to write international text\n");
#endif
          /* Mark this chunk as written */
          info_ptr->text[i].compression = PNG_TEXT_COMPRESSION_NONE_WR;
      }
      /* If we want a compressed text chunk */
      else if (info_ptr->text[i].compression == PNG_TEXT_COMPRESSION_zTXt)
      {
#if defined(PNG_WRITE_zTXt_SUPPORTED)
         /* write compressed chunk */
         png_write_zTXt(png_ptr, info_ptr->text[i].key,
            info_ptr->text[i].text, 0,
            info_ptr->text[i].compression);
#else
         png_warning(png_ptr, "Unable to write compressed text\n");
#endif
         /* Mark this chunk as written */
         info_ptr->text[i].compression = PNG_TEXT_COMPRESSION_zTXt_WR;
      }
      else if (info_ptr->text[i].compression == PNG_TEXT_COMPRESSION_NONE)
      {
#if defined(PNG_WRITE_tEXt_SUPPORTED)
         /* write uncompressed chunk */
         png_write_tEXt(png_ptr, info_ptr->text[i].key,
                         info_ptr->text[i].text,
                         0);
#else
         png_warning(png_ptr, "Unable to write uncompressed text\n");
#endif
         /* Mark this chunk as written */
         info_ptr->text[i].compression = PNG_TEXT_COMPRESSION_NONE_WR;
      }
   }
#endif
#if defined(PNG_WRITE_UNKNOWN_CHUNKS_SUPPORTED)
   if (info_ptr->unknown_chunks_num)
   {
       png_unknown_chunk *up;

       png_debug(5, "writing extra chunks\n");

       for (up = info_ptr->unknown_chunks;
            up < info_ptr->unknown_chunks + info_ptr->unknown_chunks_num;
            up++)
       {
         int keep=png_handle_as_unknown(png_ptr, up->name);
         if (keep != HANDLE_CHUNK_NEVER &&
            up->location && (up->location & PNG_HAVE_PLTE) &&
            !(up->location & PNG_HAVE_IDAT) &&
            ((up->name[3] & 0x20) || keep == HANDLE_CHUNK_ALWAYS ||
            (png_ptr->flags & PNG_FLAG_KEEP_UNSAFE_CHUNKS)))
         {
            png_write_chunk(png_ptr, up->name, up->data, up->size);
         }
       }
   }
#endif
}

/* Writes the end of the PNG file.  If you don't want to write comments or
 * time information, you can pass NULL for info.  If you already wrote these
 * in png_write_info(), do not write them again here.  If you have long
 * comments, I suggest writing them here, and compressing them.
 */
void PNGAPI
png_write_end(png_structp png_ptr, png_infop info_ptr)
{
   png_debug(1, "in png_write_end\n");
   if (!(png_ptr->mode & PNG_HAVE_IDAT))
      png_error(png_ptr, "No IDATs written into file");

   /* see if user wants us to write information chunks */
   if (info_ptr != NULL)
   {
#if defined(PNG_WRITE_TEXT_SUPPORTED)
      int i; /* local index variable */
#endif
#if defined(PNG_WRITE_tIME_SUPPORTED)
      /* check to see if user has supplied a time chunk */
      if ((info_ptr->valid & PNG_INFO_tIME) &&
         !(png_ptr->mode & PNG_WROTE_tIME))
         png_write_tIME(png_ptr, &(info_ptr->mod_time));
#endif
#if defined(PNG_WRITE_TEXT_SUPPORTED)
      /* loop through comment chunks */
      for (i = 0; i < info_ptr->num_text; i++)
      {
         png_debug2(2, "Writing trailer text chunk %d, type %d\n", i,
            info_ptr->text[i].compression);
         /* an internationalized chunk? */
         if (info_ptr->text[i].compression > 0)
         {
#if defined(PNG_WRITE_iTXt_SUPPORTED)
             /* write international chunk */
             png_write_iTXt(png_ptr,
                         info_ptr->text[i].compression,
                         info_ptr->text[i].key,
                         info_ptr->text[i].lang,
                         info_ptr->text[i].lang_key,
                         info_ptr->text[i].text);
#else
             png_warning(png_ptr, "Unable to write international text\n");
#endif
             /* Mark this chunk as written */
             info_ptr->text[i].compression = PNG_TEXT_COMPRESSION_NONE_WR;
         }
         else if (info_ptr->text[i].compression >= PNG_TEXT_COMPRESSION_zTXt)
         {
#if defined(PNG_WRITE_zTXt_SUPPORTED)
            /* write compressed chunk */
            png_write_zTXt(png_ptr, info_ptr->text[i].key,
               info_ptr->text[i].text, 0,
               info_ptr->text[i].compression);
#else
            png_warning(png_ptr, "Unable to write compressed text\n");
#endif
            /* Mark this chunk as written */
            info_ptr->text[i].compression = PNG_TEXT_COMPRESSION_zTXt_WR;
         }
         else if (info_ptr->text[i].compression == PNG_TEXT_COMPRESSION_NONE)
         {
#if defined(PNG_WRITE_tEXt_SUPPORTED)
            /* write uncompressed chunk */
            png_write_tEXt(png_ptr, info_ptr->text[i].key,
               info_ptr->text[i].text, 0);
#else
            png_warning(png_ptr, "Unable to write uncompressed text\n");
#endif

            /* Mark this chunk as written */
            info_ptr->text[i].compression = PNG_TEXT_COMPRESSION_NONE_WR;
         }
      }
#endif
#if defined(PNG_WRITE_UNKNOWN_CHUNKS_SUPPORTED)
   if (info_ptr->unknown_chunks_num)
   {
       png_unknown_chunk *up;

       png_debug(5, "writing extra chunks\n");

       for (up = info_ptr->unknown_chunks;
            up < info_ptr->unknown_chunks + info_ptr->unknown_chunks_num;
            up++)
       {
         int keep=png_handle_as_unknown(png_ptr, up->name);
         if (keep != HANDLE_CHUNK_NEVER &&
            up->location && (up->location & PNG_AFTER_IDAT) &&
            ((up->name[3] & 0x20) || keep == HANDLE_CHUNK_ALWAYS ||
            (png_ptr->flags & PNG_FLAG_KEEP_UNSAFE_CHUNKS)))
         {
            png_write_chunk(png_ptr, up->name, up->data, up->size);
         }
       }
   }
#endif
   }

   png_ptr->mode |= PNG_AFTER_IDAT;

   /* write end of PNG file */
   png_write_IEND(png_ptr);
#if 0
/* This flush, added in libpng-1.0.8,  causes some applications to crash
   because they do not set png_ptr->output_flush_fn */
   png_flush(png_ptr);
#endif
}

#if defined(PNG_WRITE_tIME_SUPPORTED)
#if !defined(_WIN32_WCE)
/* "time.h" functions are not supported on WindowsCE */
void PNGAPI
png_convert_from_struct_tm(png_timep ptime, struct tm FAR * ttime)
{
   png_debug(1, "in png_convert_from_struct_tm\n");
   ptime->year = (png_uint_16)(1900 + ttime->tm_year);
   ptime->month = (png_byte)(ttime->tm_mon + 1);
   ptime->day = (png_byte)ttime->tm_mday;
   ptime->hour = (png_byte)ttime->tm_hour;
   ptime->minute = (png_byte)ttime->tm_min;
   ptime->second = (png_byte)ttime->tm_sec;
}

void PNGAPI
png_convert_from_time_t(png_timep ptime, time_t ttime)
{
   struct tm *tbuf;

   png_debug(1, "in png_convert_from_time_t\n");
   tbuf = gmtime(&ttime);
   png_convert_from_struct_tm(ptime, tbuf);
}
#endif
#endif

/* called by user to write a row of image data */
void PNGAPI
png_write_row(png_structp png_ptr, png_bytep row)
{
   png_debug2(1, "in png_write_row (row %ld, pass %d)\n",
      png_ptr->row_number, png_ptr->pass);
   /* initialize transformations and other stuff if first time */
   if (png_ptr->row_number == 0 && png_ptr->pass == 0)
   {
   /* make sure we wrote the header info */
   if (!(png_ptr->mode & PNG_WROTE_INFO_BEFORE_PLTE))
      png_error(png_ptr,
         "png_write_info was never called before png_write_row.");

   /* check for transforms that have been set but were defined out */
#if !defined(PNG_WRITE_INVERT_SUPPORTED) && defined(PNG_READ_INVERT_SUPPORTED)
   if (png_ptr->transformations & PNG_INVERT_MONO)
      png_warning(png_ptr, "PNG_WRITE_INVERT_SUPPORTED is not defined.");
#endif
#if !defined(PNG_WRITE_FILLER_SUPPORTED) && defined(PNG_READ_FILLER_SUPPORTED)
   if (png_ptr->transformations & PNG_FILLER)
      png_warning(png_ptr, "PNG_WRITE_FILLER_SUPPORTED is not defined.");
#endif
#if !defined(PNG_WRITE_PACKSWAP_SUPPORTED) && defined(PNG_READ_PACKSWAP_SUPPORTED)
   if (png_ptr->transformations & PNG_PACKSWAP)
      png_warning(png_ptr, "PNG_WRITE_PACKSWAP_SUPPORTED is not defined.");
#endif
#if !defined(PNG_WRITE_PACK_SUPPORTED) && defined(PNG_READ_PACK_SUPPORTED)
   if (png_ptr->transformations & PNG_PACK)
      png_warning(png_ptr, "PNG_WRITE_PACK_SUPPORTED is not defined.");
#endif
#if !defined(PNG_WRITE_SHIFT_SUPPORTED) && defined(PNG_READ_SHIFT_SUPPORTED)
   if (png_ptr->transformations & PNG_SHIFT)
      png_warning(png_ptr, "PNG_WRITE_SHIFT_SUPPORTED is not defined.");
#endif
#if !defined(PNG_WRITE_BGR_SUPPORTED) && defined(PNG_READ_BGR_SUPPORTED)
   if (png_ptr->transformations & PNG_BGR)
      png_warning(png_ptr, "PNG_WRITE_BGR_SUPPORTED is not defined.");
#endif
#if !defined(PNG_WRITE_SWAP_SUPPORTED) && defined(PNG_READ_SWAP_SUPPORTED)
   if (png_ptr->transformations & PNG_SWAP_BYTES)
      png_warning(png_ptr, "PNG_WRITE_SWAP_SUPPORTED is not defined.");
#endif

      png_write_start_row(png_ptr);
   }

#if defined(PNG_WRITE_INTERLACING_SUPPORTED)
   /* if interlaced and not interested in row, return */
   if (png_ptr->interlaced && (png_ptr->transformations & PNG_INTERLACE))
   {
      switch (png_ptr->pass)
      {
         case 0:
            if (png_ptr->row_number & 0x07)
            {
               png_write_finish_row(png_ptr);
               return;
            }
            break;
         case 1:
            if ((png_ptr->row_number & 0x07) || png_ptr->width < 5)
            {
               png_write_finish_row(png_ptr);
               return;
            }
            break;
         case 2:
            if ((png_ptr->row_number & 0x07) != 4)
            {
               png_write_finish_row(png_ptr);
               return;
            }
            break;
         case 3:
            if ((png_ptr->row_number & 0x03) || png_ptr->width < 3)
            {
               png_write_finish_row(png_ptr);
               return;
            }
            break;
         case 4:
            if ((png_ptr->row_number & 0x03) != 2)
            {
               png_write_finish_row(png_ptr);
               return;
            }
            break;
         case 5:
            if ((png_ptr->row_number & 0x01) || png_ptr->width < 2)
            {
               png_write_finish_row(png_ptr);
               return;
            }
            break;
         case 6:
            if (!(png_ptr->row_number & 0x01))
            {
               png_write_finish_row(png_ptr);
               return;
            }
            break;
      }
   }
#endif

   /* set up row info for transformations */
   png_ptr->row_info.color_type = png_ptr->color_type;
   png_ptr->row_info.width = png_ptr->usr_width;
   png_ptr->row_info.channels = png_ptr->usr_channels;
   png_ptr->row_info.bit_depth = png_ptr->usr_bit_depth;
   png_ptr->row_info.pixel_depth = (png_byte)(png_ptr->row_info.bit_depth *
      png_ptr->row_info.channels);

   png_ptr->row_info.rowbytes = ((png_ptr->row_info.width *
      (png_uint_32)png_ptr->row_info.pixel_depth + 7) >> 3);

   png_debug1(3, "row_info->color_type = %d\n", png_ptr->row_info.color_type);
   png_debug1(3, "row_info->width = %lu\n", png_ptr->row_info.width);
   png_debug1(3, "row_info->channels = %d\n", png_ptr->row_info.channels);
   png_debug1(3, "row_info->bit_depth = %d\n", png_ptr->row_info.bit_depth);
   png_debug1(3, "row_info->pixel_depth = %d\n", png_ptr->row_info.pixel_depth);
   png_debug1(3, "row_info->rowbytes = %lu\n", png_ptr->row_info.rowbytes);

   /* Copy user's row into buffer, leaving room for filter byte. */
   png_memcpy_check(png_ptr, png_ptr->row_buf + 1, row,
      png_ptr->row_info.rowbytes);

#if defined(PNG_WRITE_INTERLACING_SUPPORTED)
   /* handle interlacing */
   if (png_ptr->interlaced && png_ptr->pass < 6 &&
      (png_ptr->transformations & PNG_INTERLACE))
   {
      png_do_write_interlace(&(png_ptr->row_info),
         png_ptr->row_buf + 1, png_ptr->pass);
      /* this should always get caught above, but still ... */
      if (!(png_ptr->row_info.width))
      {
         png_write_finish_row(png_ptr);
         return;
      }
   }
#endif

#if 0
   /* handle other transformations */
   if (png_ptr->transformations)
      png_do_write_transformations(png_ptr);
#endif

#if defined(PNG_MNG_FEATURES_SUPPORTED)
   /* Write filter_method 64 (intrapixel differencing) only if
    * 1. Libpng was compiled with PNG_MNG_FEATURES_SUPPORTED and
    * 2. Libpng did not write a PNG signature (this filter_method is only
    *    used in PNG datastreams that are embedded in MNG datastreams) and
    * 3. The application called png_permit_mng_features with a mask that
    *    included PNG_FLAG_MNG_FILTER_64 and
    * 4. The filter_method is 64 and
    * 5. The color_type is RGB or RGBA
    */
   if((png_ptr->mng_features_permitted & PNG_FLAG_MNG_FILTER_64) &&
      (png_ptr->filter_type == PNG_INTRAPIXEL_DIFFERENCING))
   {
      /* Intrapixel differencing */
      png_do_write_intrapixel(&(png_ptr->row_info), png_ptr->row_buf + 1);
   }
#endif

   /* Find a filter if necessary, filter the row and write it out. */
   png_write_find_filter(png_ptr, &(png_ptr->row_info));

   if (png_ptr->write_row_fn != NULL)
      (*(png_ptr->write_row_fn))(png_ptr, png_ptr->row_number, png_ptr->pass);
}

/* Free any memory used in png_ptr struct (old method) */
void /* PRIVATE */
png_write_destroy(png_structp png_ptr)
{
#ifdef PNG_SETJMP_SUPPORTED
   jmp_buf tmp_jmp; /* save jump buffer */
#endif
#ifdef PNG_USER_MEM_SUPPORTED
   png_free_ptr free_fn;
#endif

   png_debug(1, "in png_write_destroy\n");
   /* free any memory zlib uses */
   deflateEnd(&png_ptr->zstream);

   /* free our memory.  png_free checks NULL for us. */
   png_free(png_ptr, png_ptr->zbuf);
   png_free(png_ptr, png_ptr->row_buf);
   png_free(png_ptr, png_ptr->prev_row);
   png_free(png_ptr, png_ptr->sub_row);
   png_free(png_ptr, png_ptr->up_row);
   png_free(png_ptr, png_ptr->avg_row);
   png_free(png_ptr, png_ptr->paeth_row);

#if defined(PNG_TIME_RFC1123_SUPPORTED)
   png_free(png_ptr, png_ptr->time_buffer);
#endif

#if defined(PNG_WRITE_WEIGHTED_FILTER_SUPPORTED)
   png_free(png_ptr, png_ptr->prev_filters);
   png_free(png_ptr, png_ptr->filter_weights);
   png_free(png_ptr, png_ptr->inv_filter_weights);
   png_free(png_ptr, png_ptr->filter_costs);
   png_free(png_ptr, png_ptr->inv_filter_costs);
#endif

#ifdef PNG_SETJMP_SUPPORTED
   /* reset structure */
   png_memcpy(tmp_jmp, png_ptr->jmpbuf, sizeof (jmp_buf));
#endif

#ifdef PNG_USER_MEM_SUPPORTED
   free_fn = png_ptr->free_fn;
#endif

   png_memset(png_ptr, 0, sizeof (png_struct));

#ifdef PNG_USER_MEM_SUPPORTED
   png_ptr->free_fn = free_fn;
#endif

#ifdef PNG_SETJMP_SUPPORTED
   png_memcpy(png_ptr->jmpbuf, tmp_jmp, sizeof (jmp_buf));
#endif
}

/* Allow the application to select one or more row filters to use. */
void PNGAPI
png_set_filter(png_structp png_ptr, int method, int filters)
{
   png_debug(1, "in png_set_filter\n");
#if defined(PNG_MNG_FEATURES_SUPPORTED)
   if((png_ptr->mng_features_permitted & PNG_FLAG_MNG_FILTER_64) &&
      (method == PNG_INTRAPIXEL_DIFFERENCING))
         method = PNG_FILTER_TYPE_BASE;
#endif
   if (method == PNG_FILTER_TYPE_BASE)
   {
      switch (filters & (PNG_ALL_FILTERS | 0x07))
      {
         case 5:
         case 6:
         case 7: png_warning(png_ptr, "Unknown row filter for method 0");
         case PNG_FILTER_VALUE_NONE:  png_ptr->do_filter=PNG_FILTER_NONE; break;
         case PNG_FILTER_VALUE_SUB:   png_ptr->do_filter=PNG_FILTER_SUB;  break;
         case PNG_FILTER_VALUE_UP:    png_ptr->do_filter=PNG_FILTER_UP;   break;
         case PNG_FILTER_VALUE_AVG:   png_ptr->do_filter=PNG_FILTER_AVG;  break;
         case PNG_FILTER_VALUE_PAETH: png_ptr->do_filter=PNG_FILTER_PAETH;break;
         default: png_ptr->do_filter = (png_byte)filters; break;
      }

      /* If we have allocated the row_buf, this means we have already started
       * with the image and we should have allocated all of the filter buffers
       * that have been selected.  If prev_row isn't already allocated, then
       * it is too late to start using the filters that need it, since we
       * will be missing the data in the previous row.  If an application
       * wants to start and stop using particular filters during compression,
       * it should start out with all of the filters, and then add and
       * remove them after the start of compression.
       */
      if (png_ptr->row_buf != NULL)
      {
         if ((png_ptr->do_filter & PNG_FILTER_SUB) && png_ptr->sub_row == NULL)
         {
            png_ptr->sub_row = (png_bytep)png_malloc(png_ptr,
              (png_ptr->rowbytes + 1));
            png_ptr->sub_row[0] = PNG_FILTER_VALUE_SUB;
         }

         if ((png_ptr->do_filter & PNG_FILTER_UP) && png_ptr->up_row == NULL)
         {
            if (png_ptr->prev_row == NULL)
            {
               png_warning(png_ptr, "Can't add Up filter after starting");
               png_ptr->do_filter &= ~PNG_FILTER_UP;
            }
            else
            {
               png_ptr->up_row = (png_bytep)png_malloc(png_ptr,
                  (png_ptr->rowbytes + 1));
               png_ptr->up_row[0] = PNG_FILTER_VALUE_UP;
            }
         }

         if ((png_ptr->do_filter & PNG_FILTER_AVG) && png_ptr->avg_row == NULL)
         {
            if (png_ptr->prev_row == NULL)
            {
               png_warning(png_ptr, "Can't add Average filter after starting");
               png_ptr->do_filter &= ~PNG_FILTER_AVG;
            }
            else
            {
               png_ptr->avg_row = (png_bytep)png_malloc(png_ptr,
                  (png_ptr->rowbytes + 1));
               png_ptr->avg_row[0] = PNG_FILTER_VALUE_AVG;
            }
         }

         if ((png_ptr->do_filter & PNG_FILTER_PAETH) &&
             png_ptr->paeth_row == NULL)
         {
            if (png_ptr->prev_row == NULL)
            {
               png_warning(png_ptr, "Can't add Paeth filter after starting");
               png_ptr->do_filter &= (png_byte)(~PNG_FILTER_PAETH);
            }
            else
            {
               png_ptr->paeth_row = (png_bytep)png_malloc(png_ptr,
                  (png_ptr->rowbytes + 1));
               png_ptr->paeth_row[0] = PNG_FILTER_VALUE_PAETH;
            }
         }

         if (png_ptr->do_filter == PNG_NO_FILTERS)
            png_ptr->do_filter = PNG_FILTER_NONE;
      }
   }
   else
      png_error(png_ptr, "Unknown custom filter method");
}

/* This allows us to influence the way in which libpng chooses the "best"
 * filter for the current scanline.  While the "minimum-sum-of-absolute-
 * differences metric is relatively fast and effective, there is some
 * question as to whether it can be improved upon by trying to keep the
 * filtered data going to zlib more consistent, hopefully resulting in
 * better compression.
 */
#if defined(PNG_WRITE_WEIGHTED_FILTER_SUPPORTED)      /* GRR 970116 */
void PNGAPI
png_set_filter_heuristics(png_structp png_ptr, int heuristic_method,
   int num_weights, png_doublep filter_weights,
   png_doublep filter_costs)
{
   int i;

   png_debug(1, "in png_set_filter_heuristics\n");
   if (heuristic_method >= PNG_FILTER_HEURISTIC_LAST)
   {
      png_warning(png_ptr, "Unknown filter heuristic method");
      return;
   }

   if (heuristic_method == PNG_FILTER_HEURISTIC_DEFAULT)
   {
      heuristic_method = PNG_FILTER_HEURISTIC_UNWEIGHTED;
   }

   if (num_weights < 0 || filter_weights == NULL ||
      heuristic_method == PNG_FILTER_HEURISTIC_UNWEIGHTED)
   {
      num_weights = 0;
   }

   png_ptr->num_prev_filters = (png_byte)num_weights;
   png_ptr->heuristic_method = (png_byte)heuristic_method;

   if (num_weights > 0)
   {
      if (png_ptr->prev_filters == NULL)
      {
         png_ptr->prev_filters = (png_bytep)png_malloc(png_ptr,
            (png_uint_32)(sizeof(png_byte) * num_weights));

         /* To make sure that the weighting starts out fairly */
         for (i = 0; i < num_weights; i++)
         {
            png_ptr->prev_filters[i] = 255;
         }
      }

      if (png_ptr->filter_weights == NULL)
      {
         png_ptr->filter_weights = (png_uint_16p)png_malloc(png_ptr,
            (png_uint_32)(sizeof(png_uint_16) * num_weights));

         png_ptr->inv_filter_weights = (png_uint_16p)png_malloc(png_ptr,
            (png_uint_32)(sizeof(png_uint_16) * num_weights));
         for (i = 0; i < num_weights; i++)
         {
            png_ptr->inv_filter_weights[i] =
            png_ptr->filter_weights[i] = PNG_WEIGHT_FACTOR;
         }
      }

      for (i = 0; i < num_weights; i++)
      {
         if (filter_weights[i] < 0.0)
         {
            png_ptr->inv_filter_weights[i] =
            png_ptr->filter_weights[i] = PNG_WEIGHT_FACTOR;
         }
         else
         {
            png_ptr->inv_filter_weights[i] =
               (png_uint_16)((double)PNG_WEIGHT_FACTOR*filter_weights[i]+0.5);
            png_ptr->filter_weights[i] =
               (png_uint_16)((double)PNG_WEIGHT_FACTOR/filter_weights[i]+0.5);
         }
      }
   }

   /* If, in the future, there are other filter methods, this would
    * need to be based on png_ptr->filter.
    */
   if (png_ptr->filter_costs == NULL)
   {
      png_ptr->filter_costs = (png_uint_16p)png_malloc(png_ptr,
         (png_uint_32)(sizeof(png_uint_16) * PNG_FILTER_VALUE_LAST));

      png_ptr->inv_filter_costs = (png_uint_16p)png_malloc(png_ptr,
         (png_uint_32)(sizeof(png_uint_16) * PNG_FILTER_VALUE_LAST));

      for (i = 0; i < PNG_FILTER_VALUE_LAST; i++)
      {
         png_ptr->inv_filter_costs[i] =
         png_ptr->filter_costs[i] = PNG_COST_FACTOR;
      }
   }

   /* Here is where we set the relative costs of the different filters.  We
    * should take the desired compression level into account when setting
    * the costs, so that Paeth, for instance, has a high relative cost at low
    * compression levels, while it has a lower relative cost at higher
    * compression settings.  The filter types are in order of increasing
    * relative cost, so it would be possible to do this with an algorithm.
    */
   for (i = 0; i < PNG_FILTER_VALUE_LAST; i++)
   {
      if (filter_costs == NULL || filter_costs[i] < 0.0)
      {
         png_ptr->inv_filter_costs[i] =
         png_ptr->filter_costs[i] = PNG_COST_FACTOR;
      }
      else if (filter_costs[i] >= 1.0)
      {
         png_ptr->inv_filter_costs[i] =
            (png_uint_16)((double)PNG_COST_FACTOR / filter_costs[i] + 0.5);
         png_ptr->filter_costs[i] =
            (png_uint_16)((double)PNG_COST_FACTOR * filter_costs[i] + 0.5);
      }
   }
}
#endif /* PNG_WRITE_WEIGHTED_FILTER_SUPPORTED */

void PNGAPI
png_set_compression_level(png_structp png_ptr, int level)
{
   png_debug(1, "in png_set_compression_level\n");
   png_ptr->flags |= PNG_FLAG_ZLIB_CUSTOM_LEVEL;
   png_ptr->zlib_level = level;
}

void PNGAPI
png_set_compression_mem_level(png_structp png_ptr, int mem_level)
{
   png_debug(1, "in png_set_compression_mem_level\n");
   png_ptr->flags |= PNG_FLAG_ZLIB_CUSTOM_MEM_LEVEL;
   png_ptr->zlib_mem_level = mem_level;
}

void PNGAPI
png_set_compression_strategy(png_structp png_ptr, int strategy)
{
   png_debug(1, "in png_set_compression_strategy\n");
   png_ptr->flags |= PNG_FLAG_ZLIB_CUSTOM_STRATEGY;
   png_ptr->zlib_strategy = strategy;
}

void PNGAPI
png_set_compression_window_bits(png_structp png_ptr, int window_bits)
{
   if (window_bits > 15)
      png_warning(png_ptr, "Only compression windows <= 32k supported by PNG");
   else if (window_bits < 8)
      png_warning(png_ptr, "Only compression windows >= 256 supported by PNG");
#ifndef WBITS_8_OK
   /* avoid libpng bug with 256-byte windows */
   if (window_bits == 8)
     {
       png_warning(png_ptr, "Compression window is being reset to 512");
       window_bits=9;
     }
#endif
   png_ptr->flags |= PNG_FLAG_ZLIB_CUSTOM_WINDOW_BITS;
   png_ptr->zlib_window_bits = window_bits;
}

void PNGAPI
png_set_compression_method(png_structp png_ptr, int method)
{
   png_debug(1, "in png_set_compression_method\n");
   if (method != 8)
      png_warning(png_ptr, "Only compression method 8 is supported by PNG");
   png_ptr->flags |= PNG_FLAG_ZLIB_CUSTOM_METHOD;
   png_ptr->zlib_method = method;
}

#endif /* PNG_WRITE_SUPPORTED */
