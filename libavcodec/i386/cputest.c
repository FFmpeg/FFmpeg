/* Cpu detection code, extracted from mmx.h ((c)1997-99 by H. Dietz
   and R. Fisher). Converted to C and improved by Fabrice Bellard */

#include <stdlib.h>
#include "../dsputil.h"

/* ebx saving is necessary for PIC. gcc seems unable to see it alone */
#define cpuid(index,eax,ebx,ecx,edx)\
    __asm __volatile\
	("movl %%ebx, %%esi\n\t"\
         "cpuid\n\t"\
         "xchgl %%ebx, %%esi"\
         : "=a" (eax), "=S" (ebx),\
           "=c" (ecx), "=d" (edx)\
         : "0" (index));

/* Function to test if multimedia instructions are supported...  */
int mm_support(void)
{
    int rval;
    int eax, ebx, ecx, edx;
    
    __asm__ __volatile__ (
                          /* See if CPUID instruction is supported ... */
                          /* ... Get copies of EFLAGS into eax and ecx */
                          "pushf\n\t"
                          "popl %0\n\t"
                          "movl %0, %1\n\t"
                          
                          /* ... Toggle the ID bit in one copy and store */
                          /*     to the EFLAGS reg */
                          "xorl $0x200000, %0\n\t"
                          "push %0\n\t"
                          "popf\n\t"
                          
                          /* ... Get the (hopefully modified) EFLAGS */
                          "pushf\n\t"
                          "popl %0\n\t"
                          : "=a" (eax), "=c" (ecx)
                          :
                          : "cc" 
                          );
    
    if (eax == ecx)
        return 0; /* CPUID not supported */
    
    cpuid(0, eax, ebx, ecx, edx);

    if (ebx == 0x756e6547 &&
        edx == 0x49656e69 &&
        ecx == 0x6c65746e) {
        
        /* intel */
    inteltest:
        cpuid(1, eax, ebx, ecx, edx);
        if ((edx & 0x00800000) == 0)
            return 0;
        rval = MM_MMX;
        if (edx & 0x02000000) 
            rval |= MM_MMXEXT | MM_SSE;
        if (edx & 0x04000000) 
            rval |= MM_SSE2;
        return rval;
    } else if (ebx == 0x68747541 &&
               edx == 0x69746e65 &&
               ecx == 0x444d4163) {
        /* AMD */
        cpuid(0x80000000, eax, ebx, ecx, edx);
        if ((unsigned)eax < 0x80000001)
            goto inteltest;
        cpuid(0x80000001, eax, ebx, ecx, edx);
        if ((edx & 0x00800000) == 0)
            return 0;
        rval = MM_MMX;
        if (edx & 0x80000000)
            rval |= MM_3DNOW;
        if (edx & 0x00400000)
            rval |= MM_MMXEXT;
        return rval;
    } else if (ebx == 0x746e6543 &&
               edx == 0x48727561 &&
               ecx == 0x736c7561) {  /*  "CentaurHauls" */
        /* VIA C3 */
        cpuid(0x80000000, eax, ebx, ecx, edx);
        if ((unsigned)eax < 0x80000001)
            goto inteltest;	
	cpuid(0x80000001, eax, ebx, ecx, edx);
	rval = 0;      
	if( edx & ( 1 << 31) )
	  rval |= MM_3DNOW;
	if( edx & ( 1 << 23) )
	  rval |= MM_MMX;
	if( edx & ( 1 << 24) )
	  rval |= MM_MMXEXT;
	return rval;
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
        if (eax != 2) 
            goto inteltest;
        cpuid(0x80000001, eax, ebx, ecx, edx);
        if ((eax & 0x00800000) == 0)
            return 0;
        rval = MM_MMX;
        if (eax & 0x01000000)
            rval |= MM_MMXEXT;
        return rval;
    } else {
        return 0;
    }
}

#ifdef __TEST__
int main ( void )
{
  int mm_flags;
  mm_flags = mm_support();
  printf("mm_support = 0x%08u\n",mm_flags);
  return 0;
}
#endif
