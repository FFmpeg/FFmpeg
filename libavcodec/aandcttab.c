/*
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * AAN (Arai Agui Aakajima) (I)DCT tables
 */

#include <stdint.h>

const uint16_t ff_aanscales[64] = {
    /* precomputed values scaled up by 14 bits */
    16384, 22725, 21407, 19266, 16384, 12873,  8867,  4520,
    22725, 31521, 29692, 26722, 22725, 17855, 12299,  6270,
    21407, 29692, 27969, 25172, 21407, 16819, 11585,  5906,
    19266, 26722, 25172, 22654, 19266, 15137, 10426,  5315,
    16384, 22725, 21407, 19266, 16384, 12873,  8867,  4520,
    12873, 17855, 16819, 15137, 12873, 10114,  6967,  3552,
    8867 , 12299, 11585, 10426,  8867,  6967,  4799,  2446,
    4520 ,  6270,  5906,  5315,  4520,  3552,  2446,  1247
};

const uint16_t ff_inv_aanscales[64] = {
  4096,  2953,  3135,  3483,  4096,  5213,  7568, 14846,
  2953,  2129,  2260,  2511,  2953,  3759,  5457, 10703,
  3135,  2260,  2399,  2666,  3135,  3990,  5793, 11363,
  3483,  2511,  2666,  2962,  3483,  4433,  6436, 12625,
  4096,  2953,  3135,  3483,  4096,  5213,  7568, 14846,
  5213,  3759,  3990,  4433,  5213,  6635,  9633, 18895,
  7568,  5457,  5793,  6436,  7568,  9633, 13985, 27432,
 14846, 10703, 11363, 12625, 14846, 18895, 27432, 53809,
};
