/*
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>

#include "config.h"
#include "mp_msg.h"
#include "cpudetect.h"
#include "libavutil/common.h"
#include "mpbswap.h"

#include "img_format.h"
#include "mp_image.h"
#include "vf.h"

#include "libvo/fastmemcpy.h"

const vf_info_t vf_info_divtc;

struct vf_priv_s
   {
   int deghost, pass, phase, window, fcount, bcount, frameno, misscount,
      ocount, sum[5];
   double threshold;
   FILE *file;
   int8_t *bdata;
   unsigned int *csdata;
   int *history;
   };

/*
 * diff_MMX and diff_C stolen from vf_decimate.c
 */

#if HAVE_MMX && HAVE_EBX_AVAILABLE
static int diff_MMX(unsigned char *old, unsigned char *new, int os, int ns)
   {
   volatile short out[4];
   __asm__ (
        "movl $8, %%ecx \n\t"
        "pxor %%mm4, %%mm4 \n\t"
        "pxor %%mm7, %%mm7 \n\t"

        ASMALIGN(4)
        "1: \n\t"

        "movq (%%"REG_S"), %%mm0 \n\t"
        "movq (%%"REG_S"), %%mm2 \n\t"
        "add %%"REG_a", %%"REG_S" \n\t"
        "movq (%%"REG_D"), %%mm1 \n\t"
        "add %%"REG_b", %%"REG_D" \n\t"
        "psubusb %%mm1, %%mm2 \n\t"
        "psubusb %%mm0, %%mm1 \n\t"
        "movq %%mm2, %%mm0 \n\t"
        "movq %%mm1, %%mm3 \n\t"
        "punpcklbw %%mm7, %%mm0 \n\t"
        "punpcklbw %%mm7, %%mm1 \n\t"
        "punpckhbw %%mm7, %%mm2 \n\t"
        "punpckhbw %%mm7, %%mm3 \n\t"
        "paddw %%mm0, %%mm4 \n\t"
        "paddw %%mm1, %%mm4 \n\t"
        "paddw %%mm2, %%mm4 \n\t"
        "paddw %%mm3, %%mm4 \n\t"

        "decl %%ecx \n\t"
        "jnz 1b \n\t"
        "movq %%mm4, (%%"REG_d") \n\t"
        "emms \n\t"
        :
        : "S" (old), "D" (new), "a" ((long)os), "b" ((long)ns), "d" (out)
        : "%ecx", "memory"
        );
   return out[0]+out[1]+out[2]+out[3];
   }
#endif

static int diff_C(unsigned char *old, unsigned char *new, int os, int ns)
   {
   int x, y, d=0;

   for(y=8; y; y--, new+=ns, old+=os)
      for(x=8; x; x--)
         d+=abs(new[x]-old[x]);

   return d;
   }

static int (*diff)(unsigned char *, unsigned char *, int, int);

static int diff_plane(unsigned char *old, unsigned char *new,
                      int w, int h, int os, int ns, int arg)
   {
   int x, y, d, max=0, sum=0, n=0;

   for(y=0; y<h-7; y+=8)
      {
      for(x=0; x<w-7; x+=8)
         {
         d=diff(old+x+y*os, new+x+y*ns, os, ns);
         if(d>max) max=d;
         sum+=d;
         n++;
         }
      }

   return (sum+n*max)/2;
   }

/*
static unsigned int checksum_plane(unsigned char *p, unsigned char *z,
                                   int w, int h, int s, int zs, int arg)
   {
   unsigned int shift, sum;
   unsigned char *e;

   for(sum=0; h; h--, p+=s-w)
      for(e=p+w, shift=32; p<e;)
         sum^=(*p++)<<(shift=(shift-8)&31);

   return sum;
   }
*/

static unsigned int checksum_plane(unsigned char *p, unsigned char *z,
                                   int w, int h, int s, int zs, int arg)
   {
   unsigned int shift;
   uint32_t sum, t;
   unsigned char *e, *e2;
#if HAVE_FAST_64BIT
   typedef uint64_t wsum_t;
#else
   typedef uint32_t wsum_t;
#endif
   wsum_t wsum;

   for(sum=0; h; h--, p+=s-w)
      {
      for(shift=0, e=p+w; (int)p&(sizeof(wsum_t)-1) && p<e;)
         sum^=*p++<<(shift=(shift-8)&31);

      for(wsum=0, e2=e-sizeof(wsum_t)+1; p<e2; p+=sizeof(wsum_t))
         wsum^=*(wsum_t *)p;

#if HAVE_FAST_64BIT
      t=be2me_32((uint32_t)(wsum>>32^wsum));
#else
      t=be2me_32(wsum);
#endif

      for(sum^=(t<<shift|t>>(32-shift)); p<e;)
         sum^=*p++<<(shift=(shift-8)&31);
      }

   return sum;
   }

