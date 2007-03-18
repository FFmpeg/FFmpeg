#include "avutil.h"
#include "swscale.h"
#include "swscale_internal.h"

#define YTABLE_MIN 384

/**
 * YUV -> RGB conversion matrixes (inverse of table 6.9 in MPEG2 standard)
 *
 * An YUV -> RGB conversion matrix is in the form
 *              | 1  0 Rv |
 *              | 1 Gu Gv |
 *              | 1 Bu  0 |
 *
 * Inverse_Table_6_9 stores | Rv Bu Gv Gu | * 255/224*2^16.
 * \arg Maximum Rv value: 117570
 * \arg Maximum Bu value: 138420
 * \arg Maximum Gv + Gu value: 25642 + 53281 = 78923
 * 
 * These values are needed to allocate table_{r, g, b}. If you modify
 * this table, please update allocate_tables() accordingly
 */
const int32_t Inverse_Table_6_9[8][4] = {
    {0, 0, 0, 0}, /* no sequence_display_extension */
    {117500, 138420, -13985, -34933}, /* ITU-R Rec. 709 (1990) */
    {0, 0, 0, 0}, /* unspecified */
    {0, 0, 0, 0}, /* reserved */
    {104480, 132820, -24811, -53150}, /* FCC */
    {104570, 132210, -25642, -53281}, /* ITU-R Rec. 624-4 System B, G */
    {104570, 132210, -25642, -53281}, /* SMPTE 170M */
    {117570, 136230, -16892, -35552}  /* SMPTE 240M (1987) */
};


/**
 * Dithering matrixes (these are bayer ordered dither matrixes
 * with some manual changes by Michael)
 */
const uint8_t  __attribute__((aligned(8))) dither_2x2_4[2][8]={
{  1,   3,   1,   3,   1,   3,   1,   3, },
{  2,   0,   2,   0,   2,   0,   2,   0, },
};

const uint8_t  __attribute__((aligned(8))) dither_2x2_8[2][8]={
{  6,   2,   6,   2,   6,   2,   6,   2, },
{  0,   4,   0,   4,   0,   4,   0,   4, },
};

const uint8_t  __attribute__((aligned(8))) dither_8x8_32[8][8]={
{ 17,   9,  23,  15,  16,   8,  22,  14, },
{  5,  29,   3,  27,   4,  28,   2,  26, },
{ 21,  13,  19,  11,  20,  12,  18,  10, },
{  0,  24,   6,  30,   1,  25,   7,  31, },
{ 16,   8,  22,  14,  17,   9,  23,  15, },
{  4,  28,   2,  26,   5,  29,   3,  27, },
{ 20,  12,  18,  10,  21,  13,  19,  11, },
{  1,  25,   7,  31,   0,  24,   6,  30, },
};

#if 0
const uint8_t  __attribute__((aligned(8))) dither_8x8_64[8][8]={
{  0,  48,  12,  60,   3,  51,  15,  63, },
{ 32,  16,  44,  28,  35,  19,  47,  31, },
{  8,  56,   4,  52,  11,  59,   7,  55, },
{ 40,  24,  36,  20,  43,  27,  39,  23, },
{  2,  50,  14,  62,   1,  49,  13,  61, },
{ 34,  18,  46,  30,  33,  17,  45,  29, },
{ 10,  58,   6,  54,   9,  57,   5,  53, },
{ 42,  26,  38,  22,  41,  25,  37,  21, },
};
#endif

const uint8_t  __attribute__((aligned(8))) dither_8x8_73[8][8]={
{  0,  55,  14,  68,   3,  58,  17,  72, },
{ 37,  18,  50,  32,  40,  22,  54,  35, },
{  9,  64,   5,  59,  13,  67,   8,  63, },
{ 46,  27,  41,  23,  49,  31,  44,  26, },
{  2,  57,  16,  71,   1,  56,  15,  70, },
{ 39,  21,  52,  34,  38,  19,  51,  33, },
{ 11,  66,   7,  62,  10,  65,   6,  60, },
{ 48,  30,  43,  25,  47,  29,  42,  24, },
};

