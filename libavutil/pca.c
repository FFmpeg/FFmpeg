/*
 * principal component analysis (PCA)
 * Copyright (c) 2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * principal component analysis (PCA)
 */

#include "common.h"
#include "pca.h"

typedef struct PCA{
    int count;
    int n;
    double *covariance;
    double *mean;
}PCA;

PCA *ff_pca_init(int n){
    PCA *pca;
    if(n<=0)
        return NULL;

    pca= av_mallocz(sizeof(PCA));
    pca->n= n;
    pca->count=0;
    pca->covariance= av_mallocz(sizeof(double)*n*n);
    pca->mean= av_mallocz(sizeof(double)*n);

    return pca;
}

void ff_pca_free(PCA *pca){
    av_freep(&pca->covariance);
    av_freep(&pca->mean);
    av_free(pca);
}

void ff_pca_add(PCA *pca, double *v){
    int i, j;
    const int n= pca->n;

    for(i=0; i<n; i++){
        pca->mean[i] += v[i];
        for(j=i; j<n; j++)
            pca->covariance[j + i*n] += v[i]*v[j];
    }
    pca->count++;
}

int ff_pca(PCA *pca, double *eigenvector, double *eigenvalue){
    int i, j, pass;
    int k=0;
    const int n= pca->n;
    double z[n];

    memset(eigenvector, 0, sizeof(double)*n*n);

    for(j=0; j<n; j++){
        pca->mean[j] /= pca->count;
        eigenvector[j + j*n] = 1.0;
        for(i=0; i<=j; i++){
            pca->covariance[j + i*n] /= pca->count;
            pca->covariance[j + i*n] -= pca->mean[i] * pca->mean[j];
            pca->covariance[i + j*n] = pca->covariance[j + i*n];
        }
        eigenvalue[j]= pca->covariance[j + j*n];
        z[j]= 0;
    }

    for(pass=0; pass < 50; pass++){
        double sum=0;

        for(i=0; i<n; i++)
            for(j=i+1; j<n; j++)
                sum += fabs(pca->covariance[j + i*n]);

        if(sum == 0){
            for(i=0; i<n; i++){
                double maxvalue= -1;
                for(j=i; j<n; j++){
                    if(eigenvalue[j] > maxvalue){
                        maxvalue= eigenvalue[j];
                        k= j;
                    }
                }
                eigenvalue[k]= eigenvalue[i];
                eigenvalue[i]= maxvalue;
                for(j=0; j<n; j++){
                    double tmp= eigenvector[k + j*n];
                    eigenvector[k + j*n]= eigenvector[i + j*n];
                    eigenvector[i + j*n]= tmp;
                }
            }
            return pass;
        }

        for(i=0; i<n; i++){
            for(j=i+1; j<n; j++){
                double covar= pca->covariance[j + i*n];
                double t,c,s,tau,theta, h;

                if(pass < 3 && fabs(covar) < sum / (5*n*n)) //FIXME why pass < 3
                    continue;
                if(fabs(covar) == 0.0) //FIXME should not be needed
                    continue;
                if(pass >=3 && fabs((eigenvalue[j]+z[j])/covar) > (1LL<<32) && fabs((eigenvalue[i]+z[i])/covar) > (1LL<<32)){
                    pca->covariance[j + i*n]=0.0;
                    continue;
                }

                h= (eigenvalue[j]+z[j]) - (eigenvalue[i]+z[i]);
                theta=0.5*h/covar;
                t=1.0/(fabs(theta)+sqrt(1.0+theta*theta));
                if(theta < 0.0) t = -t;

                c=1.0/sqrt(1+t*t);
                s=t*c;
                tau=s/(1.0+c);
                z[i] -= t*covar;
                z[j] += t*covar;

#define ROTATE(a,i,j,k,l) {\
    double g=a[j + i*n];\
    double h=a[l + k*n];\
    a[j + i*n]=g-s*(h+g*tau);\
    a[l + k*n]=h+s*(g-h*tau); }
                for(k=0; k<n; k++) {
                    if(k!=i && k!=j){
                        ROTATE(pca->covariance,FFMIN(k,i),FFMAX(k,i),FFMIN(k,j),FFMAX(k,j))
                    }
                    ROTATE(eigenvector,k,i,k,j)
                }
                pca->covariance[j + i*n]=0.0;
            }
        }
        for (i=0; i<n; i++) {
            eigenvalue[i] += z[i];
            z[i]=0.0;
        }
    }

    return -1;
}

#ifdef TEST

#undef printf
#include <stdio.h>
#include <stdlib.h>
#include "lfg.h"

int main(void){
    PCA *pca;
    int i, j, k;
#define LEN 8
    double eigenvector[LEN*LEN];
    double eigenvalue[LEN];
    AVLFG prng;

    av_lfg_init(&prng, 1);

    pca= ff_pca_init(LEN);

    for(i=0; i<9000000; i++){
        double v[2*LEN+100];
        double sum=0;
        int pos = av_lfg_get(&prng) % LEN;
        int v2  = av_lfg_get(&prng) % 101 - 50;
        v[0]    = av_lfg_get(&prng) % 101 - 50;
        for(j=1; j<8; j++){
            if(j<=pos) v[j]= v[0];
            else       v[j]= v2;
            sum += v[j];
        }
/*        for(j=0; j<LEN; j++){
            v[j] -= v[pos];
        }*/
//        sum += av_lfg_get(&prng) % 10;
/*        for(j=0; j<LEN; j++){
            v[j] -= sum/LEN;
        }*/
//        lbt1(v+100,v+100,LEN);
        ff_pca_add(pca, v);
    }


    ff_pca(pca, eigenvector, eigenvalue);
    for(i=0; i<LEN; i++){
        pca->count= 1;
        pca->mean[i]= 0;

//        (0.5^|x|)^2 = 0.5^2|x| = 0.25^|x|


//        pca.covariance[i + i*LEN]= pow(0.5, fabs
        for(j=i; j<LEN; j++){
            printf("%f ", pca->covariance[i + j*LEN]);
        }
        printf("\n");
    }

#if 1
    for(i=0; i<LEN; i++){
        double v[LEN];
        double error=0;
        memset(v, 0, sizeof(v));
        for(j=0; j<LEN; j++){
            for(k=0; k<LEN; k++){
                v[j] += pca->covariance[FFMIN(k,j) + FFMAX(k,j)*LEN] * eigenvector[i + k*LEN];
            }
            v[j] /= eigenvalue[i];
            error += fabs(v[j] - eigenvector[i + j*LEN]);
        }
        printf("%f ", error);
    }
    printf("\n");
#endif
    for(i=0; i<LEN; i++){
        for(j=0; j<LEN; j++){
            printf("%9.6f ", eigenvector[i + j*LEN]);
        }
        printf("  %9.1f %f\n", eigenvalue[i], eigenvalue[i]/eigenvalue[0]);
    }

    return 0;
}
#endif