static int deghost_plane(unsigned char *d, unsigned char *s,
                         int w, int h, int ds, int ss, int threshold)
   {
   int t;
   unsigned char *e;

   for(; h; h--, s+=ss-w, d+=ds-w)
      for(e=d+w; d<e; d++, s++)
         if(abs(*d-*s)>=threshold)
            *d=(t=(*d<<1)-*s)<0?0:t>255?255:t;

   return 0;
   }

static int copyop(unsigned char *d, unsigned char *s, int bpl, int h, int dstride, int sstride, int dummy) {
  memcpy_pic(d, s, bpl, h, dstride, sstride);
  return 0;
}

static int imgop(int(*planeop)(unsigned char *, unsigned char *,
                               int, int, int, int, int),
                 mp_image_t *dst, mp_image_t *src, int arg)
   {
   if(dst->flags&MP_IMGFLAG_PLANAR)
      return planeop(dst->planes[0], src?src->planes[0]:0,
                     dst->w, dst->h,
                     dst->stride[0], src?src->stride[0]:0, arg)+
             planeop(dst->planes[1], src?src->planes[1]:0,
                     dst->chroma_width, dst->chroma_height,
                     dst->stride[1], src?src->stride[1]:0, arg)+
             planeop(dst->planes[2], src?src->planes[2]:0,
                     dst->chroma_width, dst->chroma_height,
                     dst->stride[2], src?src->stride[2]:0, arg);

   return planeop(dst->planes[0], src?src->planes[0]:0,
                  dst->w*(dst->bpp/8), dst->h,
                  dst->stride[0], src?src->stride[0]:0, arg);
   }

/*
 * Find the phase in which the telecine pattern fits best to the
 * given 5 frame slice of frame difference measurements.
 *
 * If phase1 and phase2 are not negative, only the two specified
 * phases are tested.
 */