#if 0
const uint8_t  __attribute__((aligned(8))) dither_8x8_128[8][8]={
{ 68,  36,  92,  60,  66,  34,  90,  58, },
{ 20, 116,  12, 108,  18, 114,  10, 106, },
{ 84,  52,  76,  44,  82,  50,  74,  42, },
{  0,  96,  24, 120,   6, 102,  30, 126, },
{ 64,  32,  88,  56,  70,  38,  94,  62, },
{ 16, 112,   8, 104,  22, 118,  14, 110, },
{ 80,  48,  72,  40,  86,  54,  78,  46, },
{  4, 100,  28, 124,   2,  98,  26, 122, },
};
#endif

#if 1
const uint8_t  __attribute__((aligned(8))) dither_8x8_220[8][8]={
{117,  62, 158, 103, 113,  58, 155, 100, },
{ 34, 199,  21, 186,  31, 196,  17, 182, },
{144,  89, 131,  76, 141,  86, 127,  72, },
{  0, 165,  41, 206,  10, 175,  52, 217, },
{110,  55, 151,  96, 120,  65, 162, 107, },
{ 28, 193,  14, 179,  38, 203,  24, 189, },
{138,  83, 124,  69, 148,  93, 134,  79, },
{  7, 172,  48, 213,   3, 168,  45, 210, },
};
#elif 1
// tries to correct a gamma of 1.5
const uint8_t  __attribute__((aligned(8))) dither_8x8_220[8][8]={
{  0, 143,  18, 200,   2, 156,  25, 215, },
{ 78,  28, 125,  64,  89,  36, 138,  74, },
{ 10, 180,   3, 161,  16, 195,   8, 175, },
{109,  51,  93,  38, 121,  60, 105,  47, },
{  1, 152,  23, 210,   0, 147,  20, 205, },
{ 85,  33, 134,  71,  81,  30, 130,  67, },
{ 14, 190,   6, 171,  12, 185,   5, 166, },
{117,  57, 101,  44, 113,  54,  97,  41, },
};
#elif 1
// tries to correct a gamma of 2.0
const uint8_t  __attribute__((aligned(8))) dither_8x8_220[8][8]={
{  0, 124,   8, 193,   0, 140,  12, 213, },
{ 55,  14, 104,  42,  66,  19, 119,  52, },
{  3, 168,   1, 145,   6, 187,   3, 162, },
{ 86,  31,  70,  21,  99,  39,  82,  28, },
{  0, 134,  11, 206,   0, 129,   9, 200, },
{ 62,  17, 114,  48,  58,  16, 109,  45, },
{  5, 181,   2, 157,   4, 175,   1, 151, },
{ 95,  36,  78,  26,  90,  34,  74,  24, },
};
#else
// tries to correct a gamma of 2.5
const uint8_t  __attribute__((aligned(8))) dither_8x8_220[8][8]={
{  0, 107,   3, 187,   0, 125,   6, 212, },
{ 39,   7,  86,  28,  49,  11, 102,  36, },
{  1, 158,   0, 131,   3, 180,   1, 151, },
{ 68,  19,  52,  12,  81,  25,  64,  17, },
{  0, 119,   5, 203,   0, 113,   4, 195, },
{ 45,   9,  96,  33,  42,   8,  91,  30, },
{  2, 172,   1, 144,   2, 165,   0, 137, },
{ 77,  23,  60,  15,  72,  21,  56,  14, },
};
#endif

static int get_entry_size(int bpp)
{
    switch(bpp) {
        case 32:
            return 4;
        case 16:
        case 15:
            return 2;
        case 24:
        case 8:
        case 4:
        case 1:
            return 1;
        default:
            return -1;
    }
}

/**
 * Allocate table_r, table_g, and table_b
 *
 * For cache efficency reasons, these three tables are allocated
 * together, so that they are contiguous in memory
 *
 * table_r is indexed in the range
 *      [-128 * 117570 / 76309, 255 + 127 * 117570 / 76309] = 
 *      [-197.21, 451.67] ---> [-198, 452]
 * table_b is indexed in the range
 *      [-128 * 138420 / 76309, 255 + 127 * 138420 / 76309] =
 *      [232.18, 485.37] ---> [-233, 486]
 * table_g is indexed in the range
 *      [-128 * 78923 / 76309, 255 + 127 * 78923 / 76309] =
 *      [-132.38, 386.35] ---> [-133, 387]
 *
 * Please look at the comments after Inverse_Table_6_9 to see where these
 * numbers are coming from.
 */
