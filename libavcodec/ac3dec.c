/* AC3 Audio Decoder.
 *
 * Copyright (c) 2006 Kartikey Mahendra BHATT (bhattkm at gmail dot com).
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdio.h>
#include <stddef.h>
#include <math.h>
#include <inttypes.h>
#include <string.h>

#define ALT_BITSTREAM_READER
#include "ac3_decoder.h"
#include "avcodec.h"
#include "bitstream.h"
#include "dsputil.h"
#include "avutil.h"

static const int sampling_rates[3] = { 32000, 44100, 48000 };

static const struct
{
  int bit_rate;
  int frame_sizes[3];
} frame_size_table[38] = {
  { 32, { 96, 69, 64 } },
  { 32, { 96, 70, 64 } },
  { 40, { 120, 87, 80 } },
  { 40, { 120, 88, 80 } },
  { 48, { 144, 104, 96 } },
  { 48, { 144, 105, 96 } },
  { 56, { 168, 121, 112 } },
  { 56, { 168, 122, 112 } },
  { 64, { 192, 139, 128 } },
  { 64, { 192, 140, 128 } },
  { 80, { 240, 174, 160 } },
  { 80, { 240, 175, 160 } },
  { 96, { 288, 208, 192 } },
  { 96, { 288, 209, 192 } },
  { 112, { 336, 243, 224 } },
  { 112, { 336, 244, 224 } },
  { 128, { 384, 278, 256 } },
  { 128, { 384, 279, 256 } },
  { 160, { 480, 348, 320 } },
  { 160, { 480, 349, 320 } },
  { 192, { 576, 417, 384 } },
  { 192, { 576, 418, 384 } },
  { 224, { 672, 487, 448 } },
  { 224, { 672, 488, 448 } },
  { 256, { 768, 557, 512 } },
  { 256, { 768, 558, 512 } },
  { 320, { 960, 696, 640 } },
  { 320, { 960, 697, 640 } },
  { 384, { 1152, 835, 768 } },
  { 384, { 1152, 836, 768 } },
  { 448, { 1344, 975, 896 } },
  { 448, { 1344, 976, 896 } },
  { 512, { 1536, 1114, 1024 } },
  { 512, { 1536, 1115, 1024 } },
  { 576, { 1728, 1253, 1152 } },
  { 576, { 1728, 1254, 1152 } },
  { 640, { 1920, 1393, 1280 } }
};

static int
ac3_decode_init (AVCodecContext * avctx)
{
  AC3DecodeContext *ctx = avctx->priv_data;

  ff_mdct_init (&ctx->mdct_ctx_256, 8, 1);
  ff_mdct_init (&ctx->mdct_ctx_512, 9, 1);
  ctx->samples = av_mallocz (6 * 6 * 256 * sizeof (float));
  if (!(ctx->samples))
    return -1;

  return 0;
}

static int
ac3_synchronize (uint8_t * buf, int buf_size)
{
  int i;

  for (i = 0; i < buf_size - 1; i++)
    if (buf[i] == 0x0b && buf[i + 1] == 0x77)
      return i;

  return -1;
}

//Returns -1 when 'fscod' is not valid;
static int
ac3_parse_sync_info (AC3DecodeContext * ctx)
{
  ac3_sync_info *sync_info = &ctx->sync_info;
  GetBitContext *gb = &ctx->gb;

  sync_info->sync_word = get_bits_long (gb, 16);
  sync_info->crc1 = get_bits_long (gb, 16);
  sync_info->fscod = get_bits_long (gb, 2);
  if (sync_info->fscod == 0x03)
    return -1;
  sync_info->frmsizecod = get_bits_long (gb, 6);
  if (sync_info->frmsizecod >= 0x38)
    return -1;
  sync_info->sampling_rate = sampling_rates[sync_info->fscod];
  sync_info->bit_rate = frame_size_table[sync_info->frmsizecod].bit_rate;
  sync_info->frame_size = frame_size_table[sync_info->frmsizecod].frame_sizes[sync_info->fscod];

  return 0;
}

static const int nfchans_tbl[8] = { 2, 1, 2, 3, 3, 4, 4, 5 };

//Returns -1 when
static int
ac3_parse_bsi (AC3DecodeContext * ctx)
{
  ac3_bsi *bsi = &ctx->bsi;
  uint32_t *flags = &bsi->flags;
  GetBitContext *gb = &ctx->gb;

  *flags = 0;
  bsi->cmixlev = 0;
  bsi->surmixlev = 0;
  bsi->dsurmod = 0;

  bsi->bsid = get_bits_long (gb, 5);
  if (bsi->bsid > 0x08)
    return -1;
  bsi->bsmod = get_bits_long (gb, 3);
  bsi->acmod = get_bits_long (gb, 3);
  if (bsi->acmod & 0x01 && bsi->acmod != 0x01)
    bsi->cmixlev = get_bits_long (gb, 2);
  if (bsi->acmod & 0x04)
    bsi->surmixlev = get_bits_long (gb, 2);
  if (bsi->acmod == 0x02)
    bsi->dsurmod = get_bits_long (gb, 2);
  if (get_bits_long (gb, 1))
    *flags |= AC3_BSI_LFEON;
  bsi->dialnorm = get_bits_long (gb, 5);
  if (get_bits_long (gb, 1)) {
    *flags |= AC3_BSI_COMPRE;
    bsi->compr = get_bits_long (gb, 5);
  }
  if (get_bits_long (gb, 1)) {
    *flags |= AC3_BSI_LANGCODE;
    bsi->langcod = get_bits_long (gb, 8);
  }
  if (get_bits_long (gb, 1)) {
    *flags |= AC3_BSI_AUDPRODIE;
    bsi->mixlevel = get_bits_long (gb, 5);
    bsi->roomtyp = get_bits_long (gb, 2);
  }
  if (bsi->acmod == 0x00) {
    bsi->dialnorm2 = get_bits_long (gb, 5);
    if (get_bits_long (gb, 1)) {
      *flags |= AC3_BSI_COMPR2E;
      bsi->compr2 = get_bits_long (gb, 5);
    }
    if (get_bits_long (gb, 1)) {
      *flags |= AC3_BSI_LANGCOD2E;
      bsi->langcod2 = get_bits_long (gb, 8);
    }
    if (get_bits_long (gb, 1)) {
      *flags |= AC3_BSI_AUDPRODIE;
      bsi->mixlevel2 = get_bits_long (gb, 5);
      bsi->roomtyp2 = get_bits_long (gb, 2);
    }
  }
  if (get_bits_long (gb, 1))
    *flags |= AC3_BSI_COPYRIGHTB;
  if (get_bits_long (gb, 1))
    *flags |= AC3_BSI_ORIGBS;
  if (get_bits_long (gb, 1)) {
    *flags |= AC3_BSI_TIMECOD1E;
    bsi->timecod1 = get_bits_long (gb, 14);
  }
  if (get_bits_long (gb, 1)) {
    *flags |= AC3_BSI_TIMECOD2E;
    bsi->timecod2 = get_bits_long (gb, 14);
  }
  if (get_bits_long (gb, 1)) {
    *flags |= AC3_BSI_ADDBSIE;
    bsi->addbsil = get_bits_long (gb, 6);
    do {
      get_bits_long (gb, 8);
    } while (bsi->addbsil--);
  }

  bsi->nfchans = nfchans_tbl[bsi->acmod];
  return 0;
}

static int bands[16] =
{ 31, 35, 37, 39, 41, 42, 43, 44,
  45, 45, 46, 46, 47, 47, 48, 48 };

static const int diff_exps_M1[128] =
  { -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
     2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
     25, 25, 25 };

static const int diff_exps_M2[128] =
  { -2, -2, -2, -2, -2, -1, -1, -1, -1, -1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2,
    -2, -2, -2, -2, -2, -1, -1, -1, -1, -1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2,
    -2, -2, -2, -2, -2, -1, -1, -1, -1, -1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2,
    -2, -2, -2, -2, -2, -1, -1, -1, -1, -1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2,
    -2, -2, -2, -2, -2, -1, -1, -1, -1, -1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2,
    25, 25, 25 };

static const int diff_exps_M3[128] =
  { -2, -1, 0, 1, 2, -2, -1, 0, 1, 2, -2, -1, 0, 1, 2, -2, -1, 0, 1, 2, -2, -1, 0, 1, 2,
    -2, -1, 0, 1, 2, -2, -1, 0, 1, 2, -2, -1, 0, 1, 2, -2, -1, 0, 1, 2, -2, -1, 0, 1, 2,
    -2, -1, 0, 1, 2, -2, -1, 0, 1, 2, -2, -1, 0, 1, 2, -2, -1, 0, 1, 2, -2, -1, 0, 1, 2,
    -2, -1, 0, 1, 2, -2, -1, 0, 1, 2, -2, -1, 0, 1, 2, -2, -1, 0, 1, 2, -2, -1, 0, 1, 2,
    -2, -1, 0, 1, 2, -2, -1, 0, 1, 2, -2, -1, 0, 1, 2, -2, -1, 0, 1, 2, -2, -1, 0, 1, 2,
    25, 25, 25 };

/* Decodes the grouped exponents (gexps) and stores them
 * in decoded exponents (dexps).
 */
