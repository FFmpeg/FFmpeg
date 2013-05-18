/*
 * Copyright (C) 2003 David S. Miller <davem@redhat.com>
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

/* You may be asking why I hard-code the instruction opcodes and don't
 * use the normal VIS assembler mnenomics for the VIS instructions.
 *
 * The reason is that Sun, in their infinite wisdom, decided that a binary
 * using a VIS instruction will cause it to be marked (in the ELF headers)
 * as doing so, and this prevents the OS from loading such binaries if the
 * current cpu doesn't have VIS.  There is no way to easily override this
 * behavior of the assembler that I am aware of.
 *
 * This totally defeats what libmpeg2 is trying to do which is allow a
 * single binary to be created, and then detect the availability of VIS
 * at runtime.
 *
 * I'm not saying that tainting the binary by default is bad, rather I'm
 * saying that not providing a way to override this easily unnecessarily
 * ties people's hands.
 *
 * Thus, we do the opcode encoding by hand and output 32-bit words in
 * the assembler to keep the binary from becoming tainted.
 */

#ifndef AVCODEC_SPARC_VIS_H
#define AVCODEC_SPARC_VIS_H

#define ACCEL_SPARC_VIS 1
#define ACCEL_SPARC_VIS2 2

static inline int vis_level(void)
{
    int accel = 0;
    accel |= ACCEL_SPARC_VIS;
    accel |= ACCEL_SPARC_VIS2;
    return accel;
}

#define vis_opc_base    ((0x1 << 31) | (0x36 << 19))
#define vis_opf(X)      ((X) << 5)
#define vis_sreg(X)     (X)
#define vis_dreg(X)     (((X)&0x1f)|((X)>>5))
#define vis_rs1_s(X)    (vis_sreg(X) << 14)
#define vis_rs1_d(X)    (vis_dreg(X) << 14)
#define vis_rs2_s(X)    (vis_sreg(X) << 0)
#define vis_rs2_d(X)    (vis_dreg(X) << 0)
#define vis_rd_s(X)     (vis_sreg(X) << 25)
#define vis_rd_d(X)     (vis_dreg(X) << 25)

#define vis_ss2s(opf,rs1,rs2,rd) \
        __asm__ volatile (".word %0" \
                              : : "i" (vis_opc_base | vis_opf(opf) | \
                                       vis_rs1_s(rs1) | \
                                       vis_rs2_s(rs2) | \
                                       vis_rd_s(rd)))

#define vis_dd2d(opf,rs1,rs2,rd) \
        __asm__ volatile (".word %0" \
                              : : "i" (vis_opc_base | vis_opf(opf) | \
                                       vis_rs1_d(rs1) | \
                                       vis_rs2_d(rs2) | \
                                       vis_rd_d(rd)))

#define vis_ss2d(opf,rs1,rs2,rd) \
        __asm__ volatile (".word %0" \
                              : : "i" (vis_opc_base | vis_opf(opf) | \
                                       vis_rs1_s(rs1) | \
                                       vis_rs2_s(rs2) | \
                                       vis_rd_d(rd)))

#define vis_sd2d(opf,rs1,rs2,rd) \
        __asm__ volatile (".word %0" \
                              : : "i" (vis_opc_base | vis_opf(opf) | \
                                       vis_rs1_s(rs1) | \
                                       vis_rs2_d(rs2) | \
                                       vis_rd_d(rd)))

#define vis_d2s(opf,rs2,rd) \
        __asm__ volatile (".word %0" \
                              : : "i" (vis_opc_base | vis_opf(opf) | \
                                       vis_rs2_d(rs2) | \
                                       vis_rd_s(rd)))

#define vis_s2d(opf,rs2,rd) \
        __asm__ volatile (".word %0" \
                              : : "i" (vis_opc_base | vis_opf(opf) | \
                                       vis_rs2_s(rs2) | \
                                       vis_rd_d(rd)))

#define vis_d12d(opf,rs1,rd) \
        __asm__ volatile (".word %0" \
                              : : "i" (vis_opc_base | vis_opf(opf) | \
                                       vis_rs1_d(rs1) | \
                                       vis_rd_d(rd)))

#define vis_d22d(opf,rs2,rd) \
        __asm__ volatile (".word %0" \
                              : : "i" (vis_opc_base | vis_opf(opf) | \
                                       vis_rs2_d(rs2) | \
                                       vis_rd_d(rd)))

#define vis_s12s(opf,rs1,rd) \
        __asm__ volatile (".word %0" \
                              : : "i" (vis_opc_base | vis_opf(opf) | \
                                       vis_rs1_s(rs1) | \
                                       vis_rd_s(rd)))