static int match(struct vf_priv_s *p, int *diffs,
                 int phase1, int phase2, double *strength)
   {
   static const int pattern1[]={ -4,  1, 1, 1, 1 },
      pattern2[]={ -2, -3, 4, 4, -3 }, *pattern;
   int f, m, n, t[5];

   pattern=p->deghost>0?pattern2:pattern1;

   for(f=0; f<5; f++)
      {
      if(phase1<0 || phase2<0 || f==phase1 || f==phase2)
         {
         for(n=t[f]=0; n<5; n++)
            t[f]+=diffs[n]*pattern[(n-f+5)%5];
         }
      else
         t[f]=INT_MIN;
      }

   /* find the best match */
   for(m=0, n=1; n<5; n++)
      if(t[n]>t[m]) m=n;

   if(strength)
      {
      /* the second best match */
      for(f=m?0:1, n=f+1; n<5; n++)
         if(n!=m && t[n]>t[f]) f=n;

      *strength=(t[m]>0?(double)(t[m]-t[f])/t[m]:0.0);
      }

   return m;
   }

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts)
   {
   mp_image_t *dmpi, *tmpi=0;
   int n, m, f, newphase;
   struct vf_priv_s *p=vf->priv;
   unsigned int checksum;
   double d;

   dmpi=vf_get_image(vf->next, mpi->imgfmt,
                     MP_IMGTYPE_STATIC, MP_IMGFLAG_ACCEPT_STRIDE |
                     MP_IMGFLAG_PRESERVE | MP_IMGFLAG_READABLE,
                     mpi->width, mpi->height);
   vf_clone_mpi_attributes(dmpi, mpi);

   newphase=p->phase;

   switch(p->pass)
      {
      case 1:
         fprintf(p->file, "%08x %d\n",
                 (unsigned int)imgop((void *)checksum_plane, mpi, 0, 0),
                 p->frameno?imgop(diff_plane, dmpi, mpi, 0):0);
         break;

      case 2:
         if(p->frameno/5>p->bcount)
            {
            mp_msg(MSGT_VFILTER, MSGL_ERR,
                   "\n%s: Log file ends prematurely! "
                   "Switching to one pass mode.\n", vf->info->name);
            p->pass=0;
            break;
            }

         checksum=(unsigned int)imgop((void *)checksum_plane, mpi, 0, 0);

         if(checksum!=p->csdata[p->frameno])
            {
            for(f=0; f<100; f++)
               if(p->frameno+f<p->fcount && p->csdata[p->frameno+f]==checksum)
                  break;
               else if(p->frameno-f>=0 && p->csdata[p->frameno-f]==checksum)
                  {
                  f=-f;
                  break;
                  }

            if(f<100)
               {
               mp_msg(MSGT_VFILTER, MSGL_INFO,
                      "\n%s: Mismatch with pass-1: %+d frame(s).\n",
                      vf->info->name, f);

               p->frameno+=f;
               p->misscount=0;
               }
            else if(p->misscount++>=30)
               {
               mp_msg(MSGT_VFILTER, MSGL_ERR,
                      "\n%s: Sync with pass-1 lost! "
                      "Switching to one pass mode.\n", vf->info->name);
               p->pass=0;
               break;
               }
            }

         n=(p->frameno)/5;
         if(n>=p->bcount) n=p->bcount-1;

         newphase=p->bdata[n];
         break;

      default:
         if(p->frameno)
            {
            int *sump=p->sum+p->frameno%5,
               *histp=p->history+p->frameno%p->window;

            *sump-=*histp;
            *sump+=(*histp=imgop(diff_plane, dmpi, mpi, 0));
            }

         m=match(p, p->sum, -1, -1, &d);

         if(d>=p->threshold)
            newphase=m;
      }

   n=p->ocount++%5;

   if(newphase!=p->phase && ((p->phase+4)%5<n)==((newphase+4)%5<n))
      {
      p->phase=newphase;
      mp_msg(MSGT_VFILTER, MSGL_STATUS,
             "\n%s: Telecine phase %d.\n", vf->info->name, p->phase);
      }

   switch((p->frameno++-p->phase+10)%5)
      {
      case 0:
         imgop(copyop, dmpi, mpi, 0);
         return 0;

      case 4:
         if(p->deghost>0)
            {
            tmpi=vf_get_image(vf->next, mpi->imgfmt,
                              MP_IMGTYPE_TEMP, MP_IMGFLAG_ACCEPT_STRIDE |
                              MP_IMGFLAG_READABLE,
                              mpi->width, mpi->height);
            vf_clone_mpi_attributes(tmpi, mpi);

            imgop(copyop, tmpi, mpi, 0);
            imgop(deghost_plane, tmpi, dmpi, p->deghost);
            imgop(copyop, dmpi, mpi, 0);
            return vf_next_put_image(vf, tmpi, MP_NOPTS_VALUE);
            }
      }

   imgop(copyop, dmpi, mpi, 0);
   return vf_next_put_image(vf, dmpi, MP_NOPTS_VALUE);
   }

