/*
 * Real Audio 1.0 (14.4K)
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
#include "ra144.h"

#define DATABLOCK1	20			/* size of 14.4 input block in bytes */
#define DATACHUNK1	1440			/* size of 14.4 input chunk in bytes */
#define AUDIOBLOCK	160			/* size of output block in 16-bit words (320 bytes) */
#define AUDIOBUFFER	12288			/* size of output buffer in 16-bit words (24576 bytes) */
/* consts */
#define NBLOCKS		4				/* number of segments within a block */
#define BLOCKSIZE	40				/* (quarter) block size in 16-bit words (80 bytes) */
#define HALFBLOCK	20				/* BLOCKSIZE/2 */
#define BUFFERSIZE	146				/* for do_output */


/* internal globals */
typedef struct {
	unsigned int	 resetflag, val, oldval;
	unsigned int	 unpacked[28];		/* buffer for unpacked input */
	unsigned int	*iptr;				/* pointer to current input (from unpacked) */
	unsigned int	 gval;
	unsigned short	*gsp;
	unsigned int	 gbuf1[8];
	unsigned short	 gbuf2[120];
	signed   short	 output_buffer[40];
	unsigned int	*decptr;			/* decoder ptr */
	signed   short	*decsp;

	/* the swapped buffers */
	unsigned int	 swapb1a[10];
	unsigned int	 swapb2a[10];
	unsigned int	 swapb1b[10];
	unsigned int	 swapb2b[10];
	unsigned int	*swapbuf1;
	unsigned int	*swapbuf2;
	unsigned int	*swapbuf1alt;
	unsigned int	*swapbuf2alt;

	unsigned int buffer[5];
	unsigned short int buffer_2[148];
	unsigned short int buffer_a[40];
	unsigned short int buffer_b[40];
	unsigned short int buffer_c[40];
	unsigned short int buffer_d[40];

	unsigned short int work[50];
	unsigned short *sptr;

	int buffer1[10];
	int buffer2[10];

	signed short wavtable1[2304];
	unsigned short wavtable2[2304];
} Real144_internal;

static int ra144_decode_init(AVCodecContext * avctx)
{
	Real144_internal *glob=avctx->priv_data;

	memset(glob,0,sizeof(Real144_internal));
	glob->resetflag=1;
	glob->swapbuf1=glob->swapb1a;
	glob->swapbuf2=glob->swapb2a;
	glob->swapbuf1alt=glob->swapb1b;
	glob->swapbuf2alt=glob->swapb2b;

	memcpy(glob->wavtable1,wavtable1,sizeof(wavtable1));
	memcpy(glob->wavtable2,wavtable2,sizeof(wavtable2));

	return 0;
}

static void final(Real144_internal *glob, short *i1, short *i2, void *out, int *statbuf, int len);
static void add_wav(Real144_internal *glob, int n, int f, int m1, int m2, int m3, short *s1, short *s2, short *s3, short *dest);
static int irms(short *data, int factor);
static void rotate_block(short *source, short *target, int offset);
/* lookup square roots in table */
static int t_sqrt(unsigned int x)
{
  int s=0;
  while (x>0xfff) { s++; x=x>>2; }
  return (sqrt_table[x]<<s)<<2;
}

/* do 'voice' */
static void do_voice(int *a1, int *a2)
{
  int buffer[10];
  int *b1,*b2;
  int x,y;
  int *ptr,*tmp;
  
  b1=buffer;
  b2=a2;
  
  for (x=0;x<10;x++) {
    b1[x]=(*a1)<<4;

    if(x>0) {
      ptr=b2+x;
      for (y=0;y<=x-1;y++)
        b1[y]=(((*a1)*(*(--ptr)))>>12)+b2[y];
    }
    tmp=b1;
    b1=b2;
    b2=tmp;
    a1++;
  }  
  ptr=a2+10;
  while (ptr>a2) (*a2++)>>=4;
}