#define vis_s22s(opf,rs2,rd) \
        __asm__ volatile (".word %0" \
                              : : "i" (vis_opc_base | vis_opf(opf) | \
                                       vis_rs2_s(rs2) | \
                                       vis_rd_s(rd)))

#define vis_s(opf,rd) \
        __asm__ volatile (".word %0" \
                              : : "i" (vis_opc_base | vis_opf(opf) | \
                                       vis_rd_s(rd)))

#define vis_d(opf,rd) \
        __asm__ volatile (".word %0" \
                              : : "i" (vis_opc_base | vis_opf(opf) | \
                                       vis_rd_d(rd)))

#define vis_r2m(op,rd,mem) \
        __asm__ volatile (#op "\t%%f" #rd ", [%0]" : : "r" (&(mem)) )

#define vis_r2m_2(op,rd,mem1,mem2) \
        __asm__ volatile (#op "\t%%f" #rd ", [%0 + %1]" : : "r" (mem1), "r" (mem2) )

#define vis_m2r(op,mem,rd) \
        __asm__ volatile (#op "\t[%0], %%f" #rd : : "r" (&(mem)) )

#define vis_m2r_2(op,mem1,mem2,rd) \
        __asm__ volatile (#op "\t[%0 + %1], %%f" #rd : : "r" (mem1), "r" (mem2) )

static inline void vis_set_gsr(unsigned int val)
{
        __asm__ volatile("mov %0,%%asr19"
                             : : "r" (val));
}

#define VIS_GSR_ALIGNADDR_MASK          0x0000007
#define VIS_GSR_ALIGNADDR_SHIFT         0
#define VIS_GSR_SCALEFACT_MASK          0x0000078
#define VIS_GSR_SCALEFACT_SHIFT         3

#define vis_ld32(mem,rs1)               vis_m2r(ld, mem, rs1)
#define vis_ld32_2(mem1,mem2,rs1)       vis_m2r_2(ld, mem1, mem2, rs1)
#define vis_st32(rs1,mem)               vis_r2m(st, rs1, mem)
#define vis_st32_2(rs1,mem1,mem2)       vis_r2m_2(st, rs1, mem1, mem2)
#define vis_ld64(mem,rs1)               vis_m2r(ldd, mem, rs1)
#define vis_ld64_2(mem1,mem2,rs1)       vis_m2r_2(ldd, mem1, mem2, rs1)
#define vis_st64(rs1,mem)               vis_r2m(std, rs1, mem)
#define vis_st64_2(rs1,mem1,mem2)       vis_r2m_2(std, rs1, mem1, mem2)

/* 16 and 32 bit partitioned addition and subtraction.  The normal
 * versions perform 4 16-bit or 2 32-bit additions or subtractions.
 * The 's' versions perform 2 16-bit or 1 32-bit additions or
 * subtractions.
 */

#define vis_padd16(rs1,rs2,rd)          vis_dd2d(0x50, rs1, rs2, rd)
#define vis_padd16s(rs1,rs2,rd)         vis_ss2s(0x51, rs1, rs2, rd)
#define vis_padd32(rs1,rs2,rd)          vis_dd2d(0x52, rs1, rs2, rd)
#define vis_padd32s(rs1,rs2,rd)         vis_ss2s(0x53, rs1, rs2, rd)
#define vis_psub16(rs1,rs2,rd)          vis_dd2d(0x54, rs1, rs2, rd)
#define vis_psub16s(rs1,rs2,rd)         vis_ss2s(0x55, rs1, rs2, rd)
#define vis_psub32(rs1,rs2,rd)          vis_dd2d(0x56, rs1, rs2, rd)
#define vis_psub32s(rs1,rs2,rd)         vis_ss2s(0x57, rs1, rs2, rd)

/* Pixel formatting instructions.  */

#define vis_pack16(rs2,rd)              vis_d2s( 0x3b,      rs2, rd)
#define vis_pack32(rs1,rs2,rd)          vis_dd2d(0x3a, rs1, rs2, rd)
#define vis_packfix(rs2,rd)             vis_d2s( 0x3d,      rs2, rd)
#define vis_expand(rs2,rd)              vis_s2d( 0x4d,      rs2, rd)
#define vis_pmerge(rs1,rs2,rd)          vis_ss2d(0x4b, rs1, rs2, rd)

/* Partitioned multiply instructions.  */

#define vis_mul8x16(rs1,rs2,rd)         vis_sd2d(0x31, rs1, rs2, rd)
#define vis_mul8x16au(rs1,rs2,rd)       vis_ss2d(0x33, rs1, rs2, rd)
#define vis_mul8x16al(rs1,rs2,rd)       vis_ss2d(0x35, rs1, rs2, rd)
#define vis_mul8sux16(rs1,rs2,rd)       vis_dd2d(0x36, rs1, rs2, rd)
#define vis_mul8ulx16(rs1,rs2,rd)       vis_dd2d(0x37, rs1, rs2, rd)
#define vis_muld8sux16(rs1,rs2,rd)      vis_ss2d(0x38, rs1, rs2, rd)
#define vis_muld8ulx16(rs1,rs2,rd)      vis_ss2d(0x39, rs1, rs2, rd)

/* Alignment instructions.  */

static inline const void *vis_alignaddr(const void *ptr)
{
        __asm__ volatile("alignaddr %0, %%g0, %0"
                             : "=&r" (ptr)
                             : "0" (ptr));

        return ptr;
}

static inline void vis_alignaddr_g0(void *ptr)
{
        __asm__ volatile("alignaddr %0, %%g0, %%g0"
                             : : "r" (ptr));
}

#define vis_faligndata(rs1,rs2,rd)        vis_dd2d(0x48, rs1, rs2, rd)

/* Logical operate instructions.  */

#define vis_fzero(rd)                   vis_d(   0x60,           rd)
#define vis_fzeros(rd)                  vis_s(   0x61,           rd)
#define vis_fone(rd)                    vis_d(   0x7e,           rd)
#define vis_fones(rd)                   vis_s(   0x7f,           rd)
#define vis_src1(rs1,rd)                vis_d12d(0x74, rs1,      rd)
#define vis_src1s(rs1,rd)               vis_s12s(0x75, rs1,      rd)
#define vis_src2(rs2,rd)                vis_d22d(0x78,      rs2, rd)
#define vis_src2s(rs2,rd)               vis_s22s(0x79,      rs2, rd)
#define vis_not1(rs1,rd)                vis_d12d(0x6a, rs1,      rd)
#define vis_not1s(rs1,rd)               vis_s12s(0x6b, rs1,      rd)
#define vis_not2(rs2,rd)                vis_d22d(0x66,      rs2, rd)
#define vis_not2s(rs2,rd)               vis_s22s(0x67,      rs2, rd)
#define vis_or(rs1,rs2,rd)              vis_dd2d(0x7c, rs1, rs2, rd)
#define vis_ors(rs1,rs2,rd)             vis_ss2s(0x7d, rs1, rs2, rd)
#define vis_nor(rs1,rs2,rd)             vis_dd2d(0x62, rs1, rs2, rd)
#define vis_nors(rs1,rs2,rd)            vis_ss2s(0x63, rs1, rs2, rd)
#define vis_and(rs1,rs2,rd)             vis_dd2d(0x70, rs1, rs2, rd)
#define vis_ands(rs1,rs2,rd)            vis_ss2s(0x71, rs1, rs2, rd)
#define vis_nand(rs1,rs2,rd)            vis_dd2d(0x6e, rs1, rs2, rd)
#define vis_nands(rs1,rs2,rd)           vis_ss2s(0x6f, rs1, rs2, rd)
#define vis_xor(rs1,rs2,rd)             vis_dd2d(0x6c, rs1, rs2, rd)
#define vis_xors(rs1,rs2,rd)            vis_ss2s(0x6d, rs1, rs2, rd)
#define vis_xnor(rs1,rs2,rd)            vis_dd2d(0x72, rs1, rs2, rd)
#define vis_xnors(rs1,rs2,rd)           vis_ss2s(0x73, rs1, rs2, rd)
#define vis_ornot1(rs1,rs2,rd)          vis_dd2d(0x7a, rs1, rs2, rd)
#define vis_ornot1s(rs1,rs2,rd)         vis_ss2s(0x7b, rs1, rs2, rd)
#define vis_ornot2(rs1,rs2,rd)          vis_dd2d(0x76, rs1, rs2, rd)
#define vis_ornot2s(rs1,rs2,rd)         vis_ss2s(0x77, rs1, rs2, rd)
#define vis_andnot1(rs1,rs2,rd)         vis_dd2d(0x68, rs1, rs2, rd)
#define vis_andnot1s(rs1,rs2,rd)        vis_ss2s(0x69, rs1, rs2, rd)
#define vis_andnot2(rs1,rs2,rd)         vis_dd2d(0x64, rs1, rs2, rd)
#define vis_andnot2s(rs1,rs2,rd)        vis_ss2s(0x65, rs1, rs2, rd)

/* Pixel component distance.  */

#define vis_pdist(rs1,rs2,rd)           vis_dd2d(0x3e, rs1, rs2, rd)

#endif /* AVCODEC_SPARC_VIS_H */
