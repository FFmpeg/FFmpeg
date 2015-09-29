/*
 * Correctly setup required msvc compatibility options based on which native
 * Windows compiler is in use.
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

#ifndef SMP_COMPAT_H
#define SMP_COMPAT_H

#ifdef _MSC_VER
#   include <crtversion.h>
#   if _VC_CRT_MAJOR_VERSION >= 14
#       pragma comment(lib, "legacy_stdio_definitions.lib")
#   else
#       include <../compat/msvcrt/snprintf.h>
#       define strtod avpriv_strtod
#       define strtoll _strtoi64
#   endif
#endif

#endif /* SMP_COMPAT_H */
