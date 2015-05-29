/* Coverity Scan model
*
* Copyright (C) 2014 Red Hat, Inc.
*
* Authors:
* Markus Armbruster <armbru@redhat.com>
* Paolo Bonzini <pbonzini@redhat.com>
*
* This work is licensed under the terms of the GNU GPL, version 2 or, at your
* option, any later version. See the COPYING file in the top-level directory.
*/
/*
* This is the source code for our Coverity user model file. The
* purpose of user models is to increase scanning accuracy by explaining
* code Coverity can't see (out of tree libraries) or doesn't
* sufficiently understand. Better accuracy means both fewer false
* positives and more true defects. Memory leaks in particular.
*
* - A model file can't import any header files. Some built-in primitives are
* available but not wchar_t, NULL etc.
* - Modeling doesn't need full structs and typedefs. Rudimentary structs
* and similar types are sufficient.
* - An uninitialized local variable signifies that the variable could be
* any value.
*
* The model file must be uploaded by an admin in the analysis settings of
* https://scan.coverity.com/projects/54
*
* above text is based on https://github.com/qemu/qemu/blob/master/scripts/coverity-model.c
*/

#define NULL (void *)0

// Based on https://scan.coverity.com/models
void *av_malloc(size_t size) {
    int has_memory;
    __coverity_negative_sink__(size);
    if(has_memory)
        return __coverity_alloc__(size);
    else
        return 0;
}
