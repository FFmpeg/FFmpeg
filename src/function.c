/*
 * Copyright © 2025, Niklas Haas
 * Copyright © 2018, VideoLAN and dav1d authors
 * Copyright © 2018, Two Orioles, LLC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>

#include "function.h"
#include "internal.h"

/* Deallocate a tree */
static void func_uninit(CheckasmFunc *const f)
{
    if (!f)
        return;

    CheckasmFuncVersion *v = f->versions.next;
    while (v) {
        CheckasmFuncVersion *next = v->next;
        free(v->suffix);
        free(v);
        v = next;
    }

    CheckasmFunc *const left  = f->child[0];
    CheckasmFunc *const right = f->child[1];
    free(f->report_name);
    free(f);

    func_uninit(right);
    func_uninit(left);
}

void checkasm_func_tree_uninit(CheckasmFuncTree *tree)
{
    func_uninit(tree->root);
    memset(tree, 0, sizeof(*tree));
}

#define is_digit(x) ((x) >= '0' && (x) <= '9')

/* ASCIIbetical sort except preserving natural order for numbers */
static int cmp_func_names(const char *a, const char *b)
{
    const char *const start = a;

    int ascii_diff, digit_diff;
    for (; !(ascii_diff = *(const unsigned char *) a - *(const unsigned char *) b) && *a;
         a++, b++)
        ;
    for (; is_digit(*a) && is_digit(*b); a++, b++)
        ;

    if (a > start && is_digit(a[-1]) && (digit_diff = is_digit(*a) - is_digit(*b)))
        return digit_diff;

    return ascii_diff;
}

/* Perform a tree rotation in the specified direction and return the new root */
static CheckasmFunc *tree_rotate(CheckasmFunc *const f, const int dir)
{
    CheckasmFunc *const r = f->child[dir ^ 1];

    f->child[dir ^ 1] = r->child[dir];
    r->child[dir]     = f;
    r->color          = f->color;
    f->color          = 0;
    return r;
}

#define is_red(f) ((f) && !(f)->color)

/* Balance a left-leaning red-black tree at the specified node */
static void tree_balance(CheckasmFunc **const root)
{
    CheckasmFunc *const f = *root;

    if (is_red(f->child[0]) && is_red(f->child[1])) {
        f->color ^= 1;
        f->child[0]->color = f->child[1]->color = 1;
    } else if (!is_red(f->child[0]) && is_red(f->child[1]))
        *root = tree_rotate(f, 0); /* Rotate left */
    else if (is_red(f->child[0]) && is_red(f->child[0]->child[0]))
        *root = tree_rotate(f, 1); /* Rotate right */
}

/* Get a node with the specified name, creating it if it doesn't exist; returns
 * 1 if a new node was inserted, 0 otherwise. */
static int func_get(CheckasmFunc **const root, const char *const name,
                    CheckasmFunc **const out_func)
{
    CheckasmFunc *f = *root;
    if (!f) {
        /* Allocate and insert a new node into the tree */
        const size_t name_length = strlen(name) + 1;
        f = checkasm_mallocz(offsetof(CheckasmFunc, name) + name_length);
        memcpy(f->name, name, name_length);
        *out_func = *root = f;
        return 1;
    }

    /* Search the tree for a matching node */
    const int cmp = cmp_func_names(name, f->name);
    if (!cmp) {
        *out_func = f;
        return 0;
    }

    int inserted = func_get(&f->child[cmp > 0], name, out_func);
    if (inserted)
        tree_balance(root); /* Rebalance the tree on the way up */
    return inserted;
}

CheckasmFunc *checkasm_func_get(CheckasmFuncTree *tree, const char *const name)
{
    CheckasmFunc *func     = NULL;
    int           inserted = func_get(&tree->root, name, &func);
    if (inserted)
        tree->root->color = 1; /* Ensure root is black */
    return func;
}