static int
_decode_exponents (int expstr, int ngrps, uint8_t absexp, uint8_t * gexps, uint8_t * dexps)
{
  int i = 0, exp;
  while (ngrps--) {
    exp = gexps[i++];

    absexp += diff_exps_M1[exp];
    if (absexp > 24)
      return -1;
    if (expstr == AC3_EXPSTR_D45) {
      *(dexps++) = absexp;
      *(dexps++) = absexp;
    }
    else if (expstr == AC3_EXPSTR_D25)
      *(dexps++) = absexp;
    else
      *(dexps++) = absexp;

    absexp += diff_exps_M2[exp];
    if (absexp > 24)
      return -1;
    if (expstr == AC3_EXPSTR_D45) {
      *(dexps++) = absexp;
      *(dexps++) = absexp;
    }
    else if (expstr == AC3_EXPSTR_D25)
      *(dexps++) = absexp;
    else
      *(dexps++) = absexp;

    absexp += diff_exps_M3[exp];
    if (absexp > 24)
      return -1;
    if (expstr == AC3_EXPSTR_D45) {
      *(dexps++) = absexp;
      *(dexps++) = absexp;
    }
    else if (expstr == AC3_EXPSTR_D25)
      *(dexps++) = absexp;
    else
      *(dexps++) = absexp;
  }

  return 0;
}

static int
decode_exponents (AC3DecodeContext * ctx)
{
  ac3_audio_block *ab = &ctx->audio_block;
  int i;
  uint8_t *exps;
  uint8_t *dexps;

  if (ab->flags & AC3_AB_CPLINU && ab->cplexpstr != AC3_EXPSTR_REUSE)
    if (_decode_exponents (ab->cplexpstr, ab->ncplgrps, ab->cplabsexp,
                           ab->cplexps, ab->dcplexps + ab->cplstrtmant))
      return -1;
  for (i = 0; i < ctx->bsi.nfchans; i++)
    if (ab->chexpstr[i] != AC3_EXPSTR_REUSE) {
      exps = ab->exps[i];
      dexps = ab->dexps[i];
      if (_decode_exponents (ab->chexpstr[i], ab->nchgrps[i], exps[0], exps + 1, dexps + 1))
        return -1;
    }
  if (ctx->bsi.flags & AC3_BSI_LFEON && ab->lfeexpstr != AC3_EXPSTR_REUSE)
    if (_decode_exponents (ab->lfeexpstr, 2, ab->lfeexps[0], ab->lfeexps + 1, ab->dlfeexps))
      return -1;
  return 0;
}

static const int16_t slowdec[4] = { 0x0f, 0x11, 0x13, 0x15 }; /* slow decay table */
static const int16_t fastdec[4] = { 0x3f, 0x53, 0x67, 0x7b }; /* fast decay table */
static const int16_t slowgain[4] = { 0x540, 0x4d8, 0x478, 0x410 };   /* slow gain table */
static const int16_t dbpbtab[4] = { 0x000, 0x700, 0x900, 0xb00 };    /* dB/bit table */

static const int16_t floortab[8] =  /* floor table */
{ 0x2f0, 0x2b0, 0x270, 0x230,
  0x1f0, 0x170, 0x0f0, 0xf800 };

static const int16_t fastgain[8] = /* fast gain table */
{ 0x080, 0x100, 0x180, 0x200,
  0x280, 0x300, 0x380, 0x400 };

static const int16_t bndtab[50] = /* start band table */
{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
  27, 28, 31, 34, 37, 40, 43, 46, 49, 55, 61, 67, 73, 79, 85, 97, 109, 121, 133, 157, 181, 205, 229 };

static const int16_t bndsz[50] = /* band size table */
{ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 3, 3, 3, 3, 3, 3, 3, 6, 6, 6, 6, 6, 6, 12, 12, 12, 12, 24, 24, 24, 24, 24 };

