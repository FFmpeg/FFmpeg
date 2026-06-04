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

#ifndef CHECKASM_PLATFORM_LOONGARCH_H
#define CHECKASM_PLATFORM_LOONGARCH_H

#include "checkasm/attributes.h"

CHECKASM_API void checkasm_checked_call(void *func, ...);

#define checkasm_declare_impl(ret, ...)                                                  \
    ret (*checked_call)(void *, int, int, int, int, int, int, int, __VA_ARGS__, int,     \
                        int, int, int, int, int, int, int, int, int, int, int, int, int, \
                        int, int)                                                        \
        = (ret (*)(void *, int, int, int, int, int, int, int, __VA_ARGS__, int, int,     \
                   int, int, int, int, int, int, int, int, int, int, int, int, int,      \
                   int))(void *) checkasm_checked_call

#define checkasm_call_checked(func, ...)                                                 \
    (checkasm_set_signal_handler_state(1),                                               \
     checked_call(func, 0, 0, 0, 0, 0, 0, 0, __VA_ARGS__, 7, 6, 5, 4, 3, 2, 1, 0, 0, 0,  \
                  0, 0, 0, 0, 0, 0));                                                    \
    checkasm_set_signal_handler_state(0)

#define CHECKASM_ALIGNMENT 16

#endif /* CHECKASM_PLATFORM_LOONGARCH_H */
