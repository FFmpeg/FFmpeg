/*
 * RealAudio 2.0 (28.8K)
 * Copyright (c) 2003 the ffmpeg project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "avcodec.h"
#include "ra288.h"
 
typedef struct {
	float	history[8];
	float	output[40];
	float	pr1[36];
	float	pr2[10];
	int	phase, phasep;

	float st1a[111],st1b[37],st1[37];
	float st2a[38],st2b[11],st2[11];
	float sb[41];
	float lhist[10];
} Real288_internal;

static int ra288_decode_init(AVCodecContext * avctx)
{
	Real288_internal *glob=avctx->priv_data;
	memset(glob,0,sizeof(Real288_internal));
	return 0;
}

static void prodsum(float *tgt, float *src, int len, int n);
static void co(int n, int i, int j, float *in, float *out, float *st1, float *st2, const float *table);
static int pred(float *in, float *tgt, int n);
static void colmult(float *tgt, float *m1, const float *m2, int n);


/* initial decode */
static void unpack(unsigned short *tgt, unsigned char *src, unsigned int len)
{
  int x,y,z;
  int n,temp;
  int buffer[len];

  for (x=0;x<len;tgt[x++]=0)
    buffer[x]=9+(x&1);

  for (x=y=z=0;x<len/*was 38*/;x++) {
    n=buffer[y]-z;
    temp=src[x];
    if (n<8) temp&=255>>(8-n);
    tgt[y]+=temp<<z;
    if (n<=8) {
      tgt[++y]+=src[x]>>n;
      z=8-n;
    } else z+=8;
  }
}

static void update(Real288_internal *glob)
{
  int x,y;
  float buffer1[40],temp1[37];
  float buffer2[8],temp2[11];

  for (x=0,y=glob->phasep+5;x<40;buffer1[x++]=glob->output[(y++)%40]);
  co(36,40,35,buffer1,temp1,glob->st1a,glob->st1b,table1);
  if (pred(temp1,glob->st1,36))
    colmult(glob->pr1,glob->st1,table1a,36);

  for (x=0,y=glob->phase+1;x<8;buffer2[x++]=glob->history[(y++)%8]);
  co(10,8,20,buffer2,temp2,glob->st2a,glob->st2b,table2);
  if (pred(temp2,glob->st2,10))
    colmult(glob->pr2,glob->st2,table2a,10);
}

/* Decode and produce output */
static void decode(Real288_internal *glob, unsigned int input)
{
  unsigned int x,y;
  float f;
  double sum,sumsum;
  float *p1,*p2;
  float buffer[5];
  const float *table;

  for (x=36;x--;glob->sb[x+5]=glob->sb[x]);
  for (x=5;x--;) {
    p1=glob->sb+x;p2=glob->pr1;
    for (sum=0,y=36;y--;sum-=(*(++p1))*(*(p2++)));
    glob->sb[x]=sum;
  }

  f=amptable[input&7];
  table=codetable+(input>>3)*5;

  /* convert log and do rms */
  for (sum=32,x=10;x--;sum-=glob->pr2[x]*glob->lhist[x]);
  if (sum<0) sum=0; else if (sum>60) sum=60;

  sumsum=exp(sum*0.1151292546497)*f;	/* pow(10.0,sum/20)*f */
  for (sum=0,x=5;x--;) { buffer[x]=table[x]*sumsum; sum+=buffer[x]*buffer[x]; }
  if ((sum/=5)<1) sum=1;

  /* shift and store */
  for (x=10;--x;glob->lhist[x]=glob->lhist[x-1]);
  *glob->lhist=glob->history[glob->phase]=10*log10(sum)-32;

  for (x=1;x<5;x++) for (y=x;y--;buffer[x]-=glob->pr1[x-y-1]*buffer[y]);

  /* output */
  for (x=0;x<5;x++) {
    f=glob->sb[4-x]+buffer[x];
    if (f>4095) f=4095; else if (f<-4095) f=-4095;
    glob->output[glob->phasep+x]=glob->sb[4-x]=f;
  }
}

/* column multiply */
static void colmult(float *tgt, float *m1, const float *m2, int n)
{
  while (n--)
    *(tgt++)=(*(m1++))*(*(m2++));
}

