
/* png.c - location for general purpose libpng functions
 *
 * libpng version 1.0.15 - October 3, 2002
 * Copyright (c) 1998-2002 Glenn Randers-Pehrson
 * (Version 0.96 Copyright (c) 1996, 1997 Andreas Dilger)
 * (Version 0.88 Copyright (c) 1995, 1996 Guy Eric Schalnat, Group 42, Inc.)
 *
 */

#define PNG_INTERNAL
#define PNG_NO_EXTERN
#include "png.h"

/* Generate a compiler error if there is an old png.h in the search path. */
typedef version_1_0_15 Your_png_h_is_not_version_1_0_15;

/* Version information for C files.  This had better match the version
 * string defined in png.h.  */

#ifdef PNG_USE_GLOBAL_ARRAYS
/* png_libpng_ver was changed to a function in version 1.0.5c */
const char png_libpng_ver[18] = "1.0.15";

/* png_sig was changed to a function in version 1.0.5c */
/* Place to hold the signature string for a PNG file. */
const png_byte FARDATA png_sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};

/* Invoke global declarations for constant strings for known chunk types */
PNG_IHDR;
PNG_IDAT;
PNG_IEND;
PNG_PLTE;
PNG_bKGD;
PNG_cHRM;
PNG_gAMA;
PNG_hIST;
PNG_iCCP;
PNG_iTXt;
PNG_oFFs;
PNG_pCAL;
PNG_sCAL;
PNG_pHYs;
PNG_sBIT;
PNG_sPLT;
PNG_sRGB;
PNG_tEXt;
PNG_tIME;
PNG_tRNS;
PNG_zTXt;

/* arrays to facilitate easy interlacing - use pass (0 - 6) as index */

/* start of interlace block */
const int FARDATA png_pass_start[] = {0, 4, 0, 2, 0, 1, 0};

/* offset to next interlace block */
const int FARDATA png_pass_inc[] = {8, 8, 4, 4, 2, 2, 1};

/* start of interlace block in the y direction */
const int FARDATA png_pass_ystart[] = {0, 0, 4, 0, 2, 0, 1};

/* offset to next interlace block in the y direction */
const int FARDATA png_pass_yinc[] = {8, 8, 8, 4, 4, 2, 2};

/* width of interlace block (used in assembler routines only) */
#ifdef PNG_HAVE_ASSEMBLER_COMBINE_ROW
const int FARDATA png_pass_width[] = {8, 4, 4, 2, 2, 1, 1};
#endif

/* Height of interlace block.  This is not currently used - if you need
 * it, uncomment it here and in png.h
const int FARDATA png_pass_height[] = {8, 8, 4, 4, 2, 2, 1};
*/

/* Mask to determine which pixels are valid in a pass */
const int FARDATA png_pass_mask[] = {0x80, 0x08, 0x88, 0x22, 0xaa, 0x55, 0xff};

/* Mask to determine which pixels to overwrite while displaying */
const int FARDATA png_pass_dsp_mask[]
   = {0xff, 0x0f, 0xff, 0x33, 0xff, 0x55, 0xff};

#endif

/* Checks whether the supplied bytes match the PNG signature.  We allow
 * checking less than the full 8-byte signature so that those apps that
 * already read the first few bytes of a file to determine the file type
 * can simply check the remaining bytes for extra assurance.  Returns
 * an integer less than, equal to, or greater than zero if sig is found,
 * respectively, to be less than, to match, or be greater than the correct
 * PNG signature (this is the same behaviour as strcmp, memcmp, etc).
 */
int PNGAPI
png_sig_cmp(png_bytep sig, png_size_t start, png_size_t num_to_check)
{
   png_byte png_signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
   if (num_to_check > 8)
      num_to_check = 8;
   else if (num_to_check < 1)
      return (0);

   if (start > 7)
      return (0);

   if (start + num_to_check > 8)
      num_to_check = 8 - start;

   return ((int)(png_memcmp(&sig[start], &png_signature[start], num_to_check)));
}

