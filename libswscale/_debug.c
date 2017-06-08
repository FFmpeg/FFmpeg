#include "swscale_internal.h"

#if !ARCH_AARCH64
void ff_sws_init_swscale_aarch64(struct SwsContext *c){};
void ff_get_unscaled_swscale_aarch64(struct SwsContext *c){};
#endif

#if !ARCH_ARM
void ff_sws_init_swscale_arm(struct SwsContext *c){};
void ff_get_unscaled_swscale_arm(struct SwsContext *c){};
#endif

#if !ARCH_PPC
void ff_sws_init_swscale_ppc(struct SwsContext *c){};
void ff_get_unscaled_swscale_ppc(struct SwsContext *c){};
void ff_yuv2rgb_init_tables_ppc(struct SwsContext *c, const int inv_table[4],
 int brightness, int contrast, int saturation){};
SwsFunc ff_yuv2rgb_init_ppc(struct SwsContext *c) {return NULL;}; 
#endif

#if !ARCH_MIPS
#endif
#if !ARCH_X86
#endif





