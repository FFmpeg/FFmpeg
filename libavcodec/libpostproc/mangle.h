/* mangle.h - This file has some CPP macros to deal with different symbol
 * mangling across binary formats.
 * (c)2002 by Felix Buenemann <atmosfear at users.sourceforge.net>
 * File licensed under the GPL, see http://www.fsf.org/ for more info.
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
#else
#define MANGLE(a) #a
#endif
#endif

#endif /* !__MANGLE_H */