/* Function to allocate memory for zlib and clear it to 0. */
#ifdef PNG_1_0_X
voidpf PNGAPI
#else
voidpf /* private */
#endif
png_zalloc(voidpf png_ptr, uInt items, uInt size)
{
   png_uint_32 num_bytes = (png_uint_32)items * size;
   png_voidp ptr;
   png_structp p=png_ptr;
   png_uint_32 save_flags=p->flags;

   p->flags|=PNG_FLAG_MALLOC_NULL_MEM_OK;
   ptr = (png_voidp)png_malloc((png_structp)png_ptr, num_bytes);
   p->flags=save_flags;

#ifndef PNG_NO_ZALLOC_ZERO
   if (ptr == NULL)
       return ((voidpf)ptr);

   if (num_bytes > (png_uint_32)0x8000L)
   {
      png_memset(ptr, 0, (png_size_t)0x8000L);
      png_memset((png_bytep)ptr + (png_size_t)0x8000L, 0,
         (png_size_t)(num_bytes - (png_uint_32)0x8000L));
   }
   else
   {
      png_memset(ptr, 0, (png_size_t)num_bytes);
   }
#endif
   return ((voidpf)ptr);
}

/* function to free memory for zlib */
#ifdef PNG_1_0_X
void PNGAPI
#else
void /* private */
#endif
png_zfree(voidpf png_ptr, voidpf ptr)
{
   png_free((png_structp)png_ptr, (png_voidp)ptr);
}

/* Reset the CRC variable to 32 bits of 1's.  Care must be taken
 * in case CRC is > 32 bits to leave the top bits 0.
 */
void /* PRIVATE */
png_reset_crc(png_structp png_ptr)
{
   png_ptr->crc = crc32(0, Z_NULL, 0);
}

/* Calculate the CRC over a section of data.  We can only pass as
 * much data to this routine as the largest single buffer size.  We
 * also check that this data will actually be used before going to the
 * trouble of calculating it.
 */
void /* PRIVATE */
png_calculate_crc(png_structp png_ptr, png_bytep ptr, png_size_t length)
{
   int need_crc = 1;

   if (png_ptr->chunk_name[0] & 0x20)                     /* ancillary */
   {
      if ((png_ptr->flags & PNG_FLAG_CRC_ANCILLARY_MASK) ==
          (PNG_FLAG_CRC_ANCILLARY_USE | PNG_FLAG_CRC_ANCILLARY_NOWARN))
         need_crc = 0;
   }
   else                                                    /* critical */
   {
      if (png_ptr->flags & PNG_FLAG_CRC_CRITICAL_IGNORE)
         need_crc = 0;
   }

   if (need_crc)
      png_ptr->crc = crc32(png_ptr->crc, ptr, (uInt)length);
}

