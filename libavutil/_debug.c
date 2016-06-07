#include "float_dsp.h"

#if !ARCH_AARCH64
int ff_get_cpu_flags_aarch64(void){return 0;};
void ff_float_dsp_init_aarch64(AVFloatDSPContext *fdsp){};
#endif

#if !ARCH_ARM
int ff_get_cpu_flags_arm(void){return 0;};
void ff_float_dsp_init_arm(AVFloatDSPContext *fdsp){};
#endif
#if !ARCH_PPC
int ff_get_cpu_flags_ppc(void){return 0;};
void ff_float_dsp_init_ppc(AVFloatDSPContext *fdsp, int strict){};
#endif
#if !ARCH_MIPS
void ff_float_dsp_init_mips(AVFloatDSPContext *fdsp){};
#endif
#if !ARCH_X86

int ff_get_cpu_flags_x86(void){return 0;};
#endif