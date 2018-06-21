// Avisynth C Interface Version 0.20
// Copyright 2003 Kevin Atkinson

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA, or visit
// http://www.gnu.org/copyleft/gpl.html .
//
// As a special exception, I give you permission to link to the
// Avisynth C interface with independent modules that communicate with
// the Avisynth C interface solely through the interfaces defined in
// avisynth_c.h, regardless of the license terms of these independent
// modules, and to copy and distribute the resulting combined work
// under terms of your choice, provided that every copy of the
// combined work is accompanied by a complete copy of the source code
// of the Avisynth C interface and Avisynth itself (with the version
// used to produce the combined work), being distributed under the
// terms of the GNU General Public License plus this exception.  An
// independent module is a module which is not derived from or based
// on Avisynth C Interface, such as 3rd-party filters, import and
// export plugins, or graphical user interfaces.

#ifndef AVS_CAPI_H
#define AVS_CAPI_H

#ifdef __cplusplus
#  define EXTERN_C extern "C"
#else
#  define EXTERN_C
#endif

#ifndef AVSC_USE_STDCALL
#  define AVSC_CC __cdecl
#else
#  define AVSC_CC __stdcall
#endif

#define AVSC_INLINE static __inline

#ifdef BUILDING_AVSCORE
#  define AVSC_EXPORT EXTERN_C
#  define AVSC_API(ret, name) EXTERN_C __declspec(dllexport) ret AVSC_CC name
#else
#  define AVSC_EXPORT EXTERN_C __declspec(dllexport)
#  ifndef AVSC_NO_DECLSPEC
#    define AVSC_API(ret, name) EXTERN_C __declspec(dllimport) ret AVSC_CC name
#  else
#    define AVSC_API(ret, name) typedef ret (AVSC_CC *name##_func)
#  endif
#endif

#endif //AVS_CAPI_H