static int pred(float *in, float *tgt, int n)
{
  int x,y;
  float *p1,*p2;
  double f0,f1,f2;
  float temp;

  if (in[n]==0) return 0;
  if ((f0=*in)<=0) return 0;

  for (x=1;;x++) {
    if (n<x) return 1;

    p1=in+x;
    p2=tgt;
    f1=*(p1--);
    for (y=x;--y;f1+=(*(p1--))*(*(p2++)));

    p1=tgt+x-1;
    p2=tgt;
    *(p1--)=f2=-f1/f0;
    for (y=x>>1;y--;) {
      temp=*p2+*p1*f2;
      *(p1--)+=*p2*f2;
      *(p2++)=temp;
    }
    if ((f0+=f1*f2)<0) return 0;
  }
}

static void co(int n, int i, int j, float *in, float *out, float *st1, float *st2, const float *table)
{
  int a,b,c;
  unsigned int x;
  float *fp;
  float buffer1[37];
  float buffer2[37];
  float work[111];

  /* rotate and multiply */
  c=(b=(a=n+i)+j)-i;
  fp=st1+i;
  for (x=0;x<b;x++) {
    if (x==c) fp=in;
    work[x]=*(table++)*(*(st1++)=*(fp++));
  }
  
  prodsum(buffer1,work+n,i,n);
  prodsum(buffer2,work+a,j,n);

  for (x=0;x<=n;x++) {
    *st2=*st2*(0.5625)+buffer1[x];
    out[x]=*(st2++)+buffer2[x];
  }
  *out*=1.00390625; /* to prevent clipping */
}

/* product sum (lsf) */
static void prodsum(float *tgt, float *src, int len, int n)
{
  unsigned int x;
  float *p1,*p2;
  double sum;

  while (n>=0)
  {
    p1=(p2=src)-n;
    for (sum=0,x=len;x--;sum+=(*p1++)*(*p2++));
    tgt[n--]=sum;
  }
}

static void * decode_block(AVCodecContext * avctx, unsigned char *in, signed short int *out,unsigned len)
{
  int x,y;
  Real288_internal *glob=avctx->priv_data;
  unsigned short int buffer[len];

  unpack(buffer,in,len);
  for (x=0;x<32;x++)
  {
    glob->phasep=(glob->phase=x&7)*5;
    decode(glob,buffer[x]);
    for (y=0;y<5;*(out++)=8*glob->output[glob->phasep+(y++)]);
    if (glob->phase==3) update(glob);
  }
  return out;
}

/* Decode a block (celp) */
static int ra288_decode_frame(AVCodecContext * avctx,
            void *data, int *data_size,
            uint8_t * buf, int buf_size)
{
  if(avctx->extradata_size>=6)
  {
//((short*)(avctx->extradata))[0]; /* subpacket size */
//((short*)(avctx->extradata))[1]; /* subpacket height */
//((short*)(avctx->extradata))[2]; /* subpacket flavour */
//((short*)(avctx->extradata))[3]; /* coded frame size */
//((short*)(avctx->extradata))[4]; /* codec's data length  */
//((short*)(avctx->extradata))[5...] /* codec's data */
    int bret;
    void *datao;
    int w=avctx->block_align; /* 228 */
    int h=((short*)(avctx->extradata))[1]; /* 12 */
    int cfs=((short*)(avctx->extradata))[3]; /* coded frame size 38 */
    int i,j;
    if(buf_size<w*h)
    {
	av_log(avctx, AV_LOG_ERROR, "ffra288: Error! Input buffer is too small [%d<%d]\n",buf_size,w*h);
	return 0;
    }
    datao = data;
    bret = 0;
    for (j = 0; j < h/2; j++)
	for (i = 0; i < h; i++)
    {
	    data=decode_block(avctx,&buf[j*cfs+cfs*i*h/2],(signed short *)data,cfs);
	    bret += cfs;
    }
    *data_size = (char *)data - (char *)datao;
    return bret;
  }
  else
  {
    av_log(avctx, AV_LOG_ERROR, "ffra288: Error: need extra data!!!\n");
    return 0;
  }
}

AVCodec ra_288_decoder =
{
    "real_288",
    CODEC_TYPE_AUDIO,
    CODEC_ID_RA_288,
    sizeof(Real288_internal),
    ra288_decode_init,
    NULL,
    NULL,
    ra288_decode_frame,
};
