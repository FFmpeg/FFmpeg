/*
 * Copyright © 2025, Rémi Denis-Courmont
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
#include "checkasm_config.h"

#include <assert.h>
#include <stdint.h>
#include "checkasm/test.h"
#include "internal.h"

static THREAD_LOCAL uintptr_t *current = NULL;

/* Sets the stack guard up.
 *
 * Note that whilst the canary only uses 2 addresses, it should be padded to a
 * larger size to reduce the risk of corrupting the stack frames of the test
 * cases or the `checkasm` run-time above the canary.
 */
void checkasm_push_stack_guard(uintptr_t guard[2])
{
    uintptr_t cookie = (uintptr_t)(void *)checkasm_push_stack_guard;
    uintptr_t selfref = (uintptr_t)(void *)guard;

    /*
     * NOTE: We CANNOT assert that `current` is null here. If the previous test
     * failed uncleanly, `current` is a stale pointer. As long as we do not
     * dereference it, that is fine.
     */
    guard[0] = cookie ^ selfref;
    guard[1] = selfref;
    current = guard;
    /*
     * In theory, with link-time optimisations and static linking, the compiler
     * could notice that the guard can never validly be overwritten, and elide
     * the validity checks below, or even the memory stores above.
     * This dummy assembler snippet prevents the compiler from making any
     * assumption.
     */
#if defined(__clang__) || !defined(_MSC_VER)
    __asm__ volatile ("# NOTHING HERE" :: "r"(guard) : "memory");
#endif
}

void checkasm_pop_stack_guard(void)
{
    uintptr_t *guard = current;
    uintptr_t cookie = (uintptr_t)(void *)checkasm_push_stack_guard;
    uintptr_t selfref = (uintptr_t)(void *)guard;

    current = NULL;
    assert(guard != NULL);
    if (guard[0] != (cookie ^ selfref) || guard[1] != selfref)
        checkasm_fail_abort("stack clobbered");
}