void PNGAPI
png_free_data(png_structp png_ptr, png_infop info_ptr, png_uint_32 mask,
   int num)
{
   png_debug(1, "in png_free_data\n");
   if (png_ptr == NULL || info_ptr == NULL)
      return;

#if defined(PNG_TEXT_SUPPORTED)
/* free text item num or (if num == -1) all text items */
#ifdef PNG_FREE_ME_SUPPORTED
if ((mask & PNG_FREE_TEXT) & info_ptr->free_me)
#else
if (mask & PNG_FREE_TEXT)
#endif
{
   if (num != -1)
   {
     if (info_ptr->text && info_ptr->text[num].key)
     {
         png_free(png_ptr, info_ptr->text[num].key);
         info_ptr->text[num].key = NULL;
     }
   }
   else
   {
       int i;
       for (i = 0; i < info_ptr->num_text; i++)
           png_free_data(png_ptr, info_ptr, PNG_FREE_TEXT, i);
       png_free(png_ptr, info_ptr->text);
       info_ptr->text = NULL;
       info_ptr->num_text=0;
   }
}
#endif

#if defined(PNG_tRNS_SUPPORTED)
/* free any tRNS entry */
#ifdef PNG_FREE_ME_SUPPORTED
if ((mask & PNG_FREE_TRNS) & info_ptr->free_me)
#else
if ((mask & PNG_FREE_TRNS) && (png_ptr->flags & PNG_FLAG_FREE_TRNS))
#endif
{
    png_free(png_ptr, info_ptr->trans);
    info_ptr->valid &= ~PNG_INFO_tRNS;
#ifndef PNG_FREE_ME_SUPPORTED
    png_ptr->flags &= ~PNG_FLAG_FREE_TRNS;
#endif
    info_ptr->trans = NULL;
}
#endif

#if defined(PNG_sCAL_SUPPORTED)
/* free any sCAL entry */
#ifdef PNG_FREE_ME_SUPPORTED
if ((mask & PNG_FREE_SCAL) & info_ptr->free_me)
#else
if (mask & PNG_FREE_SCAL)
#endif
{
#if defined(PNG_FIXED_POINT_SUPPORTED) && !defined(PNG_FLOATING_POINT_SUPPORTED)
    png_free(png_ptr, info_ptr->scal_s_width);
    png_free(png_ptr, info_ptr->scal_s_height);
    info_ptr->scal_s_width = NULL;
    info_ptr->scal_s_height = NULL;
#endif
    info_ptr->valid &= ~PNG_INFO_sCAL;
}
#endif

#if defined(PNG_pCAL_SUPPORTED)
/* free any pCAL entry */
#ifdef PNG_FREE_ME_SUPPORTED
if ((mask & PNG_FREE_PCAL) & info_ptr->free_me)
#else
if (mask & PNG_FREE_PCAL)
#endif
{
    png_free(png_ptr, info_ptr->pcal_purpose);
    png_free(png_ptr, info_ptr->pcal_units);
    info_ptr->pcal_purpose = NULL;
    info_ptr->pcal_units = NULL;
    if (info_ptr->pcal_params != NULL)
    {
        int i;
        for (i = 0; i < (int)info_ptr->pcal_nparams; i++)
        {
          png_free(png_ptr, info_ptr->pcal_params[i]);
          info_ptr->pcal_params[i]=NULL;
        }
        png_free(png_ptr, info_ptr->pcal_params);
        info_ptr->pcal_params = NULL;
    }
    info_ptr->valid &= ~PNG_INFO_pCAL;
}
#endif

#if defined(PNG_iCCP_SUPPORTED)
/* free any iCCP entry */
#ifdef PNG_FREE_ME_SUPPORTED
if ((mask & PNG_FREE_ICCP) & info_ptr->free_me)
#else
if (mask & PNG_FREE_ICCP)
#endif
{
    png_free(png_ptr, info_ptr->iccp_name);
    png_free(png_ptr, info_ptr->iccp_profile);
    info_ptr->iccp_name = NULL;
    info_ptr->iccp_profile = NULL;
    info_ptr->valid &= ~PNG_INFO_iCCP;
}
#endif

#if defined(PNG_sPLT_SUPPORTED)
/* free a given sPLT entry, or (if num == -1) all sPLT entries */
#ifdef PNG_FREE_ME_SUPPORTED
if ((mask & PNG_FREE_SPLT) & info_ptr->free_me)
#else
if (mask & PNG_FREE_SPLT)
#endif
{
   if (num != -1)
   {
      if(info_ptr->splt_palettes)
      {
          png_free(png_ptr, info_ptr->splt_palettes[num].name);
          png_free(png_ptr, info_ptr->splt_palettes[num].entries);
          info_ptr->splt_palettes[num].name = NULL;
          info_ptr->splt_palettes[num].entries = NULL;
      }
   }
   else
   {
       if(info_ptr->splt_palettes_num)
       {
         int i;
         for (i = 0; i < (int)info_ptr->splt_palettes_num; i++)
            png_free_data(png_ptr, info_ptr, PNG_FREE_SPLT, i);

         png_free(png_ptr, info_ptr->splt_palettes);
         info_ptr->splt_palettes = NULL;
         info_ptr->splt_palettes_num = 0;
       }
       info_ptr->valid &= ~PNG_INFO_sPLT;
   }
}
#endif

#if defined(PNG_UNKNOWN_CHUNKS_SUPPORTED)
#ifdef PNG_FREE_ME_SUPPORTED
if ((mask & PNG_FREE_UNKN) & info_ptr->free_me)
#else
if (mask & PNG_FREE_UNKN)
#endif
{
   if (num != -1)
   {
       if(info_ptr->unknown_chunks)
       {
          png_free(png_ptr, info_ptr->unknown_chunks[num].data);
          info_ptr->unknown_chunks[num].data = NULL;
       }
   }
   else
   {
       int i;

       if(info_ptr->unknown_chunks_num)
       {
         for (i = 0; i < (int)info_ptr->unknown_chunks_num; i++)
            png_free_data(png_ptr, info_ptr, PNG_FREE_UNKN, i);

         png_free(png_ptr, info_ptr->unknown_chunks);
         info_ptr->unknown_chunks = NULL;
         info_ptr->unknown_chunks_num = 0;
       }
   }
}
#endif

#if defined(PNG_hIST_SUPPORTED)
/* free any hIST entry */
#ifdef PNG_FREE_ME_SUPPORTED
if ((mask & PNG_FREE_HIST)  & info_ptr->free_me)
#else
if ((mask & PNG_FREE_HIST) && (png_ptr->flags & PNG_FLAG_FREE_HIST))
#endif
{
    png_free(png_ptr, info_ptr->hist);
    info_ptr->hist = NULL;
    info_ptr->valid &= ~PNG_INFO_hIST;
#ifndef PNG_FREE_ME_SUPPORTED
    png_ptr->flags &= ~PNG_FLAG_FREE_HIST;
#endif
}
#endif

/* free any PLTE entry that was internally allocated */
#ifdef PNG_FREE_ME_SUPPORTED
if ((mask & PNG_FREE_PLTE) & info_ptr->free_me)
#else
if ((mask & PNG_FREE_PLTE) && (png_ptr->flags & PNG_FLAG_FREE_PLTE))
#endif
{
    png_zfree(png_ptr, info_ptr->palette);
    info_ptr->palette = NULL;
    info_ptr->valid &= ~PNG_INFO_PLTE;
#ifndef PNG_FREE_ME_SUPPORTED
    png_ptr->flags &= ~PNG_FLAG_FREE_PLTE;
#endif
    info_ptr->num_palette = 0;
}

#if defined(PNG_INFO_IMAGE_SUPPORTED)
/* free any image bits attached to the info structure */
#ifdef PNG_FREE_ME_SUPPORTED
if ((mask & PNG_FREE_ROWS) & info_ptr->free_me)
#else
if (mask & PNG_FREE_ROWS)
#endif
{
    if(info_ptr->row_pointers)
    {
       int row;
       for (row = 0; row < (int)info_ptr->height; row++)
       {
          png_free(png_ptr, info_ptr->row_pointers[row]);
          info_ptr->row_pointers[row]=NULL;
       }
       png_free(png_ptr, info_ptr->row_pointers);
       info_ptr->row_pointers=NULL;
    }
    info_ptr->valid &= ~PNG_INFO_IDAT;
}
#endif

#ifdef PNG_FREE_ME_SUPPORTED
   if(num == -1)
     info_ptr->free_me &= ~mask;
   else
     info_ptr->free_me &= ~(mask & ~PNG_FREE_MUL);
#endif
}

#ifdef PNG_HANDLE_AS_UNKNOWN_SUPPORTED
int PNGAPI
png_handle_as_unknown(png_structp png_ptr, png_bytep chunk_name)
{
   /* check chunk_name and return "keep" value if it's on the list, else 0 */
   int i;
   png_bytep p;
   if((png_ptr == NULL && chunk_name == NULL) || png_ptr->num_chunk_list<=0)
      return 0;
   p=png_ptr->chunk_list+png_ptr->num_chunk_list*5-5;
   for (i = png_ptr->num_chunk_list; i; i--, p-=5)
      if (!png_memcmp(chunk_name, p, 4))
        return ((int)*(p+4));
   return 0;
}
#endif

void png_info_init(png_info *info_ptr)
{
    memset(info_ptr, 0, sizeof(png_info));
}


