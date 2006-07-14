/*
 * linear least squares model
 *
 * Copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
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

/**
 * @file lls.c
 * linear least squares model
 */

#include <math.h>
#include <string.h>

#include "lls.h"

#ifdef TEST
#define av_log(a,b,...) printf(__VA_ARGS__)
#endif

void av_init_lls(LLSModel *m, int indep_count){
    memset(m, 0, sizeof(LLSModel));

    m->indep_count= indep_count;
}

void av_update_lls(LLSModel *m, double *var, double decay){
    int i,j;

    for(i=0; i<=m->indep_count; i++){
        for(j=i; j<=m->indep_count; j++){
            m->covariance[i][j] *= decay;
            m->covariance[i][j] += var[i]*var[j];
        }
    }
}

double av_solve_lls(LLSModel *m, double threshold){
    int i,j,k;
    double (*factor)[MAX_VARS+1]= &m->covariance[1][0];
    double (*covar )[MAX_VARS+1]= &m->covariance[1][1];
    double  *covar_y            =  m->covariance[0];
    double variance;
    int count= m->indep_count;

    for(i=0; i<count; i++){
        for(j=i; j<count; j++){
            double sum= covar[i][j];

            for(k=i-1; k>=0; k--)
                sum -= factor[i][k]*factor[j][k];

            if(i==j){
                if(sum < threshold)
                    sum= 1.0;
                factor[i][i]= sqrt(sum);
            }else
                factor[j][i]= sum / factor[i][i];
        }
    }
    for(i=0; i<count; i++){
        double sum= covar_y[i+1];
        for(k=i-1; k>=0; k--)
            sum -= factor[i][k]*m->coeff[k];
        m->coeff[i]= sum / factor[i][i];
    }

    for(i=count-1; i>=0; i--){
        double sum= m->coeff[i];
        for(k=i+1; k<count; k++)
            sum -= factor[k][i]*m->coeff[k];
        m->coeff[i]= sum / factor[i][i];
    }

    variance= covar_y[0];
    for(i=0; i<count; i++){
        double sum= m->coeff[i]*covar[i][i] - 2*covar_y[i+1];
        for(j=0; j<i; j++)
            sum += 2*m->coeff[j]*covar[j][i];
        variance += m->coeff[i]*sum;
    }
    return variance;
}

double av_evaluate_lls(LLSModel *m, double *param){
    int i;
    double out= 0;

    for(i=0; i<m->indep_count; i++)
        out+= param[i]*m->coeff[i];

    return out;
}

#ifdef TEST

#include <stdlib.h>
#include <stdio.h>

int main(){
    LLSModel m;
    int i;

    av_init_lls(&m, 3);

    for(i=0; i<100; i++){
        double var[4];
        double eval, variance;
        var[1] = rand() / (double)RAND_MAX;
        var[2] = rand() / (double)RAND_MAX;
        var[3] = rand() / (double)RAND_MAX;

        var[2]= var[1] + var[3];

        var[0] = var[1] + var[2] + var[3] +  var[1]*var[2]/100;

        eval= av_evaluate_lls(&m, var+1);
        av_update_lls(&m, var, 0.99);
        variance= av_solve_lls(&m, 0.001);
        av_log(NULL, AV_LOG_DEBUG, "real:%f pred:%f var:%f coeffs:%f %f %f\n",
            var[0], eval, sqrt(variance / (i+1)),
            m.coeff[0], m.coeff[1], m.coeff[2]);
    }
    return 0;
}

#endif