static int analyze(struct vf_priv_s *p)
   {
   int *buf=0, *bp, bufsize=0, n, b, f, i, j, m, s;
   unsigned int *cbuf=0, *cp;
   int8_t *pbuf;
   int8_t lbuf[256];
   int sum[5];
   double d;

   /* read the file */

   n=15;
   while(fgets(lbuf, 256, p->file))
      {
      if(n>=bufsize-19)
         {
         bufsize=bufsize?bufsize*2:30000;
         if((bp=realloc(buf, bufsize*sizeof *buf))) buf=bp;
         if((cp=realloc(cbuf, bufsize*sizeof *cbuf))) cbuf=cp;

         if(!bp || !cp)
            {
            mp_msg(MSGT_VFILTER, MSGL_FATAL, "%s: Not enough memory.\n",
                   vf_info_divtc.name);
            free(buf);
            free(cbuf);
            return 0;
            }
         }
      sscanf(lbuf, "%x %d", cbuf+n, buf+n);
      n++;
      }

   if(!n)
      {
      mp_msg(MSGT_VFILTER, MSGL_FATAL, "%s: Empty 2-pass log file.\n",
             vf_info_divtc.name);
      free(buf);
      free(cbuf);
      return 0;
      }

   /* generate some dummy data past the beginning and end of the array */

   buf+=15, cbuf+=15;
   n-=15;

   memcpy(buf-15, buf, 15*sizeof *buf);
   memset(cbuf-15, 0, 15*sizeof *cbuf);

   while(n%5)
      buf[n]=buf[n-5], cbuf[n]=0, n++;

   memcpy(buf+n, buf+n-15, 15*sizeof *buf);
   memset(cbuf+n, 0, 15*sizeof *cbuf);

   p->csdata=cbuf;
   p->fcount=n;

   /* array with one slot for each slice of 5 frames */

   p->bdata=pbuf=malloc(p->bcount=b=(n/5));
   memset(pbuf, 255, b);

   /* resolve the automatic mode */

   if(p->deghost<0)
      {
      int deghost=-p->deghost;
      double s0=0.0, s1=0.0;

      for(f=0; f<n; f+=5)
         {
         p->deghost=0; match(p, buf+f, -1, -1, &d); s0+=d;
         p->deghost=1; match(p, buf+f, -1, -1, &d); s1+=d;
         }

      p->deghost=s1>s0?deghost:0;

      mp_msg(MSGT_VFILTER, MSGL_INFO,
             "%s: Deghosting %-3s (relative pattern strength %+.2fdB).\n",
             vf_info_divtc.name,
             p->deghost?"ON":"OFF",
             10.0*log10(s1/s0));
      }

   /* analyze the data */

   for(f=0; f<5; f++)
      for(sum[f]=0, n=-15; n<20; n+=5)
         sum[f]+=buf[n+f];

   for(f=0; f<b; f++)
      {
      m=match(p, sum, -1, -1, &d);

      if(d>=p->threshold)
         pbuf[f]=m;

      if(f<b-1)
         for(n=0; n<5; n++)
            sum[n]=sum[n]-buf[5*(f-3)+n]+buf[5*(f+4)+n];
      }

   /* fill in the gaps */

   /* the beginning */
   for(f=0; f<b && pbuf[f]==-1; f++);

   if(f==b)
      {
      free(buf-15);
      mp_msg(MSGT_VFILTER, MSGL_FATAL, "%s: No telecine pattern found!\n",
             vf_info_divtc.name);
      return 0;
      }

   for(n=0; n<f; pbuf[n++]=pbuf[f]);

   /* the end */
   for(f=b-1; pbuf[f]==-1; f--);
   for(n=f+1; n<b; pbuf[n++]=pbuf[f]);

   /* the rest */
   for(f=0;;)
      {
      while(f<b && pbuf[f]!=-1) f++;
      if(f==b) break;
      for(n=f; pbuf[n]==-1; n++);

      if(pbuf[f-1]==pbuf[n])
         {
         /* just a gap */
         while(f<n) pbuf[f++]=pbuf[n];
         }
      else
         {
         /* phase change, reanalyze the original data in the gap with zero
            threshold for only the two phases that appear at the ends */

         for(i=0; i<5; i++)
            for(sum[i]=0, j=5*f-15; j<5*f; j+=5)
               sum[i]+=buf[i+j];

         for(i=f; i<n; i++)
            {
            pbuf[i]=match(p, sum, pbuf[f-1], pbuf[n], 0);

            for(j=0; j<5; j++)
               sum[j]=sum[j]-buf[5*(i-3)+j]+buf[5*(i+4)+j];
            }

         /* estimate the transition point by dividing the gap
            in the same proportion as the number of matches of each kind */

         for(i=f, m=f; i<n; i++)
            if(pbuf[i]==pbuf[f-1]) m++;

         /* find the transition of the right direction nearest to the
            estimated point */

         if(m>f && m<n)
            {
            for(j=m; j>f; j--)
               if(pbuf[j-1]==pbuf[f-1] && pbuf[j]==pbuf[n]) break;
            for(s=m; s<n; s++)
               if(pbuf[s-1]==pbuf[f-1] && pbuf[s]==pbuf[n]) break;

            m=(s-m<m-j)?s:j;
            }

         /* and rewrite the data to allow only this one transition */

         for(i=f; i<m; i++)
            pbuf[i]=pbuf[f-1];

         for(; i<n; i++)
            pbuf[i]=pbuf[n];

         f=n;
         }
      }

   free(buf-15);

   return 1;
   }

