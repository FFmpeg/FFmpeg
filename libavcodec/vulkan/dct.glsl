/*
 * Copyright (c) 2025 Lynne <dev@lynne.ee>
 * Copyright (c) 2016 Nathan Egge <unlord@xiph.org>
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

/**
 * Orthonormal inverse 8-point Type-II DCT based on the Chen factorization[1].
 * 1D with scale factors moved up front.
 * This computes an n-point Type-II DCT by first computing an n/2-point Type-II DCT
 * of the even indexed inputs and an n/2-point Type-IV DST of the odd indexed inputs,
 * and then combining them using a "butterfly" operation.
 *
 * [1] W.H. Chen, C. Smith, and S. Fralick,
 * "A Fast Computational Algorithm for the Discrete Cosine Transform",
 * IEEE Transactions on Communications, Vol. 25, No. 9, pp 1004-1009, Sept. 1977
 */

#ifndef VULKAN_DCT_H
#define VULKAN_DCT_H

#extension GL_EXT_spec_constant_composites : require

layout (constant_id = 16) const uint32_t nb_blocks = 1;
layout (constant_id = 17) const uint32_t nb_components = 1;

#define V(I) layout(constant_id = (18 + I)) const float sv##I = I;
V( 0) V( 1) V( 2) V( 3) V( 4) V( 5) V( 6) V( 7) V( 8) V( 9) V(10) V(11) V(12)
V(13) V(14) V(15) V(16) V(17) V(18) V(19) V(20) V(21) V(22) V(23) V(24) V(25)
V(26) V(27) V(28) V(29) V(30) V(31) V(32) V(33) V(34) V(35) V(36) V(37) V(38)
V(39) V(40) V(41) V(42) V(43) V(44) V(45) V(46) V(47) V(48) V(49) V(50) V(51)
V(52) V(53) V(54) V(55) V(56) V(57) V(58) V(59) V(60) V(61) V(62) V(63)

const float idct_scale[64] = {
     sv0,  sv1,  sv2,  sv3,  sv4,  sv5,  sv6,  sv7,  sv8,  sv9, sv10, sv11, sv12,
    sv13, sv14, sv15, sv16, sv17, sv18, sv19, sv20, sv21, sv22, sv23, sv24, sv25,
    sv26, sv27, sv28, sv29, sv30, sv31, sv32, sv33, sv34, sv35, sv36, sv37, sv38,
    sv39, sv40, sv41, sv42, sv43, sv44, sv45, sv46, sv47, sv48, sv49, sv50, sv51,
    sv52, sv53, sv54, sv55, sv56, sv57, sv58, sv59, sv60, sv61, sv62, sv63
};

/* Padded by 1 row to avoid bank conflicts */
shared float blocks[nb_blocks][nb_components*8*(8 + 1)];

void idct8(uint block, uint offset, uint stride)
{
    float t0, t1, t2, t3, t4, t5, t6, t7, u8;
    float u0, u1, u2, u3, u4, u5, u6, u7;

    /* Input */
    t0 = blocks[block][0*stride + offset];
    u4 = blocks[block][1*stride + offset];
    t2 = blocks[block][2*stride + offset];
    u6 = blocks[block][3*stride + offset];
    t1 = blocks[block][4*stride + offset];
    u5 = blocks[block][5*stride + offset];
    t3 = blocks[block][6*stride + offset];
    u7 = blocks[block][7*stride + offset];

    /* Embedded scaled inverse 4-point Type-II DCT */
    u0 = t0 + t1;
    u1 = t0 - t1;
    u3 = t2 + t3;
    u2 = (t2 - t3)*(1.4142135623730950488016887242097f) - u3;
    t0 = u0 + u3;
    t3 = u0 - u3;
    t1 = u1 + u2;
    t2 = u1 - u2;

    /* Embedded scaled inverse 4-point Type-IV DST */
    t5 = u5 + u6;
    t6 = u5 - u6;
    t7 = u4 + u7;
    t4 = u4 - u7;
    u7 = t7 + t5;
    u5 = (t7 - t5)*(1.4142135623730950488016887242097f);
    u8 = (t4 + t6)*(1.8477590650225735122563663787936f);
    u4 = u8 - t4*(1.0823922002923939687994464107328f);
    u6 = u8 - t6*(2.6131259297527530557132863468544f);
    t7 = u7;
    t6 = t7 - u6;
    t5 = t6 + u5;
    t4 = t5 - u4;

    /* Butterflies */
    u0 = t0 + t7;
    u7 = t0 - t7;
    u6 = t1 + t6;
    u1 = t1 - t6;
    u2 = t2 + t5;
    u5 = t2 - t5;
    u4 = t3 + t4;
    u3 = t3 - t4;

    /* Output */
    blocks[block][0*stride + offset] = u0;
    blocks[block][1*stride + offset] = u1;
    blocks[block][2*stride + offset] = u2;
    blocks[block][3*stride + offset] = u3;
    blocks[block][4*stride + offset] = u4;
    blocks[block][5*stride + offset] = u5;
    blocks[block][6*stride + offset] = u6;
    blocks[block][7*stride + offset] = u7;
}

#endif /* VULKAN_DCT_H */