/* do quarter-block output */
static void do_output_subblock(Real144_internal *glob, unsigned int x)
{
  int a,b,c,d,e,f,g;

  if (x==1) memset(glob->buffer,0,20);
  if ((*glob->iptr)==0) a=0;
  else a=(*glob->iptr)+HALFBLOCK-1;
  glob->iptr++;
  b=*(glob->iptr++);
  c=*(glob->iptr++);
  d=*(glob->iptr++);
  if (a) rotate_block(glob->buffer_2,glob->buffer_a,a);
  memcpy(glob->buffer_b,etable1+b*BLOCKSIZE,BLOCKSIZE*2);
  e=((ftable1[b]>>4)*glob->gval)>>8;
  memcpy(glob->buffer_c,etable2+c*BLOCKSIZE,BLOCKSIZE*2);
  f=((ftable2[c]>>4)*glob->gval)>>8;
  if (a) g=irms(glob->buffer_a,glob->gval)>>12;
  else g=0;
  add_wav(glob,d,a,g,e,f,glob->buffer_a,glob->buffer_b,glob->buffer_c,glob->buffer_d);
  memmove(glob->buffer_2,glob->buffer_2+BLOCKSIZE,(BUFFERSIZE-BLOCKSIZE)*2);
  memcpy(glob->buffer_2+BUFFERSIZE-BLOCKSIZE,glob->buffer_d,BLOCKSIZE*2);
  final(glob,glob->gsp,glob->buffer_d,glob->output_buffer,glob->buffer,BLOCKSIZE);
}

/* rotate block */
static void rotate_block(short *source, short *target, int offset)
{
  short *end;
  short *ptr1;
  short *ptr2;
  short *ptr3;
  ptr2=source+BUFFERSIZE;
  ptr3=ptr1=ptr2-offset;
  end=target+BLOCKSIZE;
  while (target<end) {
    *(target++)=*(ptr3++);
    if (ptr3==ptr2) ptr3=ptr1;
  }
}

/* inverse root mean square */
static int irms(short *data, int factor)
{
  short *p1,*p2;
  unsigned int sum;
  p2=(p1=data)+BLOCKSIZE;
  for (sum=0;p2>p1;p1++) sum+=(*p1)*(*p1);
  if (sum==0) return 0; /* OOPS - division by zero */
  return (0x20000000/(t_sqrt(sum)>>8))*factor;
}

/* multiply/add wavetable */
static void add_wav(Real144_internal *glob, int n, int f, int m1, int m2, int m3, short *s1, short *s2, short *s3, short *dest)
{
  int a,b,c;
  short *ptr,*ptr2;

  ptr=glob->wavtable1+n*9;
  ptr2=glob->wavtable2+n*9;
  if (f!=0) {
    a=((*ptr)*m1)>>((*ptr2)+1); 
  } else {
    a=0;
  }
  ptr++;ptr2++;
  b=((*ptr)*m2)>>((*ptr2)+1);
  ptr++;ptr2++;
  c=((*ptr)*m3)>>((*ptr2)+1);
  ptr2=(ptr=dest)+BLOCKSIZE;
  if (f!=0)
    while (ptr<ptr2)
      *(ptr++)=((*(s1++))*a+(*(s2++))*b+(*(s3++))*c)>>12;
  else
    while (ptr<ptr2)
      *(ptr++)=((*(s2++))*b+(*(s3++))*c)>>12;
}


static void final(Real144_internal *glob, short *i1, short *i2, void *out, int *statbuf, int len)
{
  int x,sum;
  int buffer[10];
  short *ptr;
  short *ptr2;

  memcpy(glob->work,statbuf,20);
  memcpy(glob->work+10,i2,len*2);

  buffer[9]=i1[0];
  buffer[8]=i1[1];
  buffer[7]=i1[2];
  buffer[6]=i1[3];
  buffer[5]=i1[4];
  buffer[4]=i1[5];
  buffer[3]=i1[6];
  buffer[2]=i1[7];
  buffer[1]=i1[8];
  buffer[0]=i1[9];

  ptr2=(ptr=glob->work)+len;
  while (ptr<ptr2) {
    for(sum=0,x=0;x<=9;x++)
      sum+=buffer[x]*(ptr[x]);
    sum=sum>>12;
    x=ptr[10]-sum;
    if (x<-32768 || x>32767)
    {
      memset(out,0,len*2);
      memset(statbuf,0,20);
      return;
    }
    ptr[10]=x;
    ptr++;
  }
  memcpy(out,ptr+10-len,len*2);
  memcpy(statbuf,ptr,20);
}