static const int16_t masktab[256] = /* masking table */
{  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
  25, 26, 27, 28, 28, 28, 29, 29, 29, 30, 30, 30, 31, 31, 31, 32, 32, 32, 33, 33, 33, 34, 34, 34, 35,
  35, 35, 35, 35, 35, 36, 36, 36, 36, 36, 36, 37, 37, 37, 37, 37, 37, 38, 38, 38, 38, 38, 38, 39, 39,
  39, 39, 39, 39, 40, 40, 40, 40, 40, 40, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 42, 42, 42,
  42, 42, 42, 42, 42, 42, 42, 42, 42, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 44, 44, 44, 44,
  44, 44, 44, 44, 44, 44, 44, 44, 45, 45, 45, 45, 45, 45, 45, 45, 45, 45, 45, 45, 45, 45, 45, 45, 45,
  45, 45, 45, 45, 45, 45, 45, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46,
  46, 46, 46, 46, 46, 46, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47,
  47, 47, 47, 47, 47, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48,
  48, 48, 48, 48, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49,
  49, 49, 49, 0, 0, 0 };

static const int16_t latab[256] = /* log addition table */
{ 0x0040, 0x003f, 0x003e, 0x003d, 0x003c, 0x003b, 0x003a, 0x0039, 0x0038, 0x0037, 0x0036, 0x0035,
  0x0034, 0x0034, 0x0033, 0x0032, 0x0031, 0x0030, 0x002f, 0x002f, 0x002e, 0x002d, 0x002c, 0x002c,
  0x002b, 0x002a, 0x0029, 0x0029, 0x0028, 0x0027, 0x0026, 0x0026, 0x0025, 0x0024, 0x0024, 0x0023,
  0x0023, 0x0022, 0x0021, 0x0021, 0x0020, 0x0020, 0x001f, 0x001e, 0x001e, 0x001d, 0x001d, 0x001c,
  0x001c, 0x001b, 0x001b, 0x001a, 0x001a, 0x0019, 0x0019, 0x0018, 0x0018, 0x0017, 0x0017, 0x0016,
  0x0016, 0x0015, 0x0015, 0x0015, 0x0014, 0x0014, 0x0013, 0x0013, 0x0013, 0x0012, 0x0012, 0x0012,
  0x0011, 0x0011, 0x0011, 0x0010, 0x0010, 0x0010, 0x000f, 0x000f, 0x000f, 0x000e, 0x000e, 0x000e,
  0x000d, 0x000d, 0x000d, 0x000d, 0x000c, 0x000c, 0x000c, 0x000c, 0x000b, 0x000b, 0x000b, 0x000b,
  0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0008, 0x0008,
  0x0008, 0x0008, 0x0008, 0x0008, 0x0007, 0x0007, 0x0007, 0x0007, 0x0007, 0x0007, 0x0006, 0x0006,
  0x0006, 0x0006, 0x0006, 0x0006, 0x0006, 0x0006, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
  0x0005, 0x0005, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
  0x0004, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
  0x0003, 0x0003, 0x0003, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002,
  0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0001, 0x0001,
  0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001,
  0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001,
  0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000 };

static const int16_t hth[3][50] = /* hearing threshold table */
{
  {0x04d0, 0x04d0, 0x0440, 0x0400, 0x03e0, 0x03c0, 0x03b0, 0x03b0, 0x03a0, 0x03a0, 0x03a0, 0x03a0,
   0x03a0, 0x0390, 0x0390, 0x0390, 0x0380, 0x0380, 0x0370, 0x0370, 0x0360, 0x0360, 0x0350, 0x0350,
   0x0340, 0x0340, 0x0330, 0x0320, 0x0310, 0x0300, 0x02f0, 0x02f0, 0x02f0, 0x02f0, 0x0300, 0x0310,
   0x0340, 0x0390, 0x03e0, 0x0420, 0x0460, 0x0490, 0x04a0, 0x0440, 0x0440, 0x0400, 0x0520, 0x0800,
   0x0840, 0x0840},
  {0x04f0, 0x04f0, 0x0460, 0x0410, 0x03e0, 0x03d0, 0x03c0, 0x03b0, 0x03b0, 0x03a0, 0x03a0, 0x03a0,
   0x03a0, 0x03a0, 0x0390, 0x0390, 0x0390, 0x0380, 0x0380, 0x0380, 0x0370, 0x0370, 0x0360, 0x0360,
   0x0350, 0x0350, 0x0340, 0x0340, 0x0320, 0x0310, 0x0300, 0x02f0, 0x02f0, 0x02f0, 0x02f0, 0x0300,
   0x0320, 0x0350, 0x0390, 0x03e0, 0x0420, 0x0450, 0x04a0, 0x0490, 0x0460, 0x0440, 0x0480, 0x0630,
   0x0840, 0x0840},
  {0x0580, 0x0580, 0x04b0, 0x0450, 0x0420, 0x03f0, 0x03e0, 0x03d0, 0x03c0, 0x03b0, 0x03b0, 0x03b0,
   0x03a0, 0x03a0, 0x03a0, 0x03a0, 0x03a0, 0x03a0, 0x03a0, 0x03a0, 0x0390, 0x0390, 0x0390, 0x0390,
   0x0380, 0x0380, 0x0380, 0x0370, 0x0360, 0x0350, 0x0340, 0x0330, 0x0320, 0x0310, 0x0300, 0x02f0,
   0x02f0, 0x02f0, 0x0300, 0x0310, 0x0330, 0x0350, 0x03c0, 0x0410, 0x0470, 0x04a0, 0x0460, 0x0440,
   0x0450, 0x04e0}
};

static const uint8_t baptab[64] = /* bit allocation pointer table */
{ 0, 1, 1, 1, 1, 1, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 10,
  10, 10, 10, 11, 11, 11, 11, 12, 12, 12, 12, 13, 13, 13, 13, 14, 14, 14, 14, 14, 14, 14, 14, 15,
  15, 15, 15, 15, 15, 15, 15, 15 };

static inline int16_t
logadd (int16_t a, int16_t b)
{
  int16_t c = a - b;
  uint8_t address = FFMIN ((ABS (c) >> 1), 255);

  return ((c >= 0) ? (a + latab[address]) : (b + latab[address]));
}