static void *allocate_tables(uint8_t **table_r, uint8_t **table_g, uint8_t **table_b, int bpp)
{
    uint8_t *table;
    int entry_size;

    entry_size = get_entry_size(bpp);

    /* First allocate the memory... */
    switch (bpp) {
        case 32:
        case 15:
        case 16:
        case 8:
        case 4:
            table = av_malloc((198 + 452 + 233 + 486 + 133 + 387) * entry_size);
            break;
        case 24:
            table = av_malloc(256 + 2 * 233);
            break;
        case 1:
            table = av_malloc (256 * 2);
            break;
        default:
            table = NULL;
    }
    if (table == NULL) {
        MSG_ERR("Cannot allocate memory for the YUV -> RGB tables!\n");

        return NULL;
    }
    
    /* ...and then, assign the table_* value */
    switch (bpp) {
        case 32:
        case 15:
        case 16:
        case 8:
        case 4:
            *table_r = table + 198 * entry_size;
            *table_b = table + (198 + 452 + 133 + 387 + 233) * entry_size;
            *table_g = table + (198 + 452 + 133) * entry_size;
            break;
        case 24:
            *table_r = *table_g = *table_b = table + 233;
            break;
        case 1:
            *table_g = table;
            *table_r = *table_b = NULL;
            break;
    }

    return table;
}