static int query_format(struct vf_instance *vf, unsigned int fmt)
   {
   switch(fmt)
      {
      case IMGFMT_444P: case IMGFMT_IYUV: case IMGFMT_RGB24:
      case IMGFMT_422P: case IMGFMT_UYVY: case IMGFMT_BGR24:
      case IMGFMT_411P: case IMGFMT_YUY2: case IMGFMT_IF09:
      case IMGFMT_YV12: case IMGFMT_I420: case IMGFMT_YVU9:
      case IMGFMT_IUYV: case IMGFMT_Y800: case IMGFMT_Y8:
         return vf_next_query_format(vf,fmt);
      }

   return 0;
   }

static void uninit(struct vf_instance *vf)
   {
   if(vf->priv)
      {
      if(vf->priv->file) fclose(vf->priv->file);
      if(vf->priv->csdata) free(vf->priv->csdata-15);
      free(vf->priv->bdata);
      free(vf->priv->history);
      free(vf->priv);
      }
   }

static int vf_open(vf_instance_t *vf, char *args)
   {
   struct vf_priv_s *p;
   const char *filename="framediff.log";
   char *ap, *q, *a;

   if(args && !(args=av_strdup(args)))
      {
   nomem:
      mp_msg(MSGT_VFILTER, MSGL_FATAL,
             "%s: Not enough memory.\n", vf->info->name);
   fail:
      uninit(vf);
      free(args);
      return 0;
      }

   vf->put_image=put_image;
   vf->uninit=uninit;
   vf->query_format=query_format;
   vf->default_reqs=VFCAP_ACCEPT_STRIDE;
   if(!(vf->priv=p=calloc(1, sizeof(struct vf_priv_s))))
      goto nomem;

   p->phase=5;
   p->threshold=0.5;
   p->window=30;

   if((ap=args))
      while(*ap)
         {
         q=ap;
         if((ap=strchr(q, ':'))) *ap++=0; else ap=q+strlen(q);
         if((a=strchr(q, '='))) *a++=0; else a=q+strlen(q);

         switch(*q)
            {
            case 0:                              break;
            case 'f': filename=a;                break;
            case 't': p->threshold=atof(a);      break;
            case 'w': p->window=5*(atoi(a)+4)/5; break;
            case 'd': p->deghost=atoi(a);        break;
            case 'p':
               if(q[1]=='h') p->phase=atoi(a);
               else p->pass=atoi(a);
               break;

            case 'h':
               mp_msg(MSGT_VFILTER, MSGL_INFO,
                      "\n%s options:\n\n"
                      "pass=1|2         - Use 2-pass mode.\n"
                      "file=filename    - Set the 2-pass log file name "
                      "(default %s).\n"
                      "threshold=value  - Set the pattern recognition "
                      "sensitivity (default %g).\n"
                      "deghost=value    - Select deghosting threshold "
                      "(default %d).\n"
                      "window=numframes - Set the statistics window "
                      "for 1-pass mode (default %d).\n"
                      "phase=0|1|2|3|4  - Set the initial phase "
                      "for 1-pass mode (default %d).\n\n"
                      "The option names can be abbreviated to the shortest "
                      "unique prefix.\n\n",
                      vf->info->name, filename, p->threshold, p->deghost,
                      p->window, p->phase%5);
               break;

            default:
               mp_msg(MSGT_VFILTER, MSGL_FATAL,
                      "%s: Unknown argument %s.\n", vf->info->name, q);
               goto fail;
            }
         }

   switch(p->pass)
      {
      case 1:
         if(!(p->file=fopen(filename, "w")))
            {
            mp_msg(MSGT_VFILTER, MSGL_FATAL,
                   "%s: Can't create file %s.\n", vf->info->name, filename);
            goto fail;
            }

         break;

      case 2:
         if(!(p->file=fopen(filename, "r")))
            {
            mp_msg(MSGT_VFILTER, MSGL_FATAL,
                   "%s: Can't open file %s.\n", vf->info->name, filename);
            goto fail;
            }

         if(!analyze(p))
            goto fail;

         fclose(p->file);
         p->file=0;
         break;
      }

   if(p->window<5) p->window=5;
   if(!(p->history=calloc(sizeof *p->history, p->window)))
      goto nomem;

   diff = diff_C;
#if HAVE_MMX && HAVE_EBX_AVAILABLE
   if(gCpuCaps.hasMMX) diff = diff_MMX;
#endif

   free(args);
   return 1;
   }

const vf_info_t vf_info_divtc =
   {
   "inverse telecine for deinterlaced video",
   "divtc",
   "Ville Saari",
   "",
   vf_open,
   NULL
   };