static inline int16_t
calc_lowcomp (int16_t a, int16_t b0, int16_t b1, uint8_t bin)
{
  if (bin < 7) {
    if ((b0 + 256) == b1)
      a = 384;
    else if (b0 > b1)
      a = FFMAX (0, a - 64);
  }
  else if (bin < 20) {
    if ((b0 + 256) == b1)
      a = 320;
    else if (b0 > b1)
      a = FFMAX (0, a - 64);
  }
  else {
    a = FFMAX (0, a - 128);
  }

  return a;
}

/* do the bit allocation for chnl.
 * chnl = 0 to 4 - fbw channel
 * chnl = 5 coupling channel
 * chnl = 6 lfe channel
 */
static int
_do_bit_allocation (AC3DecodeContext * ctx, int chnl)
{
  ac3_audio_block *ab = &ctx->audio_block;
  int16_t sdecay, fdecay, sgain, dbknee, floor;
  int16_t lowcomp, fgain, snroffset, fastleak, slowleak;
  int16_t psd[256], bndpsd[50], excite[50], mask[50], delta;
  uint8_t start, end, bin, i, j, k, lastbin, bndstrt, bndend, begin, deltnseg, band, seg, address;
  uint8_t fscod = ctx->sync_info.fscod;
  uint8_t *exps, *deltoffst, *deltlen, *deltba;
  uint8_t *baps;
  int do_delta = 0;

  /* initialization */
  sdecay = slowdec[ab->sdcycod];
  fdecay = fastdec[ab->fdcycod];
  sgain = slowgain[ab->sgaincod];
  dbknee = dbpbtab[ab->dbpbcod];
  floor = dbpbtab[ab->floorcod];

  if (chnl == 5) {
    start = ab->cplstrtmant;
    end = ab->cplendmant;
    fgain = fastgain[ab->cplfgaincod];
    snroffset = (((ab->csnroffst - 15) << 4) + ab->cplfsnroffst) << 2;
    fastleak = (ab->cplfleak << 8) + 768;
    slowleak = (ab->cplsleak << 8) + 768;
    exps = ab->dcplexps;
    baps = ab->cplbap;
    if (ab->cpldeltbae == 0 || ab->cpldeltbae == 1) {
      do_delta = 1;
      deltnseg = ab->cpldeltnseg;
      deltoffst = ab->cpldeltoffst;
      deltlen = ab->cpldeltlen;
      deltba = ab->cpldeltba;
    }
  }
  else if (chnl == 6) {
    start = 0;
    end = 7;
    lowcomp = 0;
    fgain = fastgain[ab->lfefgaincod];
    snroffset = (((ab->csnroffst - 15) << 4) + ab->lfefsnroffst) << 2;
    exps = ab->dlfeexps;
    baps = ab->lfebap;
  }
  else {
    start = 0;
    end = ab->endmant[chnl];
    lowcomp = 0;
    fgain = fastgain[ab->fgaincod[chnl]];
    snroffset = (((ab->csnroffst - 15) << 4) + ab->fsnroffst[chnl]) << 2;
    exps = ab->dexps[chnl];
    baps = ab->bap[chnl];
    if (ab->deltbae[chnl] == 0 || ab->deltbae[chnl] == 1) {
      do_delta = 1;
      deltnseg = ab->deltnseg[chnl];
      deltoffst = ab->deltoffst[chnl];
      deltlen = ab->deltlen[chnl];
      deltba = ab->deltba[chnl];
    }
  }

  for (bin = start; bin < end; bin++) /* exponent mapping into psd */
    psd[bin] = (3072 - ((int16_t) (exps[bin] << 7)));

  /* psd integration */
  j = start;
  k = masktab[start];
  do {
    lastbin = FFMIN (bndtab[k] + bndsz[k], end);
    bndpsd[k] = psd[j];
    j++;
    for (i = j; i < lastbin; i++) {
      bndpsd[k] = logadd (bndpsd[k], psd[j]);
      j++;
    }
    k++;
  } while (end > lastbin);

  /* compute the excite function */
  bndstrt = masktab[start];
  bndend = masktab[end - 1] + 1;
  if (bndstrt == 0) {
    lowcomp = calc_lowcomp (lowcomp, bndpsd[0], bndpsd[1], 0);
    excite[0] = bndpsd[0] - fgain - lowcomp;
    lowcomp = calc_lowcomp (lowcomp, bndpsd[1], bndpsd[2], 1);
    excite[1] = bndpsd[1] - fgain - lowcomp;
    begin = 7;
    for (bin = 2; bin < 7; bin++) {
      if (bndend != 7 || bin != 6)
        lowcomp = calc_lowcomp (lowcomp, bndpsd[bin], bndpsd[bin + 1], bin);
      fastleak = bndpsd[bin] - fgain;
      slowleak = bndpsd[bin] - sgain;
      excite[bin] = fastleak - lowcomp;
      if (bndend != 7 || bin != 6)
        if (bndpsd[bin] <= bndpsd[bin + 1]) {
          begin = bin + 1;
          break;
        }
    }
    for (bin = begin; bin < (FFMIN (bndend, 22)); bin++) {
      if (bndend != 7 || bin != 6)
        lowcomp = calc_lowcomp (lowcomp, bndpsd[bin], bndpsd[bin + 1], bin);
      fastleak -= fdecay;
      fastleak = FFMAX (fastleak, bndpsd[bin] - fgain);
      slowleak -= sdecay;
      slowleak = FFMAX (slowleak, bndpsd[bin] - sgain);
      excite[bin] = FFMAX (fastleak - lowcomp, slowleak);
    }
    begin = 22;
  }
  else {
    begin = bndstrt;
  }
  for (bin = begin; bin < bndend; bin++) {
    fastleak -= fdecay;
    fastleak = FFMAX (fastleak, bndpsd[bin] - fgain);
    slowleak -= sdecay;
    slowleak = FFMAX (slowleak, bndpsd[bin] - sgain);
    excite[bin] = FFMAX (fastleak, slowleak);
  }

  /* compute the masking curve */
  for (bin = bndstrt; bin < bndend; bin++) {
    if (bndpsd[bin] < dbknee)
      excite[bin] += ((dbknee - bndpsd[bin]) >> 2);
    mask[bin] = FFMAX (excite[bin], hth[fscod][bin]);
  }

  /* apply the delta bit allocation */
  if (do_delta) {
    band = 0;
    for (seg = 0; seg < deltnseg + 1; seg++) {
      band += deltoffst[seg];
      if (deltba[seg] >= 4)
        delta = (deltba[seg] - 3) << 7;
      else
        delta = (deltba[seg] - 4) << 7;
      for (k = 0; k < deltlen[seg]; k++) {
        mask[band] += delta;
        band++;
      }
    }
  }

  /*compute the bit allocation */
  i = start;
  j = masktab[start];
  do {
    lastbin = FFMIN (bndtab[j] + bndsz[j], end);
    mask[j] -= snroffset;
    mask[j] -= floor;
    if (mask[j] < 0)
      mask[j] = 0;
    mask[j] &= 0x1fe0;
    mask[j] += floor;
    for (k = i; k < lastbin; k++) {
      address = (psd[i] - mask[j]) >> 5;
      address = FFMIN (63, (FFMAX (0, address)));
      baps[i] = baptab[address];
      i++;
    }
    j++;
  } while (end > lastbin);

  return 0;
}