/**
 * Initialize the table_rV, table_gU[i], table_gV, and table_bU fields
 * in SwsContext
 *
 * @param inv_table the YUV -> RGB table (this is a line of Inverse_Table_6_9)
 * @param fullRange 0->MPEG YUV space 1->JPEG YUV space
*/
int yuv2rgb_c_init_tables(SwsContext *c, const int inv_table[4], int fullRange, int brightness, int contrast, int saturation)
{  
    int i;
    static uint8_t ytable[1024];
    int64_t cy, oy;
    int64_t crv, cbu, cgu, cgv;
    int entry_size = 0;
    uint8_t *table_r, *table_g, *table_b;
    int value;

    if ((inv_table[0] == 0) || (inv_table[1] == 0) || (inv_table[2] == 0) || (inv_table[3] == 0)) {
        MSG_ERR("Invalid YUV ---> RGB table!\n");

        return -1;
    }
    crv = inv_table[0];
    cbu = inv_table[1];
    cgu = inv_table[2];
    cgv = inv_table[3];
    if (fullRange) {
        cy = 1 << 16;
        oy = 0;
        crv= (crv*224) / 255;
        cbu= (cbu*224) / 255;
        cgu= (cgu*224) / 255;
        cgv= (cgv*224) / 255;
        //FIXME maybe its cleaner if the tables where based on full range (*244/255)
    } else {
        cy = ((1 << 16) * 255) / 219;
        oy= 16 << 16;
    }

    cy = (cy *contrast             )>>16;
    crv= (crv*contrast * saturation)>>32;
    cbu= (cbu*contrast * saturation)>>32;
    cgu= (cgu*contrast * saturation)>>32;
    cgv= (cgv*contrast * saturation)>>32;
    oy -= 256*brightness;

    for (i = 0; i < 1024; i++) {
        value = (cy*(((i - YTABLE_MIN)<<16) - oy) + (1<<31))>>32;
        ytable[i] = av_clip_uint8(value);
    }

    entry_size = get_entry_size(fmt_depth(c->dstFormat));
    av_free(c->yuvTable);
    c->yuvTable = allocate_tables(&table_r, &table_g, &table_b, fmt_depth(c->dstFormat));
    if (c->yuvTable == NULL) {
        return -1;
    }

    switch (fmt_depth(c->dstFormat)) {
        case 32:
            for (i = -198; i < 256 + 197; i++) {
                value = ytable[i + YTABLE_MIN];
                if (isBGR(c->dstFormat)) {
                    value <<= 16;
                }
                ((uint32_t *)table_r)[i] = value;
            }
            for (i = -133; i < 256 + 132; i++) {
                ((uint32_t *)table_g)[i] = ytable[i + YTABLE_MIN] << 8;
            }
            for (i = -233; i < 256 + 232; i++) {
                value = ytable[i + YTABLE_MIN];
                if (!isBGR(c->dstFormat)) {
                    value <<= 16;
                }
                ((uint32_t *)table_b)[i] = value;
            }
            break;

        case 24:
            for (i = -233; i < 256 + 232; i++) {
                ((uint8_t * )table_b)[i] = ytable[i + YTABLE_MIN];
            }
            break;

        case 15:
        case 16:
            for (i = -198; i < 256 + 197; i++) {
                value = ytable[i + YTABLE_MIN] >> 3;
                if (isBGR(c->dstFormat)) {
                    value <<= ((fmt_depth(c->dstFormat) == 16) ? 11 : 10);
                }
                ((uint16_t *)table_r)[i] = value;
            }
            for (i = -133; i < 256 + 132; i++) {
                value = ytable[i + YTABLE_MIN];
                value >>= ((fmt_depth(c->dstFormat) == 16) ? 2 : 3);
                ((uint16_t *)table_g)[i] = value << 5;
            }
            for (i = -233; i < 256 + 232; i++) {
                value = ytable[i + YTABLE_MIN] >> 3;
                if (!isBGR(c->dstFormat)) {
                    value <<= ((fmt_depth(c->dstFormat) == 16) ? 11 : 10);
                }
                ((uint16_t *)table_b)[i] = value;
            }
            break;
        case 8:
            for (i = -198; i < 256 + 197; i++) {
                value = (ytable[i + YTABLE_MIN - 16] + 18) / 36;
                if (isBGR(c->dstFormat)) {
                    value <<= 5;
                }
                ((uint8_t *)table_r)[i] = value;
            }
            for (i = -133; i < 256 + 132; i++) {
                value = (ytable[i + YTABLE_MIN - 16] + 18) / 36;
                if (!isBGR(c->dstFormat)) {
                    value <<= 1;
                }
                ((uint8_t *)table_g)[i] = value << 2;
            }
            for (i = -233; i < 256 + 232; i++) {
                value = (ytable[i + YTABLE_MIN - 37] + 43) / 85;
                if (!isBGR(c->dstFormat)) {
                    value <<= 6;
                }
                ((uint8_t *)table_b)[i] = value;
            }
            break;
        case 4:
            for (i = -198; i < 256 + 197; i++) {
                value = ytable[i + YTABLE_MIN - 110] >> 7;
                if (isBGR(c->dstFormat)) {
                    value <<= 3;
                }
                ((uint8_t *)table_r)[i] = value;
            }
            for (i = -133; i < 256 + 132; i++) {
                value = (ytable[i + YTABLE_MIN - 37]+ 43) / 85;
                ((uint8_t *)table_g)[i] = value << 1;
            }
            for (i = -233; i < 256 + 232; i++) {
                value = ytable[i + YTABLE_MIN - 110] >> 7;
                if (!isBGR(c->dstFormat)) {
                    value <<= 3;
                }
                ((uint8_t *)table_b)[i] = value;
            }
            break;
        case 1:
            for (i = 0; i < 256 + 256; i++) {
                value = ytable[i + YTABLE_MIN - 110] >> 7;
                ((uint8_t *)table_g)[i] = value;
            }
            break;
        default:
            MSG_ERR("%ibpp not supported by yuv2rgb\n", fmt_depth(c->dstFormat));
            av_free(c->yuvTable);
            c->yuvTable = NULL;

            return -1;
    }

    for (i = 0; i < 256; i++) {
        c->table_rV[i] = table_r +
                         entry_size * ROUNDED_DIV(crv * (i - 128), 76309);
        c->table_gU[i] = table_g +
                         entry_size * ROUNDED_DIV(cgu * (i - 128), 76309);
        c->table_gV[i] = entry_size * ROUNDED_DIV(cgv * (i - 128), 76309);
        c->table_bU[i] = table_b +
                         entry_size * ROUNDED_DIV(cbu * (i - 128), 76309);
    }

    return 0;
}
