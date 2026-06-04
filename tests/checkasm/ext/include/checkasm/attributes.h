/*
 * Copyright © 2025, Niklas Haas
 * Copyright © 2018, VideoLAN and dav1d authors
 * Copyright © 2018, Two Orioles, LLC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file attributes.h
 * @brief Platform and compiler attribute macros
 *
 * This header defines platform-specific compiler attributes used throughout
 * the checkasm API for features like printf format checking and symbol visibility.
 */

#ifndef CHECKASM_ATTRIBUTES_H
#define CHECKASM_ATTRIBUTES_H

#include <stddef.h>

/**
 * @def CHECKASM_PRINTF(fmt, attr)
 * @brief Printf-style format string checking attribute
 *
 * Enables compile-time checking of printf-style format strings and arguments
 * when supported by the compiler. This helps catch format string errors at
 * compile time rather than runtime.
 *
 * @param fmt Position of the format string parameter (1-indexed)
 * @param attr Position of the first variadic argument (1-indexed)
 */
#ifndef CHECKASM_PRINTF
  #ifdef __GNUC__
    #if defined(__MINGW32__) && !defined(__clang__)
      #define CHECKASM_PRINTF(fmt, attr)                                                 \
          __attribute__((__format__(__gnu_printf__, fmt, attr)))
    #else
      #define CHECKASM_PRINTF(fmt, attr)                                                 \
          __attribute__((__format__(__printf__, fmt, attr)))
    #endif
  #else
    #define CHECKASM_PRINTF(fmt, attr)
  #endif
#endif

/**
 * @def CHECKASM_API
 * @brief Symbol visibility attribute for public API functions
 *
 * Marks functions and variables as part of the public checkasm API. This
 * ensures proper symbol export/import behavior across different platforms
 * and build configurations.
 *
 * @note All public checkasm API functions are marked with this attribute.
 */
#ifndef CHECKASM_API
  #ifdef _WIN32
    #ifdef CHECKASM_BUILDING_DLL
      #define CHECKASM_API __declspec(dllexport)
    #else
      #define CHECKASM_API
    #endif
  #elif defined(__OS2__)
    #define CHECKASM_API __declspec(dllexport)
  #else
    #if __GNUC__ >= 4
      #define CHECKASM_API __attribute__((visibility("default")))
    #else
      #define CHECKASM_API
    #endif
  #endif
#endif

#endif /* CHECKASM_ATTRIBUTES_H */
