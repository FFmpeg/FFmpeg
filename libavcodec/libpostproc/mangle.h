/* mangle.h - This file has some CPP macros to deal with different symbol
 * mangling across binary formats.
 * (c)2002 by Felix Buenemann <atmosfear at users.sourceforge.net>
 * File licensed under the GPL, see http://www.fsf.org/ for more info.
 */

#ifndef __MANGLE_H
#define __MANGLE_H

/* Feel free to add more to the list, eg. a.out IMO */
#if defined(__CYGWIN__) || defined(__MINGW32__) || defined(__OS2__) || \
   (defined(__OpenBSD__) && !defined(__ELF__))
#define MANGLE(a) "_" #a
#else
#define MANGLE(a) #a
#endif

#endif /* !__MANGLE_H */

