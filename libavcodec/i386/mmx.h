/*	mmx.h

	MultiMedia eXtensions GCC interface library for IA32.

	To use this library, simply include this header file
	and compile with GCC.  You MUST have inlining enabled
	in order for mmx_ok() to work; this can be done by
	simply using -O on the GCC command line.

	Compiling with -DMMX_TRACE will cause detailed trace
	output to be sent to stderr for each mmx operation.
	This adds lots of code, and obviously slows execution to
	a crawl, but can be very useful for debugging.

	THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY
	EXPRESS OR IMPLIED WARRANTIES, INCLUDING, WITHOUT
	LIMITATION, THE IMPLIED WARRANTIES OF MERCHANTABILITY
	AND FITNESS FOR ANY PARTICULAR PURPOSE.

	1997-99 by H. Dietz and R. Fisher

 Notes:
	It appears that the latest gas has the pand problem fixed, therefore
	  I'll undefine BROKEN_PAND by default.
*/

#ifndef _MMX_H
#define _MMX_H


/*	Warning:  at this writing, the version of GAS packaged
	with most Linux distributions does not handle the
	parallel AND operation mnemonic correctly.  If the
	symbol BROKEN_PAND is defined, a slower alternative
	coding will be used.  If execution of mmxtest results
	in an illegal instruction fault, define this symbol.
*/
#undef	BROKEN_PAND


/*	The type of an value that fits in an MMX register
	(note that long long constant values MUST be suffixed
	 by LL and unsigned long long values by ULL, lest
	 they be truncated by the compiler)
*/
typedef	union {
	long long		q;	/* Quadword (64-bit) value */
	unsigned long long	uq;	/* Unsigned Quadword */
	int			d[2];	/* 2 Doubleword (32-bit) values */
	unsigned int		ud[2];	/* 2 Unsigned Doubleword */
	short			w[4];	/* 4 Word (16-bit) values */
	unsigned short		uw[4];	/* 4 Unsigned Word */
	char			b[8];	/* 8 Byte (8-bit) values */
	unsigned char		ub[8];	/* 8 Unsigned Byte */
	float			s[2];	/* Single-precision (32-bit) value */
} __attribute__ ((aligned (8))) mmx_t;	/* On an 8-byte (64-bit) boundary */


/*	Helper functions for the instruction macros that follow...
	(note that memory-to-register, m2r, instructions are nearly
	 as efficient as register-to-register, r2r, instructions;
	 however, memory-to-memory instructions are really simulated
	 as a convenience, and are only 1/3 as efficient)
*/
#ifdef	MMX_TRACE

/*	Include the stuff for printing a trace to stderr...
*/

#include <stdio.h>

#define	mmx_i2r(op, imm, reg) \
	{ \
		mmx_t mmx_trace; \
		mmx_trace.uq = (imm); \
		fprintf(stderr, #op "_i2r(" #imm "=0x%08x%08x, ", \
			mmx_trace.d[1], mmx_trace.d[0]); \
		__asm__ __volatile__ ("movq %%" #reg ", %0" \
				      : "=X" (mmx_trace) \
				      : /* nothing */ ); \
		fprintf(stderr, #reg "=0x%08x%08x) => ", \
			mmx_trace.d[1], mmx_trace.d[0]); \
		__asm__ __volatile__ (#op " %0, %%" #reg \
				      : /* nothing */ \
				      : "X" (imm)); \
		__asm__ __volatile__ ("movq %%" #reg ", %0" \
				      : "=X" (mmx_trace) \
				      : /* nothing */ ); \
		fprintf(stderr, #reg "=0x%08x%08x\n", \
			mmx_trace.d[1], mmx_trace.d[0]); \
	}

#define	mmx_m2r(op, mem, reg) \
	{ \
		mmx_t mmx_trace; \
		mmx_trace = (mem); \
		fprintf(stderr, #op "_m2r(" #mem "=0x%08x%08x, ", \
			mmx_trace.d[1], mmx_trace.d[0]); \
		__asm__ __volatile__ ("movq %%" #reg ", %0" \
				      : "=X" (mmx_trace) \
				      : /* nothing */ ); \
		fprintf(stderr, #reg "=0x%08x%08x) => ", \
			mmx_trace.d[1], mmx_trace.d[0]); \
		__asm__ __volatile__ (#op " %0, %%" #reg \
				      : /* nothing */ \
				      : "X" (mem)); \
		__asm__ __volatile__ ("movq %%" #reg ", %0" \
				      : "=X" (mmx_trace) \
				      : /* nothing */ ); \
		fprintf(stderr, #reg "=0x%08x%08x\n", \
			mmx_trace.d[1], mmx_trace.d[0]); \
	}

