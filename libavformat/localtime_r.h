/* Convert a string representation of time to a time value.
   Copyright (C) 1996, 1997, 1998, 1999, 2000 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Ulrich Drepper <drepper@cygnus.com>, 1996.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#ifndef __LOCALTIME_R_H__
#define __LOCALTIME_R_H__

/*
 * Version of "localtime_r()", for the benefit of OSes that don't have it.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#if !defined(HAVE_LOCALTIME_R)
#include <time.h>
/* Approximate localtime_r as best we can in its absence.  */
# define localtime_r my_localtime_r
extern struct tm *localtime_r(const time_t *, struct tm *);
#endif

#endif
