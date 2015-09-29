/*
 * MSVC unistd.h compatability header.
 * Copyright (c) 2015 Matthew Oliver
 *
 * This file is part of Shift Media Project.
 *
 * Shift Media Project is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Shift Media Project is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the code; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef _SMP_UNISTD_H_
#define _SMP_UNISTD_H_

#ifndef _MSC_VER
#   include_next <unistd.h>
#else

#ifndef __STRICT_ANSI__

#include <stdlib.h>
//#include <io.h>
#include <process.h>
#include <direct.h>
#include <fcntl.h>

#define __UNISTD_GETOPT__
//#include <getopt.h>
#undef __UNISTD_GETOPT__

#define R_OK    4       /* Test for read permission.  */
#define W_OK    2       /* Test for write permission.  */
//#define   X_OK    1       /* execute permission - unsupported in windows*/
#define F_OK    0       /* Test for existence.  */

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif

#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif

#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

#define srandom srand
#define random rand

#define inline __inline
typedef int mode_t;
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;

#endif /* __STRICT_ANSI__ */

#endif /* _MSC_VER */

#endif /* _SMP_UNISTD_H_ */