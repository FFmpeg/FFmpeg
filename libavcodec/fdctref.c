/**
 * @file fdctref.c
 * forward discrete cosine transform, double precision.
 */

/* Copyright (C) 1996, MPEG Software Simulation Group. All Rights Reserved. */

/*
 * Disclaimer of Warranty
 *
 * These software programs are available to the user without any license fee or
 * royalty on an "as is" basis.  The MPEG Software Simulation Group disclaims
 * any and all warranties, whether express, implied, or statuary, including any
 * implied warranties or merchantability or of fitness for a particular
 * purpose.  In no event shall the copyright-holder be liable for any
 * incidental, punitive, or consequential damages of any kind whatsoever
 * arising from the use of these programs.
 *
 * This disclaimer of warranty extends to the user of these programs and user's
 * customers, employees, agents, transferees, successors, and assigns.
 *
 * The MPEG Software Simulation Group does not represent or warrant that the
 * programs furnished hereunder are free of infringement of any third-party
 * patents.
 *
 * Commercial implementations of MPEG-1 and MPEG-2 video, including shareware,
 * are subject to royalty fees to patent holders.  Many of these patents are
 * general enough such that they are unavoidable regardless of implementation
 * design.
 *
 */

#include <math.h>

#ifndef PI
# ifdef M_PI
#  define PI M_PI
# else
#  define PI 3.14159265358979323846
# endif
#endif

/* global declarations */
void init_fdct (void);
void fdct (short *block);

/* private data */
static double c[8][8]; /* transform coefficients */

void init_fdct()
{
  int i, j;
  double s;

  for (i=0; i<8; i++)
  {
    s = (i==0) ? sqrt(0.125) : 0.5;

    for (j=0; j<8; j++)
      c[i][j] = s * cos((PI/8.0)*i*(j+0.5));
  }
}

void fdct(block)
short *block;
{
	register int i, j;
	double s;
	double tmp[64];

	for(i = 0; i < 8; i++)
    	for(j = 0; j < 8; j++)
    	{
    		s = 0.0;

/*
 *     		for(k = 0; k < 8; k++)
 *         		s += c[j][k] * block[8 * i + k];
 */
        	s += c[j][0] * block[8 * i + 0];
        	s += c[j][1] * block[8 * i + 1];
        	s += c[j][2] * block[8 * i + 2];
        	s += c[j][3] * block[8 * i + 3];
        	s += c[j][4] * block[8 * i + 4];
        	s += c[j][5] * block[8 * i + 5];
        	s += c[j][6] * block[8 * i + 6];
        	s += c[j][7] * block[8 * i + 7];

    		tmp[8 * i + j] = s;
    	}

	for(j = 0; j < 8; j++)
    	for(i = 0; i < 8; i++)
    	{
    		s = 0.0;

/*
 *     	  	for(k = 0; k < 8; k++)
 *        	    s += c[i][k] * tmp[8 * k + j];
 */
        	s += c[i][0] * tmp[8 * 0 + j];
        	s += c[i][1] * tmp[8 * 1 + j];
        	s += c[i][2] * tmp[8 * 2 + j];
        	s += c[i][3] * tmp[8 * 3 + j];
        	s += c[i][4] * tmp[8 * 4 + j];
        	s += c[i][5] * tmp[8 * 5 + j];
        	s += c[i][6] * tmp[8 * 6 + j];
        	s += c[i][7] * tmp[8 * 7 + j];
		s*=8.0;

    		block[8 * i + j] = (short)floor(s + 0.499999);
/*
 * reason for adding 0.499999 instead of 0.5:
 * s is quite often x.5 (at least for i and/or j = 0 or 4)
 * and setting the rounding threshold exactly to 0.5 leads to an
 * extremely high arithmetic implementation dependency of the result;
 * s being between x.5 and x.500001 (which is now incorrectly rounded
 * downwards instead of upwards) is assumed to occur less often
 * (if at all)
 */
      }
}

/* perform IDCT matrix multiply for 8x8 coefficient block */

void idct(block)
short *block;
{
  int i, j, k, v;
  double partial_product;
  double tmp[64];

  for (i=0; i<8; i++)
    for (j=0; j<8; j++)
    {
      partial_product = 0.0;

      for (k=0; k<8; k++)
        partial_product+= c[k][j]*block[8*i+k];

      tmp[8*i+j] = partial_product;
    }

  /* Transpose operation is integrated into address mapping by switching 
     loop order of i and j */

  for (j=0; j<8; j++)
    for (i=0; i<8; i++)
    {
      partial_product = 0.0;

      for (k=0; k<8; k++)
        partial_product+= c[k][i]*tmp[8*k+j];

      v = (int) floor(partial_product+0.5);
      block[8*i+j] = v;
    }
}
