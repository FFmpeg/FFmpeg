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

#ifndef CHECKASM_FUNCTION_H
#define CHECKASM_FUNCTION_H

#include "checkasm/checkasm.h"
#include "stats.h"

typedef enum CheckasmFuncState {
    CHECKASM_FUNC_OK,
    CHECKASM_FUNC_FAILED,
    CHECKASM_FUNC_CRASHED, /* signal handler triggered */
} CheckasmFuncState;

typedef struct CheckasmFuncVersion {
    struct CheckasmFuncVersion *next;
    const CheckasmCpuInfo      *cpu;
    char                       *suffix; /* optional custom suffix */
    CheckasmKey                 key;
    CheckasmMeasurement         cycles;
    CheckasmFuncState           state;
} CheckasmFuncVersion;

typedef struct CheckasmFunc {
    struct CheckasmFunc *child[2];
    struct CheckasmFunc *prev; /* previous function in current report group */
    CheckasmFuncVersion  versions;
    const char          *test_name;
    char                *report_name;
    int                  report_idx; /* when was this function last reported? */
    uint8_t              color; /* 0 = red, 1 = black */
    char                 name[];
} CheckasmFunc;

typedef struct CheckasmFuncTree {
    CheckasmFunc *root;
} CheckasmFuncTree;

/* Free all resources associated with a function tree and set it to {0}. */
void checkasm_func_tree_uninit(CheckasmFuncTree *tree);

/* Get the node for a given function name, creating it if it doesn't exist. */
CheckasmFunc *checkasm_func_get(CheckasmFuncTree *tree, const char *name);

#endif /* CHECKASM_FUNCTION_H */
