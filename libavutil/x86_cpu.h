#ifndef AVUTIL_X86CPU_H
#define AVUTIL_X86CPU_H

#ifdef ARCH_X86_64
#  define REG_a "rax"
#  define REG_b "rbx"
#  define REG_c "rcx"
#  define REG_d "rdx"
#  define REG_D "rdi"
#  define REG_S "rsi"
#  define PTR_SIZE "8"

#  define REG_SP "rsp"
#  define REG_BP "rbp"
#  define REGBP   rbp
#  define REGa    rax
#  define REGb    rbx
#  define REGSP   rsp

#else

#  define REG_a "eax"
#  define REG_b "ebx"
#  define REG_c "ecx"
#  define REG_d "edx"
#  define REG_D "edi"
#  define REG_S "esi"
#  define PTR_SIZE "4"

#  define REG_SP "esp"
#  define REG_BP "ebp"
#  define REGBP   ebp
#  define REGa    eax
#  define REGb    ebx
#  define REGSP   esp
#endif

#endif /* AVUTIL_X86CPU_H */
