/*
 * mangle.h - This file has some CPP macros to deal with different symbol
 * mangling across binary formats.
 *
 * (c)2002 by Felix Buenemann <atmosfear at users.sourceforge.net>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef __MANGLE_H
#define __MANGLE_H

/* Feel free to add more to the list, eg. a.out IMO */
/* Use rip-relative addressing if compiling PIC code on x86-64. */
#if defined(__CYGWIN__) || defined(__MINGW32__) || defined(__OS2__) || \
   (defined(__OpenBSD__) && !defined(__ELF__))
#if defined(ARCH_X86_64) && defined(PIC)
#define MANGLE(a) "_" #a"(%%rip)"
#else
#define MANGLE(a) "_" #a
#endif
#else
#if defined(ARCH_X86_64) && defined(PIC)
#define MANGLE(a) #a"(%%rip)"
#elif defined(CONFIG_DARWIN)
#define MANGLE(a) "_" #a
#else
#define MANGLE(a) #a
#endif
#endif

#endif /* !__MANGLE_H */