static int
do_bit_allocation (AC3DecodeContext * ctx, int flags)
{
  ac3_audio_block *ab = &ctx->audio_block;
  int i, snroffst = 0;

  if (!flags) /* bit allocation is not required */
    return 0;

  if (ab->flags & AC3_AB_SNROFFSTE) { /* check whether snroffsts are zero */
    snroffst += ab->csnroffst;
    if (ab->flags & AC3_AB_CPLINU)
      snroffst += ab->cplfsnroffst;
    for (i = 0; i < ctx->bsi.nfchans; i++)
      snroffst += ab->fsnroffst[i];
    if (ctx->bsi.flags & AC3_BSI_LFEON)
      snroffst += ab->lfefsnroffst;
    if (!snroffst) {
      memset (ab->cplbap, 0, sizeof (ab->cplbap));
      for (i = 0; i < ctx->bsi.nfchans; i++)
        memset (ab->bap[i], 0, sizeof (ab->bap[i]));
      memset (ab->lfebap, 0, sizeof (ab->lfebap));

      return 0;
    }
  }

  /* perform bit allocation */
  if ((ab->flags & AC3_AB_CPLINU) && (flags & 64))
    if (_do_bit_allocation (ctx, 5))
      return -1;
  for (i = 0; i < ctx->bsi.nfchans; i++)
    if (flags & (1 << i))
      if (_do_bit_allocation (ctx, i))
        return -1;
  if ((ctx->bsi.flags & AC3_BSI_LFEON) && (flags & 32))
    if (_do_bit_allocation (ctx, 6))
      return -1;

  return 0;
}

/* table for exponent to scale_factor mapping
 * scale_factor[i] = 2 ^ -(i + 15)
 */
static const float scale_factors[25] = {
  0.000030517578125000000000000000000000000,
  0.000015258789062500000000000000000000000,
  0.000007629394531250000000000000000000000,
  0.000003814697265625000000000000000000000,
  0.000001907348632812500000000000000000000,
  0.000000953674316406250000000000000000000,
  0.000000476837158203125000000000000000000,
  0.000000238418579101562500000000000000000,
  0.000000119209289550781250000000000000000,
  0.000000059604644775390625000000000000000,
  0.000000029802322387695312500000000000000,
  0.000000014901161193847656250000000000000,
  0.000000007450580596923828125000000000000,
  0.000000003725290298461914062500000000000,
  0.000000001862645149230957031250000000000,
  0.000000000931322574615478515625000000000,
  0.000000000465661287307739257812500000000,
  0.000000000232830643653869628906250000000,
  0.000000000116415321826934814453125000000,
  0.000000000058207660913467407226562500000,
  0.000000000029103830456733703613281250000,
  0.000000000014551915228366851806640625000,
  0.000000000007275957614183425903320312500,
  0.000000000003637978807091712951660156250,
  0.000000000001818989403545856475830078125
};

static const int16_t l3_q_tab[3] = { /* 3-level quantization table */
  (-2 << 15) / 3, 0, (2 << 15) / 3
};

static const int16_t l5_q_tab[5] = { /* 5-level quantization table */
  (-4 << 15) / 5, (-2 << 15) / 5, 0, (2 << 15) / 5, (4 << 15) / 5
};

static const int16_t l7_q_tab[7] = { /* 7-level quantization table */
  (-6 << 15) / 7, (-4 << 15) / 7, (-2 << 15) / 7, 0,
  (2 << 15) / 7, (4 << 15) / 7, (6 << 15) / 7
};

static const int16_t l11_q_tab[11] = { /* 11-level quantization table */
  (-10 << 15) / 11, (-8 << 15) / 11, (-6 << 15) / 11, (-4 << 15) / 11, (-2 << 15) / 11, 0,
  (2 << 15) / 11, (4 << 15) / 11, (6 << 15) / 11, (8 << 15) / 11, (10 << 15) / 11
};

static const int16_t l15_q_tab[15] = { /* 15-level quantization table */
  (-14 << 15) / 15, (-12 << 15) / 15, (-10 << 15) / 15, (-8 << 15) / 15,
  (-6 << 15) / 15, (-4 << 15) / 15, (-2 << 15) / 15, 0,
  (2 << 15) / 15, (4 << 15) / 15, (6 << 15) / 15, (8 << 15) / 15,
  (10 << 15) / 15, (12 << 15) / 15, (14 << 15) / 15
};

static const uint8_t qntztab[16] = { 0, 5, 7, 3, 7, 4, 5, 6, 7, 8, 9, 10, 12, 12, 14, 16 };

static inline float
to_float (uint8_t exp, int16_t mantissa)
{
  return ((float) (mantissa * scale_factors[exp]));
}

typedef struct
{ /* grouped mantissas for 3-level 5-leve and 11-level quantization */
  uint8_t gcodes[3];
  uint8_t gcptr;
} mant_group;

