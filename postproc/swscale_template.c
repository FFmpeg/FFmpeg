
// Software scaling and colorspace conversion routines for MPlayer

// temporary storage for 4 yuv lines:
static unsigned int pix_buf_y[4][2048];
static unsigned int pix_buf_uv[2][2048*2];

// clipping helper table for C implementations:
static unsigned char clip_table[768];

// yuv->rgb conversion tables:
static    int yuvtab_2568[256];
static    int yuvtab_3343[256];
static    int yuvtab_0c92[256];
static    int yuvtab_1a1e[256];
static    int yuvtab_40cf[256];

// *** bilinear scaling and yuv->rgb conversion of yv12 slices:
// *** Note: it's called multiple times while decoding a frame, first time y==0
// *** Designed to upscale, but may work for downscale too.
// s_xinc = (src_width << 8) / dst_width
// s_yinc = (src_height << 16) / dst_height
void SwScale_YV12slice_brg24(unsigned char* srcptr[],int stride[], int y, int h,
			     unsigned char* dstptr, int dststride, int dstw, int dstbpp,
			     unsigned int s_xinc,unsigned int s_yinc){

// scaling factors:
//static int s_yinc=(vo_dga_src_height<<16)/vo_dga_vp_height;
//static int s_xinc=(vo_dga_src_width<<8)/vo_dga_vp_width;

unsigned int s_xinc2=s_xinc>>1;

static int s_srcypos;
static int s_ypos;
static int s_last_ypos;

  if(y==0){
      s_srcypos=-2*s_yinc;
      s_ypos=-2;
      s_last_ypos=-2;
  } // reset counters
  
  while(1){
    unsigned char *dest=dstptr+dststride*s_ypos;
    int y0=2+(s_srcypos>>16);
    int y1=1+(s_srcypos>>17);
    int yalpha=(s_srcypos&0xFFFF)>>8;
    int yalpha1=yalpha^255;
    int uvalpha=((s_srcypos>>1)&0xFFFF)>>8;
    int uvalpha1=uvalpha^255;
    unsigned int *buf0=pix_buf_y[y0&3];
    unsigned int *buf1=pix_buf_y[((y0+1)&3)];
    unsigned int *uvbuf0=pix_buf_uv[y1&1];
    unsigned int *uvbuf1=pix_buf_uv[(y1&1)^1];
    int i;

    if(y0>=y+h) break;

    s_ypos++; s_srcypos+=s_yinc;

    if(s_last_ypos!=y0){
      unsigned char *src=srcptr[0]+(y0-y)*stride[0];
      unsigned int xpos=0;
      s_last_ypos=y0;
      // *** horizontal scale Y line to temp buffer
      // this loop should be rewritten in MMX assembly!!!!
      for(i=0;i<dstw;i++){
	register unsigned int xx=xpos>>8;
        register unsigned int xalpha=xpos&0xFF;
	buf1[i]=(src[xx]*(xalpha^255)+src[xx+1]*xalpha);
	xpos+=s_xinc;
      }
      // *** horizontal scale U and V lines to temp buffer
      if(!(y0&1)){
        unsigned char *src1=srcptr[1]+(y1-y/2)*stride[1];
        unsigned char *src2=srcptr[2]+(y1-y/2)*stride[2];
        xpos=0;
        // this loop should be rewritten in MMX assembly!!!!
        for(i=0;i<dstw;i++){
	  register unsigned int xx=xpos>>8;
          register unsigned int xalpha=xpos&0xFF;
	  uvbuf1[i]=(src1[xx]*(xalpha^255)+src1[xx+1]*xalpha);
	  uvbuf1[i+2048]=(src2[xx]*(xalpha^255)+src2[xx+1]*xalpha);
	  xpos+=s_xinc2;
        }
      }
      if(!y0) continue;
    }

    // this loop should be rewritten in MMX assembly!!!!
    // Note1: this code can be resticted to n*8 (or n*16) width lines to simplify optimization...
    // Note2: instead of using lookup tabs, mmx version could do the multiply...
    // Note3: maybe we should make separated 15/16, 24 and 32bpp version of this:
    for(i=0;i<dstw;i++){
	// vertical linear interpolation && yuv2rgb in a single step:
	int Y=yuvtab_2568[((buf0[i]*yalpha1+buf1[i]*yalpha)>>16)];
	int U=((uvbuf0[i]*uvalpha1+uvbuf1[i]*uvalpha)>>16);
	int V=((uvbuf0[i+2048]*uvalpha1+uvbuf1[i+2048]*uvalpha)>>16);
	dest[0]=clip_table[((Y + yuvtab_3343[U]) >>13)];
	dest[1]=clip_table[((Y + yuvtab_0c92[V] + yuvtab_1a1e[U]) >>13)];
	dest[2]=clip_table[((Y + yuvtab_40cf[V]) >>13)];
	dest+=dstbpp;
    }
  
  }

}


void SwScale_Init(){
    // generating tables:
    int i;
    for(i=0;i<256;i++){
        clip_table[i]=0;
        clip_table[i+256]=i;
        clip_table[i+512]=255;
	yuvtab_2568[i]=(0x2568*(i-16))+(256<<13);
	yuvtab_3343[i]=0x3343*(i-128);
	yuvtab_0c92[i]=-0x0c92*(i-128);
	yuvtab_1a1e[i]=-0x1a1e*(i-128);
	yuvtab_40cf[i]=0x40cf*(i-128);
    }

}
