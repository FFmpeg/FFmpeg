/* strptime.h
 *
 * $Id$
 *
 * Ethereal - Network traffic analyzer
 * By Gerald Combs <gerald@ethereal.com>
 * Copyright 1998 Gerald Combs
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

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