/* Get the transform coefficients for particular channel */
static int
_get_transform_coeffs (uint8_t * exps, uint8_t * bap, float *samples,
                       int start, int end, int dith_flag, GetBitContext * gb)
{
  int16_t mantissa;
  int i;
  int gcode;
  mant_group l3_grp, l5_grp, l11_grp;

  for (i = 0; i < 3; i++)
    l3_grp.gcodes[i] = l5_grp.gcodes[i] = l11_grp.gcodes[i] = -1;
  l3_grp.gcptr = l5_grp.gcptr = 3;
  l11_grp.gcptr = 2;

  i = 0;
  while (i < start)
    samples[i++] = 0;

  for (i = start; i < end; i++) {
    switch (bap[i]) {
    case 0:
      if (!dith_flag)
        mantissa = 0;
      else
        mantissa = gen_dither ();
      samples[i] = to_float (exps[i], mantissa);
      break;

    case 1:
      if (l3_grp.gcptr > 2) {
        gcode = get_bits_long (gb, qntztab[1]);
        if (gcode > 26)
          return -1;
        l3_grp.gcodes[0] = gcode / 9;
        l3_grp.gcodes[1] = (gcode % 9) / 3;
        l3_grp.gcodes[2] = (gcode % 9) % 3;
        l3_grp.gcptr = 0;
      }
      mantissa = l3_q_tab[l3_grp.gcodes[l3_grp.gcptr++]];
      samples[i] = to_float (exps[i], mantissa);
      break;

    case 2:
      if (l5_grp.gcptr > 2) {
        gcode = get_bits_long (gb, qntztab[2]);
        if (gcode > 124)
          return -1;
        l5_grp.gcodes[0] = gcode / 25;
        l5_grp.gcodes[1] = (gcode % 25) / 5;
        l5_grp.gcodes[2] = (gcode % 25) % 5;
        l5_grp.gcptr = 0;
      }
      mantissa = l5_q_tab[l5_grp.gcodes[l5_grp.gcptr++]];
      samples[i] = to_float (exps[i], mantissa);
      break;

    case 3:
      mantissa = get_bits_long (gb, qntztab[3]);
      if (mantissa > 6)
        return -1;
      mantissa = l7_q_tab[mantissa];
      samples[i] = to_float (exps[i], mantissa);
      break;

    case 4:
      if (l11_grp.gcptr > 1) {
        gcode = get_bits_long (gb, qntztab[4]);
        if (gcode > 120)
          return -1;
        l11_grp.gcodes[0] = gcode / 11;
        l11_grp.gcodes[1] = gcode % 11;
      }
      mantissa = l11_q_tab[l11_grp.gcodes[l11_grp.gcptr++]];
      samples[i] = to_float (exps[i], mantissa);
      break;

    case 5:
      mantissa = get_bits_long (gb, qntztab[5]);
      if (mantissa > 14)
        return -1;
      mantissa = l15_q_tab[mantissa];
      break;

    default:
      mantissa = get_bits_long (gb, qntztab[bap[i]]) << (16 - qntztab[bap[i]]);
      samples[i] = to_float (exps[i], mantissa);
      break;
    }
  }

  i = end;
  while (i < 256)
    samples[i++] = 0;

  return 0;
}

static int
uncouple_channels (AC3DecodeContext * ctx)
{
  ac3_audio_block *ab = &ctx->audio_block;
  int ch, sbnd, bin;
  int index;
  float (*samples)[256];
  int16_t mantissa;

  samples = (float (*)[256]) (ab->ab_samples);
  samples += (ctx->bsi.flags & AC3_BSI_LFEON) ? 256 : 0;

  /* uncouple channels */
  for (ch = 0; ch < ctx->bsi.nfchans; ch++)
    if (ab->chincpl & (1 << ch))
      for (sbnd = ab->cplbegf; sbnd < 3 + ab->cplendf; sbnd++)
        for (bin = 0; bin < 12; bin++) {
          index = sbnd * 12 + bin + 37;
          samples[ch][index] = ab->cplcoeffs[index] * ab->cplco[ch][sbnd] * 8;
        }

  /* generate dither if required */
  for (ch = 0; ch < ctx->bsi.nfchans; ch++)
    if ((ab->chincpl & (1 << ch)) && (ab->dithflag & (1 << ch)))
      for (index = 0; index < ab->endmant[ch]; index++)
        if (!ab->bap[ch][index]) {
          mantissa = gen_dither ();
          samples[ch][index] = to_float (ab->dexps[ch][index], mantissa);
        }

  return 0;
}

static int
get_transform_coeffs (AC3DecodeContext * ctx)
{
  int i;
  ac3_audio_block *ab = &ctx->audio_block;
  float *samples = ab->ab_samples;
  int got_cplchan = 0;
  int dithflag = 0;

  samples += (ctx->bsi.flags & AC3_BSI_LFEON) ? 256 : 0;
  for (i = 0; i < ctx->bsi.nfchans; i++) {
    if ((ab->flags & AC3_AB_CPLINU) && (ab->chincpl & (1 << i)))
      dithflag = 0; /* don't generate dither until channels are decoupled */
    else
      dithflag = ab->dithflag & (1 << i);
    /* transform coefficients for individual channel */
    if (_get_transform_coeffs (ab->dexps[i], ab->bap[i], samples + (i * 256),
                               0, ab->endmant[i], dithflag, &ctx->gb))
      return -1;
    /* tranform coefficients for coupling channels */
    if ((ab->flags & AC3_AB_CPLINU) && (ab->chincpl & (1 << i)) && !got_cplchan) {
      if (_get_transform_coeffs (ab->dcplexps, ab->cplbap, ab->cplcoeffs,
                                 ab->cplstrtmant, ab->cplendmant, 0, &ctx->gb))
        return -1;
      got_cplchan = 1;
    }
  }

  /* uncouple the channels from the coupling channel */
  if (ab->flags & AC3_AB_CPLINU)
    if (uncouple_channels (ctx))
      return -1;

  return 0;
}

/* generate coupling co-ordinates for each coupling subband
 * from coupling co-ordinates of each band and coupling band
 * structure information
 */
static int
generate_coupling_coordinates (AC3DecodeContext * ctx)
{
  ac3_audio_block *ab = &ctx->audio_block;
  uint8_t exp, mstrcplco;
  int16_t mant;
  uint32_t cplbndstrc = (1 << ab->ncplsubnd) >> 1;
  int ch, bnd, sbnd;
  float cplco;

  if (ab->cplcoe)
    for (ch = 0; ch < ctx->bsi.nfchans; ch++)
      if (ab->cplcoe & (1 << ch)) {
        mstrcplco = 3 * ab->mstrcplco[ch];
        sbnd = ab->cplbegf;
        for (bnd = 0; bnd < ab->ncplbnd; bnd++) {
          exp = ab->cplcoexp[ch][bnd];
          if (exp == 15)
            mant = ab->cplcomant[ch][bnd] <<= 14;
          else
            mant = (ab->cplcomant[ch][bnd] | 0x10) << 13;
          cplco = to_float (exp + mstrcplco, mant);
          if (ctx->bsi.acmod == 0x02 && (ab->flags & AC3_AB_PHSFLGINU) && ch == 1
              && (ab->phsflg & (1 << bnd)))
            cplco = -cplco; /* invert the right channel */
          ab->cplco[ch][sbnd++] = cplco;
          while (cplbndstrc & ab->cplbndstrc) {
            cplbndstrc >>= 1;
            ab->cplco[ch][sbnd++] = cplco;
          }
          cplbndstrc >>= 1;
        }
      }

  return 0;
}

