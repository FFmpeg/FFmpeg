/* Cpu detection code, extracted from mmx.h ((c)1997-99 by H. Dietz
   and R. Fisher). Converted to C and improved by Fabrice Bellard */

#include <stdlib.h>
#include "../dsputil.h"

#ifdef ARCH_X86_64
#  define REG_b "rbx"
#  define REG_S "rsi"
#else
#  define REG_b "ebx"
#  define REG_S "esi"
#endif

/* ebx saving is necessary for PIC. gcc seems unable to see it alone */
#define cpuid(index,eax,ebx,ecx,edx)\
    __asm __volatile\
	("mov %%"REG_b", %%"REG_S"\n\t"\
         "cpuid\n\t"\
         "xchg %%"REG_b", %%"REG_S\
         : "=a" (eax), "=S" (ebx),\
           "=c" (ecx), "=d" (edx)\
         : "0" (index));

/* Function to test if multimedia instructions are supported...  */
int mm_support(void)
{
    int rval = 0;
    int eax, ebx, ecx, edx;
    int max_std_level, max_ext_level, std_caps=0, ext_caps=0;
    long a, c;
    
    __asm__ __volatile__ (
                          /* See if CPUID instruction is supported ... */
                          /* ... Get copies of EFLAGS into eax and ecx */
                          "pushf\n\t"
                          "pop %0\n\t"
                          "mov %0, %1\n\t"
                          
                          /* ... Toggle the ID bit in one copy and store */
                          /*     to the EFLAGS reg */
                          "xor $0x200000, %0\n\t"
                          "push %0\n\t"
                          "popf\n\t"
                          
                          /* ... Get the (hopefully modified) EFLAGS */
                          "pushf\n\t"
                          "pop %0\n\t"
                          : "=a" (a), "=c" (c)
                          :
                          : "cc" 
                          );
    
    if (a == c)
        return 0; /* CPUID not supported */

    cpuid(0, max_std_level, ebx, ecx, edx);

    if(max_std_level >= 1){
        cpuid(1, eax, ebx, ecx, std_caps);
        if (std_caps & (1<<23))
            rval |= MM_MMX;
        if (std_caps & (1<<25)) 
            rval |= MM_MMXEXT | MM_SSE;
        if (std_caps & (1<<26)) 
            rval |= MM_SSE2;
    }

    cpuid(0x80000000, max_ext_level, ebx, ecx, edx);

    if(max_ext_level >= 0x80000001){
        cpuid(0x80000001, eax, ebx, ecx, ext_caps);
        if (ext_caps & (1<<31))
            rval |= MM_3DNOW;
        if (ext_caps & (1<<30))
            rval |= MM_3DNOWEXT;
        if (ext_caps & (1<<23))
            rval |= MM_MMX;
    }

    cpuid(0, eax, ebx, ecx, edx);
    if (       ebx == 0x68747541 &&
               edx == 0x69746e65 &&
               ecx == 0x444d4163) {
        /* AMD */
        if(ext_caps & (1<<22))
            rval |= MM_MMXEXT;
    } else if (ebx == 0x746e6543 &&
               edx == 0x48727561 &&
               ecx == 0x736c7561) {  /*  "CentaurHauls" */
        /* VIA C3 */
	if(ext_caps & (1<<24))
	  rval |= MM_MMXEXT;
    } else if (ebx == 0x69727943 &&
               edx == 0x736e4978 &&
               ecx == 0x64616574) {
        /* Cyrix Section */
        /* See if extended CPUID level 80000001 is supported */
        /* The value of CPUID/80000001 for the 6x86MX is undefined
           according to the Cyrix CPU Detection Guide (Preliminary
           Rev. 1.01 table 1), so we'll check the value of eax for
           CPUID/0 to see if standard CPUID level 2 is supported.
           According to the table, the only CPU which supports level
           2 is also the only one which supports extended CPUID levels.
        */
        if (eax < 2) 
            return rval;
        if (ext_caps & (1<<24))
            rval |= MM_MMXEXT;
    }
#if 0
    av_log(NULL, AV_LOG_DEBUG, "%s%s%s%s%s%s\n", 
        (rval&MM_MMX) ? "MMX ":"", 
        (rval&MM_MMXEXT) ? "MMX2 ":"", 
        (rval&MM_SSE) ? "SSE ":"", 
        (rval&MM_SSE2) ? "SSE2 ":"", 
        (rval&MM_3DNOW) ? "3DNow ":"", 
        (rval&MM_3DNOWEXT) ? "3DNowExt ":"");
#endif
    return rval;
}

#ifdef __TEST__
int main ( void )
{
  int mm_flags;
  mm_flags = mm_support();
  printf("mm_support = 0x%08X\n",mm_flags);
  return 0;
}
#endif