#define	mmx_r2m(op, reg, mem) \
	{ \
		mmx_t mmx_trace; \
		__asm__ __volatile__ ("movq %%" #reg ", %0" \
				      : "=X" (mmx_trace) \
				      : /* nothing */ ); \
		fprintf(stderr, #op "_r2m(" #reg "=0x%08x%08x, ", \
			mmx_trace.d[1], mmx_trace.d[0]); \
		mmx_trace = (mem); \
		fprintf(stderr, #mem "=0x%08x%08x) => ", \
			mmx_trace.d[1], mmx_trace.d[0]); \
		__asm__ __volatile__ (#op " %%" #reg ", %0" \
				      : "=X" (mem) \
				      : /* nothing */ ); \
		mmx_trace = (mem); \
		fprintf(stderr, #mem "=0x%08x%08x\n", \
			mmx_trace.d[1], mmx_trace.d[0]); \
	}

#define	mmx_r2r(op, regs, regd) \
	{ \
		mmx_t mmx_trace; \
		__asm__ __volatile__ ("movq %%" #regs ", %0" \
				      : "=X" (mmx_trace) \
				      : /* nothing */ ); \
		fprintf(stderr, #op "_r2r(" #regs "=0x%08x%08x, ", \
			mmx_trace.d[1], mmx_trace.d[0]); \
		__asm__ __volatile__ ("movq %%" #regd ", %0" \
				      : "=X" (mmx_trace) \
				      : /* nothing */ ); \
		fprintf(stderr, #regd "=0x%08x%08x) => ", \
			mmx_trace.d[1], mmx_trace.d[0]); \
		__asm__ __volatile__ (#op " %" #regs ", %" #regd); \
		__asm__ __volatile__ ("movq %%" #regd ", %0" \
				      : "=X" (mmx_trace) \
				      : /* nothing */ ); \
		fprintf(stderr, #regd "=0x%08x%08x\n", \
			mmx_trace.d[1], mmx_trace.d[0]); \
	}

#define	mmx_m2m(op, mems, memd) \
	{ \
		mmx_t mmx_trace; \
		mmx_trace = (mems); \
		fprintf(stderr, #op "_m2m(" #mems "=0x%08x%08x, ", \
			mmx_trace.d[1], mmx_trace.d[0]); \
		mmx_trace = (memd); \
		fprintf(stderr, #memd "=0x%08x%08x) => ", \
			mmx_trace.d[1], mmx_trace.d[0]); \
		__asm__ __volatile__ ("movq %0, %%mm0\n\t" \
				      #op " %1, %%mm0\n\t" \
				      "movq %%mm0, %0" \
				      : "=X" (memd) \
				      : "X" (mems)); \
		mmx_trace = (memd); \
		fprintf(stderr, #memd "=0x%08x%08x\n", \
			mmx_trace.d[1], mmx_trace.d[0]); \
	}

#else

/*	These macros are a lot simpler without the tracing...
*/

#define	mmx_i2r(op, imm, reg) \
	__asm__ __volatile__ (#op " %0, %%" #reg \
			      : /* nothing */ \
			      : "i" (imm) )

#define	mmx_m2r(op, mem, reg) \
	__asm__ __volatile__ (#op " %0, %%" #reg \
			      : /* nothing */ \
			      : "m" (mem))

#define	mmx_r2m(op, reg, mem) \
	__asm__ __volatile__ (#op " %%" #reg ", %0" \
			      : "=m" (mem) \
			      : /* nothing */ )

#define	mmx_r2r(op, regs, regd) \
	__asm__ __volatile__ (#op " %" #regs ", %" #regd)

#define	mmx_m2m(op, mems, memd) \
	__asm__ __volatile__ ("movq %0, %%mm0\n\t" \
			      #op " %1, %%mm0\n\t" \
			      "movq %%mm0, %0" \
			      : "=m" (memd) \
			      : "m" (mems))

#endif


/*	1x64 MOVe Quadword
	(this is both a load and a store...
	 in fact, it is the only way to store)
*/
#define	movq_m2r(var, reg)	mmx_m2r(movq, var, reg)
#define	movq_r2m(reg, var)	mmx_r2m(movq, reg, var)
#define	movq_r2r(regs, regd)	mmx_r2r(movq, regs, regd)
#define	movq(vars, vard) \
	__asm__ __volatile__ ("movq %1, %%mm0\n\t" \
			      "movq %%mm0, %0" \
			      : "=X" (vard) \
			      : "X" (vars))


/*	1x32 MOVe Doubleword
	(like movq, this is both load and store...
	 but is most useful for moving things between
	 mmx registers and ordinary registers)
*/
#define	movd_m2r(var, reg)	mmx_m2r(movd, var, reg)
#define	movd_r2m(reg, var)	mmx_r2m(movd, reg, var)
#define	movd_r2r(regs, regd)	mmx_r2r(movd, regs, regd)
#define	movd(vars, vard) \
	__asm__ __volatile__ ("movd %1, %%mm0\n\t" \
			      "movd %%mm0, %0" \
			      : "=X" (vard) \
			      : "X" (vars))


/*	2x32, 4x16, and 8x8 Parallel ADDs
*/
#define	paddd_m2r(var, reg)	mmx_m2r(paddd, var, reg)
#define	paddd_r2r(regs, regd)	mmx_r2r(paddd, regs, regd)
#define	paddd(vars, vard)	mmx_m2m(paddd, vars, vard)

#define	paddw_m2r(var, reg)	mmx_m2r(paddw, var, reg)
#define	paddw_r2r(regs, regd)	mmx_r2r(paddw, regs, regd)
#define	paddw(vars, vard)	mmx_m2m(paddw, vars, vard)

#define	paddb_m2r(var, reg)	mmx_m2r(paddb, var, reg)
#define	paddb_r2r(regs, regd)	mmx_r2r(paddb, regs, regd)
#define	paddb(vars, vard)	mmx_m2m(paddb, vars, vard)


/*	4x16 and 8x8 Parallel ADDs using Saturation arithmetic
*/
#define	paddsw_m2r(var, reg)	mmx_m2r(paddsw, var, reg)
#define	paddsw_r2r(regs, regd)	mmx_r2r(paddsw, regs, regd)
#define	paddsw(vars, vard)	mmx_m2m(paddsw, vars, vard)

#define	paddsb_m2r(var, reg)	mmx_m2r(paddsb, var, reg)
#define	paddsb_r2r(regs, regd)	mmx_r2r(paddsb, regs, regd)
#define	paddsb(vars, vard)	mmx_m2m(paddsb, vars, vard)


/*	4x16 and 8x8 Parallel ADDs using Unsigned Saturation arithmetic
*/
#define	paddusw_m2r(var, reg)	mmx_m2r(paddusw, var, reg)
#define	paddusw_r2r(regs, regd)	mmx_r2r(paddusw, regs, regd)
#define	paddusw(vars, vard)	mmx_m2m(paddusw, vars, vard)

#define	paddusb_m2r(var, reg)	mmx_m2r(paddusb, var, reg)
#define	paddusb_r2r(regs, regd)	mmx_r2r(paddusb, regs, regd)
#define	paddusb(vars, vard)	mmx_m2m(paddusb, vars, vard)


/*	2x32, 4x16, and 8x8 Parallel SUBs
*/
#define	psubd_m2r(var, reg)	mmx_m2r(psubd, var, reg)
#define	psubd_r2r(regs, regd)	mmx_r2r(psubd, regs, regd)
#define	psubd(vars, vard)	mmx_m2m(psubd, vars, vard)

#define	psubw_m2r(var, reg)	mmx_m2r(psubw, var, reg)
#define	psubw_r2r(regs, regd)	mmx_r2r(psubw, regs, regd)
#define	psubw(vars, vard)	mmx_m2m(psubw, vars, vard)

#define	psubb_m2r(var, reg)	mmx_m2r(psubb, var, reg)
#define	psubb_r2r(regs, regd)	mmx_r2r(psubb, regs, regd)
#define	psubb(vars, vard)	mmx_m2m(psubb, vars, vard)


/*	4x16 and 8x8 Parallel SUBs using Saturation arithmetic
*/
#define	psubsw_m2r(var, reg)	mmx_m2r(psubsw, var, reg)
#define	psubsw_r2r(regs, regd)	mmx_r2r(psubsw, regs, regd)
#define	psubsw(vars, vard)	mmx_m2m(psubsw, vars, vard)

#define	psubsb_m2r(var, reg)	mmx_m2r(psubsb, var, reg)
#define	psubsb_r2r(regs, regd)	mmx_r2r(psubsb, regs, regd)
#define	psubsb(vars, vard)	mmx_m2m(psubsb, vars, vard)


/*	4x16 and 8x8 Parallel SUBs using Unsigned Saturation arithmetic
*/
#define	psubusw_m2r(var, reg)	mmx_m2r(psubusw, var, reg)
#define	psubusw_r2r(regs, regd)	mmx_r2r(psubusw, regs, regd)
#define	psubusw(vars, vard)	mmx_m2m(psubusw, vars, vard)

#define	psubusb_m2r(var, reg)	mmx_m2r(psubusb, var, reg)
#define	psubusb_r2r(regs, regd)	mmx_r2r(psubusb, regs, regd)
#define	psubusb(vars, vard)	mmx_m2m(psubusb, vars, vard)


/*	4x16 Parallel MULs giving Low 4x16 portions of results
*/
#define	pmullw_m2r(var, reg)	mmx_m2r(pmullw, var, reg)
#define	pmullw_r2r(regs, regd)	mmx_r2r(pmullw, regs, regd)
#define	pmullw(vars, vard)	mmx_m2m(pmullw, vars, vard)


/*	4x16 Parallel MULs giving High 4x16 portions of results
*/
#define	pmulhw_m2r(var, reg)	mmx_m2r(pmulhw, var, reg)
#define	pmulhw_r2r(regs, regd)	mmx_r2r(pmulhw, regs, regd)
#define	pmulhw(vars, vard)	mmx_m2m(pmulhw, vars, vard)


/*	4x16->2x32 Parallel Mul-ADD
	(muls like pmullw, then adds adjacent 16-bit fields
	 in the multiply result to make the final 2x32 result)
*/
#define	pmaddwd_m2r(var, reg)	mmx_m2r(pmaddwd, var, reg)
#define	pmaddwd_r2r(regs, regd)	mmx_r2r(pmaddwd, regs, regd)
#define	pmaddwd(vars, vard)	mmx_m2m(pmaddwd, vars, vard)


/*	1x64 bitwise AND
*/
#ifdef	BROKEN_PAND
#define	pand_m2r(var, reg) \
	{ \
		mmx_m2r(pandn, (mmx_t) -1LL, reg); \
		mmx_m2r(pandn, var, reg); \
	}
#define	pand_r2r(regs, regd) \
	{ \
		mmx_m2r(pandn, (mmx_t) -1LL, regd); \
		mmx_r2r(pandn, regs, regd) \
	}
#define	pand(vars, vard) \
	{ \
		movq_m2r(vard, mm0); \
		mmx_m2r(pandn, (mmx_t) -1LL, mm0); \
		mmx_m2r(pandn, vars, mm0); \
		movq_r2m(mm0, vard); \
	}
#else
#define	pand_m2r(var, reg)	mmx_m2r(pand, var, reg)
#define	pand_r2r(regs, regd)	mmx_r2r(pand, regs, regd)
#define	pand(vars, vard)	mmx_m2m(pand, vars, vard)
#endif


/*	1x64 bitwise AND with Not the destination
*/
#define	pandn_m2r(var, reg)	mmx_m2r(pandn, var, reg)
#define	pandn_r2r(regs, regd)	mmx_r2r(pandn, regs, regd)
#define	pandn(vars, vard)	mmx_m2m(pandn, vars, vard)


/*	1x64 bitwise OR
*/
#define	por_m2r(var, reg)	mmx_m2r(por, var, reg)
#define	por_r2r(regs, regd)	mmx_r2r(por, regs, regd)
#define	por(vars, vard)	mmx_m2m(por, vars, vard)


/*	1x64 bitwise eXclusive OR
*/
#define	pxor_m2r(var, reg)	mmx_m2r(pxor, var, reg)
#define	pxor_r2r(regs, regd)	mmx_r2r(pxor, regs, regd)
#define	pxor(vars, vard)	mmx_m2m(pxor, vars, vard)


/*	2x32, 4x16, and 8x8 Parallel CoMPare for EQuality
	(resulting fields are either 0 or -1)
*/
#define	pcmpeqd_m2r(var, reg)	mmx_m2r(pcmpeqd, var, reg)
#define	pcmpeqd_r2r(regs, regd)	mmx_r2r(pcmpeqd, regs, regd)
#define	pcmpeqd(vars, vard)	mmx_m2m(pcmpeqd, vars, vard)

#define	pcmpeqw_m2r(var, reg)	mmx_m2r(pcmpeqw, var, reg)
#define	pcmpeqw_r2r(regs, regd)	mmx_r2r(pcmpeqw, regs, regd)
#define	pcmpeqw(vars, vard)	mmx_m2m(pcmpeqw, vars, vard)

#define	pcmpeqb_m2r(var, reg)	mmx_m2r(pcmpeqb, var, reg)
#define	pcmpeqb_r2r(regs, regd)	mmx_r2r(pcmpeqb, regs, regd)
#define	pcmpeqb(vars, vard)	mmx_m2m(pcmpeqb, vars, vard)


/*	2x32, 4x16, and 8x8 Parallel CoMPare for Greater Than
	(resulting fields are either 0 or -1)
*/
#define	pcmpgtd_m2r(var, reg)	mmx_m2r(pcmpgtd, var, reg)
#define	pcmpgtd_r2r(regs, regd)	mmx_r2r(pcmpgtd, regs, regd)
#define	pcmpgtd(vars, vard)	mmx_m2m(pcmpgtd, vars, vard)

#define	pcmpgtw_m2r(var, reg)	mmx_m2r(pcmpgtw, var, reg)
#define	pcmpgtw_r2r(regs, regd)	mmx_r2r(pcmpgtw, regs, regd)
#define	pcmpgtw(vars, vard)	mmx_m2m(pcmpgtw, vars, vard)

#define	pcmpgtb_m2r(var, reg)	mmx_m2r(pcmpgtb, var, reg)
#define	pcmpgtb_r2r(regs, regd)	mmx_r2r(pcmpgtb, regs, regd)
#define	pcmpgtb(vars, vard)	mmx_m2m(pcmpgtb, vars, vard)


/*	1x64, 2x32, and 4x16 Parallel Shift Left Logical
*/
#define	psllq_i2r(imm, reg)	mmx_i2r(psllq, imm, reg)
#define	psllq_m2r(var, reg)	mmx_m2r(psllq, var, reg)
#define	psllq_r2r(regs, regd)	mmx_r2r(psllq, regs, regd)
#define	psllq(vars, vard)	mmx_m2m(psllq, vars, vard)

#define	pslld_i2r(imm, reg)	mmx_i2r(pslld, imm, reg)
#define	pslld_m2r(var, reg)	mmx_m2r(pslld, var, reg)
#define	pslld_r2r(regs, regd)	mmx_r2r(pslld, regs, regd)
#define	pslld(vars, vard)	mmx_m2m(pslld, vars, vard)

#define	psllw_i2r(imm, reg)	mmx_i2r(psllw, imm, reg)
#define	psllw_m2r(var, reg)	mmx_m2r(psllw, var, reg)
#define	psllw_r2r(regs, regd)	mmx_r2r(psllw, regs, regd)
#define	psllw(vars, vard)	mmx_m2m(psllw, vars, vard)


/*	1x64, 2x32, and 4x16 Parallel Shift Right Logical
*/
#define	psrlq_i2r(imm, reg)	mmx_i2r(psrlq, imm, reg)
#define	psrlq_m2r(var, reg)	mmx_m2r(psrlq, var, reg)
#define	psrlq_r2r(regs, regd)	mmx_r2r(psrlq, regs, regd)
#define	psrlq(vars, vard)	mmx_m2m(psrlq, vars, vard)

#define	psrld_i2r(imm, reg)	mmx_i2r(psrld, imm, reg)
#define	psrld_m2r(var, reg)	mmx_m2r(psrld, var, reg)
#define	psrld_r2r(regs, regd)	mmx_r2r(psrld, regs, regd)
#define	psrld(vars, vard)	mmx_m2m(psrld, vars, vard)

#define	psrlw_i2r(imm, reg)	mmx_i2r(psrlw, imm, reg)
#define	psrlw_m2r(var, reg)	mmx_m2r(psrlw, var, reg)
#define	psrlw_r2r(regs, regd)	mmx_r2r(psrlw, regs, regd)
#define	psrlw(vars, vard)	mmx_m2m(psrlw, vars, vard)


/*	2x32 and 4x16 Parallel Shift Right Arithmetic
*/
#define	psrad_i2r(imm, reg)	mmx_i2r(psrad, imm, reg)
#define	psrad_m2r(var, reg)	mmx_m2r(psrad, var, reg)
#define	psrad_r2r(regs, regd)	mmx_r2r(psrad, regs, regd)
#define	psrad(vars, vard)	mmx_m2m(psrad, vars, vard)

#define	psraw_i2r(imm, reg)	mmx_i2r(psraw, imm, reg)
#define	psraw_m2r(var, reg)	mmx_m2r(psraw, var, reg)
#define	psraw_r2r(regs, regd)	mmx_r2r(psraw, regs, regd)
#define	psraw(vars, vard)	mmx_m2m(psraw, vars, vard)


/*	2x32->4x16 and 4x16->8x8 PACK and Signed Saturate
	(packs source and dest fields into dest in that order)
*/
#define	packssdw_m2r(var, reg)	mmx_m2r(packssdw, var, reg)
#define	packssdw_r2r(regs, regd) mmx_r2r(packssdw, regs, regd)
#define	packssdw(vars, vard)	mmx_m2m(packssdw, vars, vard)

#define	packsswb_m2r(var, reg)	mmx_m2r(packsswb, var, reg)
#define	packsswb_r2r(regs, regd) mmx_r2r(packsswb, regs, regd)
#define	packsswb(vars, vard)	mmx_m2m(packsswb, vars, vard)


/*	4x16->8x8 PACK and Unsigned Saturate
	(packs source and dest fields into dest in that order)
*/
#define	packuswb_m2r(var, reg)	mmx_m2r(packuswb, var, reg)
#define	packuswb_r2r(regs, regd) mmx_r2r(packuswb, regs, regd)
#define	packuswb(vars, vard)	mmx_m2m(packuswb, vars, vard)


/*	2x32->1x64, 4x16->2x32, and 8x8->4x16 UNPaCK Low
	(interleaves low half of dest with low half of source
	 as padding in each result field)
*/
#define	punpckldq_m2r(var, reg)	mmx_m2r(punpckldq, var, reg)
#define	punpckldq_r2r(regs, regd) mmx_r2r(punpckldq, regs, regd)
#define	punpckldq(vars, vard)	mmx_m2m(punpckldq, vars, vard)

#define	punpcklwd_m2r(var, reg)	mmx_m2r(punpcklwd, var, reg)
#define	punpcklwd_r2r(regs, regd) mmx_r2r(punpcklwd, regs, regd)
#define	punpcklwd(vars, vard)	mmx_m2m(punpcklwd, vars, vard)

#define	punpcklbw_m2r(var, reg)	mmx_m2r(punpcklbw, var, reg)
#define	punpcklbw_r2r(regs, regd) mmx_r2r(punpcklbw, regs, regd)
#define	punpcklbw(vars, vard)	mmx_m2m(punpcklbw, vars, vard)


/*	2x32->1x64, 4x16->2x32, and 8x8->4x16 UNPaCK High
	(interleaves high half of dest with high half of source
	 as padding in each result field)
*/
#define	punpckhdq_m2r(var, reg)	mmx_m2r(punpckhdq, var, reg)
#define	punpckhdq_r2r(regs, regd) mmx_r2r(punpckhdq, regs, regd)
#define	punpckhdq(vars, vard)	mmx_m2m(punpckhdq, vars, vard)

#define	punpckhwd_m2r(var, reg)	mmx_m2r(punpckhwd, var, reg)
#define	punpckhwd_r2r(regs, regd) mmx_r2r(punpckhwd, regs, regd)
#define	punpckhwd(vars, vard)	mmx_m2m(punpckhwd, vars, vard)

#define	punpckhbw_m2r(var, reg)	mmx_m2r(punpckhbw, var, reg)
#define	punpckhbw_r2r(regs, regd) mmx_r2r(punpckhbw, regs, regd)
#define	punpckhbw(vars, vard)	mmx_m2m(punpckhbw, vars, vard)


/*	Empty MMx State
	(used to clean-up when going from mmx to float use
	 of the registers that are shared by both; note that
	 there is no float-to-mmx operation needed, because
	 only the float tag word info is corruptible)
*/
#ifdef	MMX_TRACE

#define	emms() \
	{ \
		fprintf(stderr, "emms()\n"); \
		__asm__ __volatile__ ("emms"); \
	}

#else

#define	emms()			__asm__ __volatile__ ("emms")

#endif

#endif