static int
ac3_parse_audio_block (AC3DecodeContext * ctx, int index)
{
  ac3_audio_block *ab = &ctx->audio_block;
  int nfchans = ctx->bsi.nfchans;
  int acmod = ctx->bsi.acmod;
  int i, bnd, rbnd, grp, seg;
  GetBitContext *gb = &ctx->gb;
  uint32_t *flags = &ab->flags;
  int bit_alloc_flags = 0;

  *flags = 0;
  ab->blksw = 0;
  for (i = 0; i < nfchans; i++) /*block switch flag */
    ab->blksw |= get_bits_long (gb, 1) << i;
  ab->dithflag = 0;
  for (i = 0; i < nfchans; i++) /* dithering flag */
    ab->dithflag |= get_bits_long (gb, 1) << i;
  if (get_bits_long (gb, 1)) { /* dynamic range */
    *flags |= AC3_AB_DYNRNGE;
    ab->dynrng = get_bits_long (gb, 8);
  }
  if (acmod == 0x00) { /* dynamic range 1+1 mode */
    if (get_bits_long (gb, 1)) {
      *flags |= AC3_AB_DYNRNG2E;
      ab->dynrng2 = get_bits_long (gb, 8);
    }
  }
  ab->chincpl = 0;
  if (get_bits_long (gb, 1)) { /* coupling strategy */
    *flags |= AC3_AB_CPLSTRE;
    ab->cplbndstrc = 0;
    if (get_bits_long (gb, 1)) { /* coupling in use */
      *flags |= AC3_AB_CPLINU;
    for (i = 0; i < nfchans; i++)
      ab->chincpl |= get_bits_long (gb, 1) << i;
    if (acmod == 0x02)
      if (get_bits_long (gb, 1)) /* phase flag in use */
        *flags |= AC3_AB_PHSFLGINU;
    ab->cplbegf = get_bits_long (gb, 4);
    ab->cplendf = get_bits_long (gb, 4);
    if ((ab->ncplsubnd = 3 + ab->cplendf - ab->cplbegf) < 0)
      return -1;
    ab->ncplbnd = ab->ncplsubnd;
    for (i = 0; i < ab->ncplsubnd - 1; i++) /* coupling band structure */
      if (get_bits_long (gb, 1)) {
        ab->cplbndstrc |= 1 << i;
        ab->ncplbnd--;
      }
    }
  }
  if (*flags & AC3_AB_CPLINU) {
    ab->cplcoe = 0;
    for (i = 0; i < nfchans; i++)
      if (ab->chincpl & (1 << i))
        if (get_bits_long (gb, 1)) { /* coupling co-ordinates */
          ab->cplcoe |= 1 << i;
          ab->mstrcplco[i] = get_bits_long (gb, 2);
          for (bnd = 0; bnd < ab->ncplbnd; bnd++) {
            ab->cplcoexp[i][bnd] = get_bits_long (gb, 4);
            ab->cplcomant[i][bnd] = get_bits_long (gb, 4);
          }
        }
  }
  ab->phsflg = 0;
  if ((acmod == 0x02) && (*flags & AC3_AB_PHSFLGINU) && (ab->cplcoe & 1 || ab->cplcoe & (1 << 1))) {
    for (bnd = 0; bnd < ab->ncplbnd; bnd++)
      if (get_bits_long (gb, 1))
        ab->phsflg |= 1 << bnd;
  }
  generate_coupling_coordinates (ctx);
  ab->rematflg = 0;
  if (acmod == 0x02) /* rematrixing */
    if (get_bits_long (gb, 1)) {
      *flags |= AC3_AB_REMATSTR;
      if (ab->cplbegf > 2 || !(*flags & AC3_AB_CPLINU))
        for (rbnd = 0; rbnd < 4; rbnd++)
          ab->rematflg |= get_bits_long (gb, 1) << bnd;
      else if (ab->cplbegf > 0 && ab->cplbegf <= 2 && *flags & AC3_AB_CPLINU)
        for (rbnd = 0; rbnd < 3; rbnd++)
          ab->rematflg |= get_bits_long (gb, 1) << bnd;
      else if (!(ab->cplbegf) && *flags & AC3_AB_CPLINU)
        for (rbnd = 0; rbnd < 2; rbnd++)
          ab->rematflg |= get_bits_long (gb, 1) << bnd;
    }
  if (*flags & AC3_AB_CPLINU) /* coupling exponent strategy */
    ab->cplexpstr = get_bits_long (gb, 2);
  for (i = 0; i < nfchans; i++) /* channel exponent strategy */
    ab->chexpstr[i] = get_bits_long (gb, 2);
  if (ctx->bsi.flags & AC3_BSI_LFEON) /* lfe exponent strategy */
    ab->lfeexpstr = get_bits_long (gb, 1);
  for (i = 0; i < nfchans; i++) /* channel bandwidth code */
    if (ab->chexpstr[i] != AC3_EXPSTR_REUSE)
      if (!(ab->chincpl & (1 << i))) {
        ab->chbwcod[i] = get_bits_long (gb, 6);
        if (ab->chbwcod[i] > 60)
          return -1;
      }
  if (*flags & AC3_AB_CPLINU)
    if (ab->cplexpstr != AC3_EXPSTR_REUSE) {/* coupling exponents */
      bit_alloc_flags |= 64;
      ab->cplabsexp = get_bits_long (gb, 4) << 1;
      ab->cplstrtmant = (ab->cplbegf * 12) + 37;
      ab->cplendmant = ((ab->cplendmant + 3) * 12) + 37;
      ab->ncplgrps = (ab->cplendmant - ab->cplstrtmant) / (3 << (ab->cplexpstr - 1));
      for (grp = 0; grp < ab->ncplgrps; grp++)
        ab->cplexps[grp] = get_bits_long (gb, 7);
    }
  for (i = 0; i < nfchans; i++) /* fbw channel exponents */
    if (ab->chexpstr[i] != AC3_EXPSTR_REUSE) {
      bit_alloc_flags |= 1 << i;
      if (ab->chincpl & (1 << i))
        ab->endmant[i] = (ab->cplbegf * 12) + 37;
      else
        ab->endmant[i] = ((ab->chbwcod[i] + 3) * 12) + 37;
      ab->nchgrps[i] =
        (ab->endmant[i] + (3 << (ab->chexpstr[i] - 1)) - 4) / (3 << (ab->chexpstr[i] - 1));
      ab->exps[i][0] = ab->dexps[i][0] = get_bits_long (gb, 4);
      for (grp = 1; grp <= ab->nchgrps[i]; grp++)
        ab->exps[i][grp] = get_bits_long (gb, 7);
      ab->gainrng[i] = get_bits_long (gb, 2);
    }
  if (ctx->bsi.flags & AC3_BSI_LFEON) /* lfe exponents */
    if (ab->lfeexpstr != AC3_EXPSTR_REUSE) {
      bit_alloc_flags |= 32;
      ab->lfeexps[0] = ab->dlfeexps[0] = get_bits_long (gb, 4);
      ab->lfeexps[1] = get_bits_long (gb, 7);
      ab->lfeexps[2] = get_bits_long (gb, 7);
    }
  if (decode_exponents (ctx)) /* decode the exponents for this block */
    return -1;
  if (get_bits_long (gb, 1)) { /* bit allocation information */
    *flags |= AC3_AB_BAIE;
    bit_alloc_flags |= 127;
    ab->sdcycod = get_bits_long (gb, 2);
    ab->fdcycod = get_bits_long (gb, 2);
    ab->sgaincod = get_bits_long (gb, 2);
    ab->dbpbcod = get_bits_long (gb, 2);
    ab->floorcod = get_bits_long (gb, 3);
  }
  if (get_bits_long (gb, 1)) { /* snroffset */
    *flags |= AC3_AB_SNROFFSTE;
    bit_alloc_flags |= 127;
    ab->csnroffst = get_bits_long (gb, 6);
    if (*flags & AC3_AB_CPLINU) { /* couling fine snr offset and fast gain code */
      ab->cplfsnroffst = get_bits_long (gb, 4);
      ab->cplfgaincod = get_bits_long (gb, 3);
    }
    for (i = 0; i < nfchans; i++) { /* channel fine snr offset and fast gain code */
      ab->fsnroffst[i] = get_bits_long (gb, 4);
      ab->fgaincod[i] = get_bits_long (gb, 3);
    }
    if (ctx->bsi.flags & AC3_BSI_LFEON) { /* lfe fine snr offset and fast gain code */
      ab->lfefsnroffst = get_bits_long (gb, 4);
      ab->lfefgaincod = get_bits_long (gb, 3);
    }
  }
  if (*flags & AC3_AB_CPLINU)
    if (get_bits_long (gb, 1)) { /* coupling leak information */
      bit_alloc_flags |= 64;
      *flags |= AC3_AB_CPLLEAKE;
      ab->cplfleak = get_bits_long (gb, 3);
      ab->cplsleak = get_bits_long (gb, 3);
    }
  if (get_bits_long (gb, 1)) { /* delta bit allocation information */
    *flags |= AC3_AB_DELTBAIE;
    bit_alloc_flags |= 127;
    if (*flags & AC3_AB_CPLINU) {
      ab->cpldeltbae = get_bits_long (gb, 2);
      if (ab->cpldeltbae == AC3_DBASTR_RESERVED)
        return -1;
    }
    for (i = 0; i < nfchans; i++) {
      ab->deltbae[i] = get_bits_long (gb, 2);
      if (ab->deltbae[i] == AC3_DBASTR_RESERVED)
        return -1;
    }
    if (*flags & AC3_AB_CPLINU)
      if (ab->cpldeltbae == AC3_DBASTR_NEW) { /*coupling delta offset, len and bit allocation */
    ab->cpldeltnseg = get_bits_long (gb, 3);
    for (seg = 0; seg <= ab->cpldeltnseg; seg++) {
      ab->cpldeltoffst[seg] = get_bits_long (gb, 5);
      ab->cpldeltlen[seg] = get_bits_long (gb, 4);
      ab->cpldeltba[seg] = get_bits_long (gb, 3);
    }
      }
    for (i = 0; i < nfchans; i++)
      if (ab->deltbae[i] == AC3_DBASTR_NEW) {/*channel delta offset, len and bit allocation */
    ab->deltnseg[i] = get_bits_long (gb, 3);
    for (seg = 0; seg <= ab->deltnseg[i]; seg++) {
      ab->deltoffst[i][seg] = get_bits_long (gb, 5);
      ab->deltlen[i][seg] = get_bits_long (gb, 4);
      ab->deltba[i][seg] = get_bits_long (gb, 3);
    }
      }
  }
  if (do_bit_allocation (ctx, bit_alloc_flags)) /* perform the bit allocation */
    return -1;
  if (get_bits_long (gb, 1)) { /* unused dummy data */
    *flags |= AC3_AB_SKIPLE;
    ab->skipl = get_bits_long (gb, 9);
    while (ab->skipl) {
      get_bits_long (gb, 8);
      ab->skipl--;
    }
  }
  /* point ab_samples to the right place within smaples */
  if (!index)
    ab->ab_samples = ctx->samples;
  else {
    ab->ab_samples = ctx->samples + (i * nfchans * 256);
    ab->ab_samples += ((ctx->bsi.flags & AC3_BSI_LFEON) ? 256 : 0);
  }
  /* unpack the transform coefficients
   * this also uncouples channels if coupling is in use.
   */
  if (get_transform_coeffs (ctx))
    return -1;

  return 0;
}