/* Decode 20-byte input */
static void unpack_input(unsigned char *input, unsigned int *output)
{
  unsigned int outbuffer[28];
  unsigned short inbuffer[10];
  unsigned int x;
  unsigned int *ptr;

  /* fix endianness */
  for (x=0;x<20;x+=2)
    inbuffer[x/2]=(input[x]<<8)+input[x+1];

  /* unpack */
  ptr=outbuffer;
  *(ptr++)=27;
  *(ptr++)=(inbuffer[0]>>10)&0x3f;
  *(ptr++)=(inbuffer[0]>>5)&0x1f;
  *(ptr++)=inbuffer[0]&0x1f;
  *(ptr++)=(inbuffer[1]>>12)&0xf;
  *(ptr++)=(inbuffer[1]>>8)&0xf;
  *(ptr++)=(inbuffer[1]>>5)&7;
  *(ptr++)=(inbuffer[1]>>2)&7;
  *(ptr++)=((inbuffer[1]<<1)&6)|((inbuffer[2]>>15)&1);
  *(ptr++)=(inbuffer[2]>>12)&7;
  *(ptr++)=(inbuffer[2]>>10)&3;
  *(ptr++)=(inbuffer[2]>>5)&0x1f;
  *(ptr++)=((inbuffer[2]<<2)&0x7c)|((inbuffer[3]>>14)&3);
  *(ptr++)=(inbuffer[3]>>6)&0xff;
  *(ptr++)=((inbuffer[3]<<1)&0x7e)|((inbuffer[4]>>15)&1);
  *(ptr++)=(inbuffer[4]>>8)&0x7f;
  *(ptr++)=(inbuffer[4]>>1)&0x7f;
  *(ptr++)=((inbuffer[4]<<7)&0x80)|((inbuffer[5]>>9)&0x7f);
  *(ptr++)=(inbuffer[5]>>2)&0x7f;
  *(ptr++)=((inbuffer[5]<<5)&0x60)|((inbuffer[6]>>11)&0x1f);
  *(ptr++)=(inbuffer[6]>>4)&0x7f;
  *(ptr++)=((inbuffer[6]<<4)&0xf0)|((inbuffer[7]>>12)&0xf);
  *(ptr++)=(inbuffer[7]>>5)&0x7f;
  *(ptr++)=((inbuffer[7]<<2)&0x7c)|((inbuffer[8]>>14)&3);
  *(ptr++)=(inbuffer[8]>>7)&0x7f;
  *(ptr++)=((inbuffer[8]<<1)&0xfe)|((inbuffer[9]>>15)&1);
  *(ptr++)=(inbuffer[9]>>8)&0x7f;
  *(ptr++)=(inbuffer[9]>>1)&0x7f;

  *(output++)=outbuffer[11];
  for (x=1;x<11;*(output++)=outbuffer[x++]);
  ptr=outbuffer+12;
  for (x=0;x<16;x+=4)
  {
    *(output++)=ptr[x];
    *(output++)=ptr[x+2];
    *(output++)=ptr[x+3];
    *(output++)=ptr[x+1];    
  }
}

static unsigned int rms(int *data, int f)
{
  int *c;
  int x;
  unsigned int res;
  int b;

  c=data;
  b=0;
  res=0x10000;
  for (x=0;x<10;x++)
  {
    res=(((0x1000000-(*c)*(*c))>>12)*res)>>12;
    if (res==0) return 0;
    if (res<=0x3fff)
    {
      while (res<=0x3fff)
      {
        b++;
        res<<=2;
      }
    } else {
      if (res>0x10000)
        return 0; /* We're screwed, might as well go out with a bang. :P */
    }
    c++;
  }
  if (res>0) res=t_sqrt(res);

  res>>=(b+10);
  res=(res*f)>>10;
  return res;
}

static void dec1(Real144_internal *glob, int *data, int *inp, int n, int f)
{
  short *ptr,*end;

  *(glob->decptr++)=rms(data,f);
  glob->decptr++;
  end=(ptr=glob->decsp)+(n*10);
  while (ptr<end) *(ptr++)=*(inp++);
}

static int eq(Real144_internal *glob, short *in, int *target)
{
  int retval;
  int a;
  int b;
  int c;
  unsigned int u;
  short *sptr;
  int *ptr1,*ptr2,*ptr3;
  int *bp1,*bp2,*temp;

  retval=0;
  bp1=glob->buffer1;
  bp2=glob->buffer2;
  ptr2=(ptr3=glob->buffer2)+9;
  sptr=in;
  while (ptr2>=ptr3)
    *(ptr3++)=*(sptr++);

  target+=9;
  a=bp2[9];
  *target=a;
  if (a+0x1000>0x1fff)
    return 0; /* We're screwed, might as well go out with a bang. :P */
  c=8;u=a;
  while (c>=0)
  {
    if (u==0x1000) u++;
    if (u==0xfffff000) u--;
    b=0x1000-((u*u)>>12);
    if (b==0) b++;
    ptr2=bp1;
    ptr1=(ptr3=bp2)+c;
    for (u=0;u<=c;u++)
      *(ptr2++)=((*(ptr3++)-(((*target)*(*(ptr1--)))>>12))*(0x1000000/b))>>12;
    *(--target)=u=bp1[(c--)];
    if ((u+0x1000)>0x1fff) retval=1;
    temp=bp2;
    bp2=bp1;
    bp1=temp;
  }
  return retval;
}

