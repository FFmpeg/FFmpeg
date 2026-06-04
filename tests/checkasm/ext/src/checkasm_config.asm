; Copyright © 2025, Martin Storsjo
; All rights reserved.
;
; Redistribution and use in source and binary forms, with or without
; modification, are permitted provided that the following conditions are met:
;
; 1. Redistributions of source code must retain the above copyright notice, this
;    list of conditions and the following disclaimer.
;
; 2. Redistributions in binary form must reproduce the above copyright notice,
;    this list of conditions and the following disclaimer in the documentation
;    and/or other materials provided with the distribution.
;
; THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
; ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
; WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
; DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
; ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
; (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
; ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
; (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
; SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

%ifdef CHECKASM_HAVE_GENERATED_H
%include "checkasm_config_generated.asm"
%endif

%ifndef ARCH_X86_32
    %define ARCH_X86_32 0
    %define ARCH_X86_64 0
    %ifidn __OUTPUT_FORMAT__,win32
        %define ARCH_X86_32 1
        %define PREFIX
    %elifidn __OUTPUT_FORMAT__,win64
        %define ARCH_X86_64 1
    %elifidn __OUTPUT_FORMAT__,elf32
        %define ARCH_X86_32 1
    %elifidn __OUTPUT_FORMAT__,elf64
        %define ARCH_X86_64 1
    %elifidn __OUTPUT_FORMAT__,macho32
        %define ARCH_X86_32 1
        %define PREFIX
    %elifidn __OUTPUT_FORMAT__,macho64
        %define ARCH_X86_64 1
        %define PREFIX
    %elifidn __OUTPUT_FORMAT__,aout
        %define ARCH_X86_32 1
        %define PREFIX
    %elifidn __OUTPUT_FORMAT__,obj
        %define ARCH_X86_32 1
        %define PREFIX
    %elifidn __OUTPUT_FORMAT__,obj2
        %define ARCH_X86_32 1
        %define PREFIX
    %endif
%endif

%ifndef PIC
    %define PIC 1
%endif

%ifndef FORCE_VEX_ENCODING
    %define FORCE_VEX_ENCODING 0
%endif

%ifdef CHECKASM_BUILDING_TESTS
    %define cvisible_for_tests cvisible
%else
    %define cvisible_for_tests cglobal
%endif