static int
ac3_decode_frame (AVCodecContext * avctx, void *data, int *data_size, uint8_t * buf, int buf_size)
{
  AC3DecodeContext *ctx = avctx->priv_data;
  int frame_start;
  int i;

  //Synchronize the frame.
  frame_start = ac3_synchronize (buf, buf_size);
  if (frame_start == -1) {
    *data_size = 0;
    return -1;
  }

  //Initialize the GetBitContext with the start of valid AC3 Frame.
  init_get_bits (&(ctx->gb), buf + frame_start, (buf_size - frame_start) * 8);

  //Parse the syncinfo.
  //If 'fscod' is not valid the decoder shall mute as per the standard.
  if (ac3_parse_sync_info (ctx)) {
    *data_size = 0;
    return -1;
  }

  //Check for the errors.
  /*if (ac3_error_check(ctx))
     {
     *data_size = 0;
     return -1;
     } */

  //Parse the BSI.
  //If 'bsid' is not valid decoder shall not decode the audio as per the standard.
  if (ac3_parse_bsi (ctx)) {
    *data_size = 0;
    return -1;
  }

  //Parse the Audio Blocks.
  for (i = 0; i < 6; i++)
    if (ac3_parse_audio_block (ctx, i)) {
      *data_size = 0;
      return -1;
    }

  return 0;
}