static void dec2(Real144_internal *glob, int *data, int *inp, int n, int f, int *inp2, int l)
{
  unsigned int *ptr1,*ptr2;
  int work[10];
  int a,b;
  int x;
  int result;

  if(l+1<NBLOCKS/2) a=NBLOCKS-(l+1);
  else a=l+1;
  b=NBLOCKS-a;
  if (l==0)
  {
    glob->decsp=glob->sptr=glob->gbuf2;
    glob->decptr=glob->gbuf1;
  }
  ptr1=inp;
  ptr2=inp2;
  for (x=0;x<10*n;x++)
    *(glob->sptr++)=(a*(*ptr1++)+b*(*ptr2++))>>2;
  result=eq(glob,glob->decsp,work);
  if (result==1)
  {
    dec1(glob,data,inp,n,f);
  } else {
    *(glob->decptr++)=rms(work,f);
    glob->decptr++;
  }
  glob->decsp+=n*10;
}

/* Uncompress one block (20 bytes -> 160*2 bytes) */
static int ra144_decode_frame(AVCodecContext * avctx,
            void *vdata, int *data_size,
            uint8_t * buf, int buf_size)
{
  unsigned int a,b,c;
  long s;
  signed short *shptr;
  unsigned int *lptr,*temp;
  const short **dptr;
  int16_t *datao;
  int16_t *data = vdata;
  Real144_internal *glob=avctx->priv_data;

  if(buf_size==0)
      return 0;
  
  datao = data;
  unpack_input(buf,glob->unpacked);
  
  glob->iptr=glob->unpacked;
  glob->val=decodetable[0][(*(glob->iptr++))<<1];

  dptr=decodetable+1;
  lptr=glob->swapbuf1;
  while (lptr<glob->swapbuf1+10)
    *(lptr++)=(*(dptr++))[(*(glob->iptr++))<<1];

  do_voice(glob->swapbuf1,glob->swapbuf2);

  a=t_sqrt(glob->val*glob->oldval)>>12;

  for (c=0;c<NBLOCKS;c++) {
    if (c==(NBLOCKS-1)) {
      dec1(glob,glob->swapbuf1,glob->swapbuf2,3,glob->val);
    } else {
      if (c*2==(NBLOCKS-2)) {
        if (glob->oldval<glob->val) {
          dec2(glob,glob->swapbuf1,glob->swapbuf2,3,a,glob->swapbuf2alt,c);
        } else {
          dec2(glob,glob->swapbuf1alt,glob->swapbuf2alt,3,a,glob->swapbuf2,c);
        }
      } else {
        if (c*2<(NBLOCKS-2)) {
          dec2(glob,glob->swapbuf1alt,glob->swapbuf2alt,3,glob->oldval,glob->swapbuf2,c);
        } else {
          dec2(glob,glob->swapbuf1,glob->swapbuf2,3,glob->val,glob->swapbuf2alt,c);
        }
      }
    }
  }

  /* do output */
  for (b=0,c=0;c<4;c++) {
    glob->gval=glob->gbuf1[c*2];
    glob->gsp=glob->gbuf2+b;
    do_output_subblock(glob,glob->resetflag);
    glob->resetflag=0;

    shptr=glob->output_buffer;
    while (shptr<glob->output_buffer+BLOCKSIZE) {
      s=*(shptr++)<<2;
      *data=s;
      if (s>32767) *data=32767;
      if (s<-32767) *data=-32768;
      data++;
    }
    b+=30;
  }

  glob->oldval=glob->val;
  temp=glob->swapbuf1alt;
  glob->swapbuf1alt=glob->swapbuf1;
  glob->swapbuf1=temp;
  temp=glob->swapbuf2alt;
  glob->swapbuf2alt=glob->swapbuf2;
  glob->swapbuf2=temp;
  *data_size=(data-datao)*sizeof(*data);
  return 20;
}


AVCodec ra_144_decoder =
{
    "real_144",
    CODEC_TYPE_AUDIO,
    CODEC_ID_RA_144,
    sizeof(Real144_internal),
    ra144_decode_init,
    NULL,
    NULL,
    ra144_decode_frame,
};
