/*
 * tables.h
 * Copyright (C) 2000-2003 Michel Lespinasse <walken@zoy.org>
 * Copyright (C) 1999-2000 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * This file is part of a52dec, a free ATSC A-52 stream decoder.
 * See http://liba52.sourceforge.net/ for updates.
 *
 * a52dec is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * a52dec is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

static const int8_t exp_1[128] = {
    -2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
     2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    25,25,25
};
static const int8_t exp_2[128] = {
    -2,-2,-2,-2,-2,-1,-1,-1,-1,-1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2,
    -2,-2,-2,-2,-2,-1,-1,-1,-1,-1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2,
    -2,-2,-2,-2,-2,-1,-1,-1,-1,-1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2,
    -2,-2,-2,-2,-2,-1,-1,-1,-1,-1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2,
    -2,-2,-2,-2,-2,-1,-1,-1,-1,-1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2,
    25,25,25
};
static const int8_t exp_3[128] = {
    -2,-1, 0, 1, 2,-2,-1, 0, 1, 2,-2,-1, 0, 1, 2,-2,-1, 0, 1, 2,-2,-1, 0, 1, 2,
    -2,-1, 0, 1, 2,-2,-1, 0, 1, 2,-2,-1, 0, 1, 2,-2,-1, 0, 1, 2,-2,-1, 0, 1, 2,
    -2,-1, 0, 1, 2,-2,-1, 0, 1, 2,-2,-1, 0, 1, 2,-2,-1, 0, 1, 2,-2,-1, 0, 1, 2,
    -2,-1, 0, 1, 2,-2,-1, 0, 1, 2,-2,-1, 0, 1, 2,-2,-1, 0, 1, 2,-2,-1, 0, 1, 2,
    -2,-1, 0, 1, 2,-2,-1, 0, 1, 2,-2,-1, 0, 1, 2,-2,-1, 0, 1, 2,-2,-1, 0, 1, 2,
    25,25,25
};

#define Q(x) ROUND (32768.0 * x)

#define Q0 Q (-2/3)
#define Q1 Q (0)
#define Q2 Q (2/3)

static const quantizer_t q_1_0[32] = {
    Q0, Q0, Q0, Q0, Q0, Q0, Q0, Q0, Q0,
    Q1, Q1, Q1, Q1, Q1, Q1, Q1, Q1, Q1,
    Q2, Q2, Q2, Q2, Q2, Q2, Q2, Q2, Q2,
    0,  0,  0,  0,  0
};

static const quantizer_t q_1_1[32] = {
    Q0, Q0, Q0, Q1, Q1, Q1, Q2, Q2, Q2,
    Q0, Q0, Q0, Q1, Q1, Q1, Q2, Q2, Q2,
    Q0, Q0, Q0, Q1, Q1, Q1, Q2, Q2, Q2,
    0,  0,  0,  0,  0
};

static const quantizer_t q_1_2[32] = {
    Q0, Q1, Q2, Q0, Q1, Q2, Q0, Q1, Q2,
    Q0, Q1, Q2, Q0, Q1, Q2, Q0, Q1, Q2,
    Q0, Q1, Q2, Q0, Q1, Q2, Q0, Q1, Q2,
    0,  0,  0,  0,  0
};

#undef Q0
#undef Q1
#undef Q2

#define Q0 Q (-4/5)
#define Q1 Q (-2/5)
#define Q2 Q (0)
#define Q3 Q (2/5)
#define Q4 Q (4/5)

static const quantizer_t q_2_0[128] = {
    Q0,Q0,Q0,Q0,Q0,Q0,Q0,Q0,Q0,Q0,Q0,Q0,Q0,Q0,Q0,Q0,Q0,Q0,Q0,Q0,Q0,Q0,Q0,Q0,Q0,
    Q1,Q1,Q1,Q1,Q1,Q1,Q1,Q1,Q1,Q1,Q1,Q1,Q1,Q1,Q1,Q1,Q1,Q1,Q1,Q1,Q1,Q1,Q1,Q1,Q1,
    Q2,Q2,Q2,Q2,Q2,Q2,Q2,Q2,Q2,Q2,Q2,Q2,Q2,Q2,Q2,Q2,Q2,Q2,Q2,Q2,Q2,Q2,Q2,Q2,Q2,
    Q3,Q3,Q3,Q3,Q3,Q3,Q3,Q3,Q3,Q3,Q3,Q3,Q3,Q3,Q3,Q3,Q3,Q3,Q3,Q3,Q3,Q3,Q3,Q3,Q3,
    Q4,Q4,Q4,Q4,Q4,Q4,Q4,Q4,Q4,Q4,Q4,Q4,Q4,Q4,Q4,Q4,Q4,Q4,Q4,Q4,Q4,Q4,Q4,Q4,Q4,
    0,0,0
};

static const quantizer_t q_2_1[128] = {
    Q0,Q0,Q0,Q0,Q0,Q1,Q1,Q1,Q1,Q1,Q2,Q2,Q2,Q2,Q2,Q3,Q3,Q3,Q3,Q3,Q4,Q4,Q4,Q4,Q4,
    Q0,Q0,Q0,Q0,Q0,Q1,Q1,Q1,Q1,Q1,Q2,Q2,Q2,Q2,Q2,Q3,Q3,Q3,Q3,Q3,Q4,Q4,Q4,Q4,Q4,
    Q0,Q0,Q0,Q0,Q0,Q1,Q1,Q1,Q1,Q1,Q2,Q2,Q2,Q2,Q2,Q3,Q3,Q3,Q3,Q3,Q4,Q4,Q4,Q4,Q4,
    Q0,Q0,Q0,Q0,Q0,Q1,Q1,Q1,Q1,Q1,Q2,Q2,Q2,Q2,Q2,Q3,Q3,Q3,Q3,Q3,Q4,Q4,Q4,Q4,Q4,
    Q0,Q0,Q0,Q0,Q0,Q1,Q1,Q1,Q1,Q1,Q2,Q2,Q2,Q2,Q2,Q3,Q3,Q3,Q3,Q3,Q4,Q4,Q4,Q4,Q4,
    0,0,0
};

static const quantizer_t q_2_2[128] = {
    Q0,Q1,Q2,Q3,Q4,Q0,Q1,Q2,Q3,Q4,Q0,Q1,Q2,Q3,Q4,Q0,Q1,Q2,Q3,Q4,Q0,Q1,Q2,Q3,Q4,
    Q0,Q1,Q2,Q3,Q4,Q0,Q1,Q2,Q3,Q4,Q0,Q1,Q2,Q3,Q4,Q0,Q1,Q2,Q3,Q4,Q0,Q1,Q2,Q3,Q4,
    Q0,Q1,Q2,Q3,Q4,Q0,Q1,Q2,Q3,Q4,Q0,Q1,Q2,Q3,Q4,Q0,Q1,Q2,Q3,Q4,Q0,Q1,Q2,Q3,Q4,
    Q0,Q1,Q2,Q3,Q4,Q0,Q1,Q2,Q3,Q4,Q0,Q1,Q2,Q3,Q4,Q0,Q1,Q2,Q3,Q4,Q0,Q1,Q2,Q3,Q4,
    Q0,Q1,Q2,Q3,Q4,Q0,Q1,Q2,Q3,Q4,Q0,Q1,Q2,Q3,Q4,Q0,Q1,Q2,Q3,Q4,Q0,Q1,Q2,Q3,Q4,
    0,0,0
};

#undef Q0
#undef Q1
#undef Q2
#undef Q3
#undef Q4

static const quantizer_t q_3[8] = {
    Q (-6/7), Q (-4/7), Q (-2/7), Q (0), Q (2/7), Q (4/7), Q (6/7), 0
};

#define Q0 Q (-10/11)
#define Q1 Q (-8/11)
#define Q2 Q (-6/11)
#define Q3 Q (-4/11)
#define Q4 Q (-2/11)
#define Q5 Q (0)
#define Q6 Q (2/11)
#define Q7 Q (4/11)
#define Q8 Q (6/11)
#define Q9 Q (8/11)
#define QA Q (10/11)

static const quantizer_t q_4_0[128] = {
    Q0, Q0, Q0, Q0, Q0, Q0, Q0, Q0, Q0, Q0, Q0,
    Q1, Q1, Q1, Q1, Q1, Q1, Q1, Q1, Q1, Q1, Q1,
    Q2, Q2, Q2, Q2, Q2, Q2, Q2, Q2, Q2, Q2, Q2,
    Q3, Q3, Q3, Q3, Q3, Q3, Q3, Q3, Q3, Q3, Q3,
    Q4, Q4, Q4, Q4, Q4, Q4, Q4, Q4, Q4, Q4, Q4,
    Q5, Q5, Q5, Q5, Q5, Q5, Q5, Q5, Q5, Q5, Q5,
    Q6, Q6, Q6, Q6, Q6, Q6, Q6, Q6, Q6, Q6, Q6,
    Q7, Q7, Q7, Q7, Q7, Q7, Q7, Q7, Q7, Q7, Q7,
    Q8, Q8, Q8, Q8, Q8, Q8, Q8, Q8, Q8, Q8, Q8,
    Q9, Q9, Q9, Q9, Q9, Q9, Q9, Q9, Q9, Q9, Q9,
    QA, QA, QA, QA, QA, QA, QA, QA, QA, QA, QA,
    0,  0,  0,  0,  0,  0,  0
};

static const quantizer_t q_4_1[128] = {
    Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7, Q8, Q9, QA,
    Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7, Q8, Q9, QA,
    Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7, Q8, Q9, QA,
    Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7, Q8, Q9, QA,
    Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7, Q8, Q9, QA,
    Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7, Q8, Q9, QA,
    Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7, Q8, Q9, QA,
    Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7, Q8, Q9, QA,
    Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7, Q8, Q9, QA,
    Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7, Q8, Q9, QA,
    Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7, Q8, Q9, QA,
    0,  0,  0,  0,  0,  0,  0
};

#undef Q0
#undef Q1
#undef Q2
#undef Q3
#undef Q4
#undef Q5
#undef Q6
#undef Q7
#undef Q8
#undef Q9
#undef QA

static const quantizer_t q_5[16] = {
    Q (-14/15), Q (-12/15), Q (-10/15), Q (-8/15), Q (-6/15),
    Q (-4/15), Q (-2/15), Q (0), Q (2/15), Q (4/15),
    Q (6/15), Q (8/15), Q (10/15), Q (12/15), Q (14/15), 0
};

#ifndef LIBA52_FIXED
static const sample_t scale_factor[25] = {
    0.000030517578125,
    0.0000152587890625,
    0.00000762939453125,
    0.000003814697265625,
    0.0000019073486328125,
    0.00000095367431640625,
    0.000000476837158203125,
    0.0000002384185791015625,
    0.00000011920928955078125,
    0.000000059604644775390625,
    0.0000000298023223876953125,
    0.00000001490116119384765625,
    0.000000007450580596923828125,
    0.0000000037252902984619140625,
    0.00000000186264514923095703125,
    0.000000000931322574615478515625,
    0.0000000004656612873077392578125,
    0.00000000023283064365386962890625,
    0.000000000116415321826934814453125,
    0.0000000000582076609134674072265625,
    0.00000000002910383045673370361328125,
    0.000000000014551915228366851806640625,
    0.0000000000072759576141834259033203125,
    0.00000000000363797880709171295166015625,
    0.000000000001818989403545856475830078125
};
#endif

static const uint16_t dither_lut[256] = {
    0x0000, 0xa011, 0xe033, 0x4022, 0x6077, 0xc066, 0x8044, 0x2055,
    0xc0ee, 0x60ff, 0x20dd, 0x80cc, 0xa099, 0x0088, 0x40aa, 0xe0bb,
    0x21cd, 0x81dc, 0xc1fe, 0x61ef, 0x41ba, 0xe1ab, 0xa189, 0x0198,
    0xe123, 0x4132, 0x0110, 0xa101, 0x8154, 0x2145, 0x6167, 0xc176,
    0x439a, 0xe38b, 0xa3a9, 0x03b8, 0x23ed, 0x83fc, 0xc3de, 0x63cf,
    0x8374, 0x2365, 0x6347, 0xc356, 0xe303, 0x4312, 0x0330, 0xa321,
    0x6257, 0xc246, 0x8264, 0x2275, 0x0220, 0xa231, 0xe213, 0x4202,
    0xa2b9, 0x02a8, 0x428a, 0xe29b, 0xc2ce, 0x62df, 0x22fd, 0x82ec,
    0x8734, 0x2725, 0x6707, 0xc716, 0xe743, 0x4752, 0x0770, 0xa761,
    0x47da, 0xe7cb, 0xa7e9, 0x07f8, 0x27ad, 0x87bc, 0xc79e, 0x678f,
    0xa6f9, 0x06e8, 0x46ca, 0xe6db, 0xc68e, 0x669f, 0x26bd, 0x86ac,
    0x6617, 0xc606, 0x8624, 0x2635, 0x0660, 0xa671, 0xe653, 0x4642,
    0xc4ae, 0x64bf, 0x249d, 0x848c, 0xa4d9, 0x04c8, 0x44ea, 0xe4fb,
    0x0440, 0xa451, 0xe473, 0x4462, 0x6437, 0xc426, 0x8404, 0x2415,
    0xe563, 0x4572, 0x0550, 0xa541, 0x8514, 0x2505, 0x6527, 0xc536,
    0x258d, 0x859c, 0xc5be, 0x65af, 0x45fa, 0xe5eb, 0xa5c9, 0x05d8,
    0xae79, 0x0e68, 0x4e4a, 0xee5b, 0xce0e, 0x6e1f, 0x2e3d, 0x8e2c,
    0x6e97, 0xce86, 0x8ea4, 0x2eb5, 0x0ee0, 0xaef1, 0xeed3, 0x4ec2,
    0x8fb4, 0x2fa5, 0x6f87, 0xcf96, 0xefc3, 0x4fd2, 0x0ff0, 0xafe1,
    0x4f5a, 0xef4b, 0xaf69, 0x0f78, 0x2f2d, 0x8f3c, 0xcf1e, 0x6f0f,
    0xede3, 0x4df2, 0x0dd0, 0xadc1, 0x8d94, 0x2d85, 0x6da7, 0xcdb6,
    0x2d0d, 0x8d1c, 0xcd3e, 0x6d2f, 0x4d7a, 0xed6b, 0xad49, 0x0d58,
    0xcc2e, 0x6c3f, 0x2c1d, 0x8c0c, 0xac59, 0x0c48, 0x4c6a, 0xec7b,
    0x0cc0, 0xacd1, 0xecf3, 0x4ce2, 0x6cb7, 0xcca6, 0x8c84, 0x2c95,
    0x294d, 0x895c, 0xc97e, 0x696f, 0x493a, 0xe92b, 0xa909, 0x0918,
    0xe9a3, 0x49b2, 0x0990, 0xa981, 0x89d4, 0x29c5, 0x69e7, 0xc9f6,
    0x0880, 0xa891, 0xe8b3, 0x48a2, 0x68f7, 0xc8e6, 0x88c4, 0x28d5,
    0xc86e, 0x687f, 0x285d, 0x884c, 0xa819, 0x0808, 0x482a, 0xe83b,
    0x6ad7, 0xcac6, 0x8ae4, 0x2af5, 0x0aa0, 0xaab1, 0xea93, 0x4a82,
    0xaa39, 0x0a28, 0x4a0a, 0xea1b, 0xca4e, 0x6a5f, 0x2a7d, 0x8a6c,
    0x4b1a, 0xeb0b, 0xab29, 0x0b38, 0x2b6d, 0x8b7c, 0xcb5e, 0x6b4f,
    0x8bf4, 0x2be5, 0x6bc7, 0xcbd6, 0xeb83, 0x4b92, 0x0bb0, 0xaba1
};
