
// a52_resample_init should find the requested converter (from type flags ->
// given number of channels) and set up some function pointers...

// a52_resample() should do the conversion.

#include "a52.h"
#include "mm_accel.h"
#include "config.h"
#include "../libpostproc/mangle.h"

int (* a52_resample) (float * _f, int16_t * s16)=NULL;

#include "resample_c.c"

#ifdef ARCH_X86
#include "resample_mmx.c"
#endif

void* a52_resample_init(uint32_t mm_accel,int flags,int chans){
void* tmp;

#ifdef ARCH_X86
    if(mm_accel&MM_ACCEL_X86_MMX){
	tmp=a52_resample_MMX(flags,chans);
	if(tmp){
	    if(a52_resample==NULL) fprintf(stderr, "Using MMX optimized resampler\n");
	    a52_resample=tmp;
	    return tmp;
	}
    }
#endif

    tmp=a52_resample_C(flags,chans);
    if(tmp){
	if(a52_resample==NULL) fprintf(stderr, "No accelerated resampler found\n");
	a52_resample=tmp;
	return tmp;
    }
    
    fprintf(stderr, "Unimplemented resampler for mode 0x%X -> %d channels conversion - Contact MPlayer developers!\n", flags, chans);
    return NULL;
}
