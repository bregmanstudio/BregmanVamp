/* iirfilter.c */

/* Author: Michael A. Casey
 * Language: C
 *
 * Implementation of filter opcode for general purpose filtering.
 * This opcode implements the following difference equation:
 *
 * (1)*y(n) = b(0)*x(n) + b(1)*x(n-1) + ... + b(nb)*x(n-nb)
 *                      - a(1)*y(n-1) - ... - a(na)*y(n-na)
 *
 * whose system function is represented by:
 *
 *                               -1              -nb
 *        jw  B(z)   b(0) + b(1)z + .... + b(nb)z
 *     H(e) = ---- = ----------------------------
 *                               -1              -na
 *            A(z)    1   + a(1)z + .... + a(na)z
 *
 *
 * This is the same as scipy.signal's lfilter and Matlab's filter:
 * It is a Direct Form II Transposed Filter:
 *                              -1               -nb
 *                  b[0] + b[1]z  + ... + b[nb] z
 *          Y(z) = ---------------------------------- X(z)
 *                              -1               -na
 *                    1  + a[1]z  + ... + a[na] z    
 *
 *
 * Copyright (C) 1997 Michael A. Casey, MIT Media Lab, All Rights Reserved
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "iirfilter.h"


static sampleT readFilter(FILTER*, int);
static void insertFilter(FILTER*,sampleT);

#ifndef MAX
#define MAX(a,b) ((a>b)?(a):(b))
#define MIN(a,b) ((a>b)?(b):(a))
#endif

/*#define POLISH (1) */     /* 1=polish pole roots after Laguer root finding */

typedef struct FPOLAR {sampleT mag,ph;} fpolar;

/* Routines associated with pole control */
static void expandPoly(fcomplex[], sampleT[], int);
static void complex2polar(fcomplex[],fpolar[], int);
static void polar2complex(fpolar[],fcomplex[], int);
static void sortRoots(fcomplex roots[], int dim);
static int sortfun(fpolar *a, fpolar *b);
static void nudgeMags(fpolar a[], fcomplex b[], int dim, sampleT fact);
static void nudgePhases(fpolar a[], fcomplex b[], int dim, sampleT fact);

static void zroots(fcomplex [], int, fcomplex []);
static fcomplex Cadd(fcomplex, fcomplex);
static fcomplex Csub(fcomplex, fcomplex);
static fcomplex Cmul(fcomplex, fcomplex);
static fcomplex Cdiv(fcomplex, fcomplex);
static fcomplex Complex(sampleT, sampleT);
static sampleT Cabs(fcomplex);
static fcomplex Csqrt(fcomplex);
static fcomplex RCmul(sampleT, fcomplex);

/* Filter initialization routine */
int ifilter(FILTER* p)
{
    int i;

    /* since i-time arguments are not guaranteed to propegate to p-time
     * we must copy the i-vars into the p structure.
     */

    /* First check bounds on initialization arguments */
    if ((p->numb<1) || (p->numb>(MAXZEROS+1)) ||
    (p->numa<0) || (p->numa>MAXPOLES)){
      fprintf(stderr, "Filter order out of bounds: (1 <= nb(%d) < 51, 0 <= na(%d) <= 50)", p->numb, p->numa);
      return 0;
    }

    /* Calculate the total delay in samples and allocate memory for it */
    p->ndelay = MAX(p->numb-1,p->numa);
    p->delay = (sampleT*) calloc(p->ndelay, sizeof(sampleT));

    /* Set current position pointer to beginning of delay */
    p->currPos = p->delay;

    return OK;
}

void free_filter(FILTER* p){  
  if(p->delay!=NULL){
    free(p->delay);
  }
  free(p);
}

/* izfilter - initialize z-plane controllable filter */
int izfilter(ZFILTER *p)
{
    fcomplex a[MAXPOLES];
    fcomplex *roots;
    sampleT *coeffs;
    int i, dim;

    /* since i-time arguments are not guaranteed to propagate to p-time
     * we must copy the i-vars into the p structure.
     */

    /* First check bounds on initialization arguments */
    if ((p->numb<1) || (p->numb>(MAXZEROS+1)) ||
                 (p->numa<0) || (p->numa>MAXPOLES)){
      fprintf(stderr, "Filter order out of bounds: (1 <= nb < 51, 0 <= na <= 50)");
      return 0;
    }
    
    /* Calculate the total delay in samples and allocate memory for it */
    p->ndelay = MAX(p->numb-1,p->numa);    
    p->delay = (sampleT*) calloc(p->ndelay, sizeof(sampleT));

    /* Set current position pointer to beginning of delay */
    p->currPos = p->delay;

    /* Add auxillary root memory */
    p->roots = (fcomplex*) calloc(p->numa, sizeof(fcomplex));
    roots = p->roots;
    dim = p->numa;

    coeffs = p->coeffs + p->numb;

    /* Reverse coefficient order for root finding */
    a[dim] = Complex(1.0,0.0);
    for (i=dim-1; i>=0; i--)
      a[i] = Complex(coeffs[dim-i-1],0.0);

    /* NRIC root finding routine, a[0..M] roots[1..M] */
    zroots(a, dim,  roots-1);

    /* Sort roots into descending order of magnitudes */
    sortRoots(roots, dim);
    return OK;
}

void free_zfilter(ZFILTER* p){  
  if(p->delay!=NULL){
    free(p->delay);
  }
  free(p);
}

/* a-rate filter routine
 *
 * Implements the following difference equation
 *
 * (1)*y(n) = b(0)*x(n) + b(1)*x(n-1) + ... + b(nb)*x(n-nb)
 *                      - a(1)*y(n-1) - ... - a(na)*y(n-na)
 *
 */
int afilter(FILTER* p, uint32_t nsmps)
{
    int      i;
    uint32_t offset = 0; //p->h.insdshead->ksmps_offset;
    uint32_t early  = 0; //p->h.insdshead->ksmps_no_end;
    uint32_t n;

    sampleT* a = p->coeffs+p->numb;
    sampleT* b = p->coeffs+1;
    sampleT  b0 = p->coeffs[0];

    sampleT poleSamp, zeroSamp, inSamp;

    /* Outer loop */
    /*if (UNLIKELY(offset)) memset(p->out, '\0', offset*sizeof(sampleT));
    if (UNLIKELY(early)) {
      nsmps -= early;
      memset(&p->out[nsmps], '\0', early*sizeof(sampleT));
      }*/
    for (n=offset; n<nsmps; n++) {

      inSamp = p->in[n];
      poleSamp = inSamp;
      zeroSamp = 0.0;

      /* Inner filter loop */
      for (i=0; i< p->ndelay; i++) {

        /* Do poles first */
        /* Sum of products of a's and delays */
        if (i<p->numa)
          poleSamp += -(a[i])*readFilter(p,i+1);

        /* Now do the zeros */
        if (i<(p->numb-1))
          zeroSamp += (b[i])*readFilter(p,i+1);

      }

      p->out[n] = (b0)*poleSamp + zeroSamp;
      /* update filter delay line */
      insertFilter(p, poleSamp);
    }
    return OK;
}

/* k-rate filter routine
 *
 * Implements the following difference equation at the k rate
 *
 * (1)*y(k) = b(0)*x(k) + b(1)*x(k-1) + ... + b(nb)*x(k-nb)
 *                      - a(1)*y(k-1) - ... - a(na)*y(k-na)
 *
 */
int kfilter(FILTER* p)
{
    int i;

    sampleT* a = p->coeffs+p->numb;
    sampleT* b = p->coeffs+1;
    sampleT  b0 = p->coeffs[0];

    sampleT poleSamp, zeroSamp, inSamp;

    inSamp = *p->in;
    poleSamp = inSamp;
    zeroSamp = 0.0;

    /* Filter loop */
    for (i=0; i<p->ndelay; i++) {

      /* Do poles first */
      /* Sum of products of a's and delays */
      if (i<p->numa)
        poleSamp += -(a[i])*readFilter(p,i+1);

      /* Now do the zeros */
      if (i<(p->numb-1))
        zeroSamp += (b[i])*readFilter(p,i+1);
    }

    *p->out = ((b0)*poleSamp + zeroSamp);

    /* update filter delay line */
    insertFilter(p, poleSamp);
    return OK;
}

/* azfilter - a-rate controllable pole filter
 *
 * This filter allows control over the magnitude
 * and frequency response of the filter by efficient
 * manipulation of the poles.
 *
 * The k-rate controls are:
 *
 * kmag, kfreq
 *
 * The rest of the filter is the same as filter
 *
 */
int azfilter(ZFILTER* p, uint32_t nsmps)
{
    int      i;
    uint32_t offset = 0; // p->h.insdshead->ksmps_offset;
    uint32_t early  = 0; // p->h.insdshead->ksmps_no_end;
    uint32_t n;

    sampleT* a = p->coeffs+p->numb;
    sampleT* b = p->coeffs+1;
    sampleT  b0 = p->coeffs[0];

    sampleT poleSamp, zeroSamp, inSamp;

    fpolar B[MAXPOLES];
    fcomplex C[MAXPOLES+1];

    fcomplex *roots = p->roots;
    sampleT kmagf = *p->kmagf; /* Mag nudge factor */
    sampleT kphsf = *p->kphsf; /* Phs nudge factor */

    int dim = p->numa;

    /* Nudge pole magnitudes */
    complex2polar(roots,B,dim);
    nudgeMags(B,roots,dim,kmagf);
    nudgePhases(B,roots,dim,kphsf);
    polar2complex(B,C,dim);
    expandPoly(C,a,dim);

    /* C now contains the complex roots of the nudged filter */
    /* and a contains their associated real coefficients. */

    /* Outer loop */
    /*if (UNLIKELY(offset)) memset(p->out, '\0', offset*sizeof(sampleT));
    if (UNLIKELY(early)) {
      nsmps -= early;
      memset(&p->out[nsmps], '\0', early*sizeof(sampleT));
    }
    */
    for (n=offset; n<nsmps; n++) {
      inSamp = p->in[n];
      poleSamp = inSamp;
      zeroSamp = 0.0;

      /* Inner filter loop */
      for (i=0; i< p->ndelay; i++) {

        /* Do poles first */
        /* Sum of products of a's and delays */
        if (i<p->numa)
          poleSamp += -(a[i])*readFilter((FILTER*)p,i+1);

        /* Now do the zeros */
        if (i<(p->numb-1))
          zeroSamp += (b[i])*readFilter((FILTER*)p,i+1);
      }

      p->out[n] = (b0)*poleSamp + zeroSamp;

      /* update filter delay line */
      insertFilter((FILTER*)p, poleSamp);
    }
    return OK;
}

/* readFilter -- delay-line access routine
 *
 * Reads sample x[n-i] from a previously established delay line.
 * With this syntax i is +ve for a time delay and -ve for a time advance.
 *
 * The use of explicit indexing rather than implicit index incrementing
 * allows multiple lattice structures to access the same delay line.
 *
 */
static inline sampleT readFilter(FILTER* p, int i)
{
    sampleT* readPoint; /* Generic pointer address */

    /* Calculate the address of the index for this read */
    readPoint = p->currPos - i;

    /* Wrap around for time-delay if necessary */
    if (readPoint < (p->delay) )
      readPoint += p->ndelay;
    else
      /* Wrap for time-advance if necessary */
      if (readPoint > (p->delay + (p->ndelay-1)) )
        readPoint -= p->ndelay;

    return *readPoint; /* Dereference read address for delayed value */
}

/* insertFilter -- delay-line update routine
 *
 * Inserts the passed value into the currPos and increments the
 * currPos pointer modulo the length of the delay line.
 *
 */
static inline void insertFilter(FILTER* p, sampleT val)
{
    /* Insert the passed value into the delay line */
    *p->currPos = val;

    /* Update the currPos pointer and wrap modulo the delay length */
    if ((++p->currPos) >
        (p->delay + (p->ndelay-1)) )
      p->currPos -= p->ndelay;
}

/* Compute polynomial coefficients from the roots */
/* The expanded polynomial is computed as a[0..N] in
 * descending powers of Z
 */
static void expandPoly(fcomplex roots[], sampleT a[], int dim)
{
    int j,k;
    fcomplex z[MAXPOLES],d[MAXPOLES];

    z[0] = Complex(1.0, 0.0);
    for (j=1;j<=dim;j++)
      z[j] = Complex(0.0,0.0);

    /* Recursive coefficient expansion about the roots of A(Z) */
    for (j=0;j<dim;j++) {
      for (k=0;k<dim;k++)
        d[k]=z[k]; /* Store last vector of coefficients */
      for (k=1;k<=j+1;k++)
        z[k] = Csub(z[k],Cmul(roots[j], d[k-1]));
    }
    for (j=0;j<dim;j++)
      (a[j]) = z[j+1].r;
}

#define SQR(a) (a*a)

static void complex2polar(fcomplex a[], fpolar b[], int N)
{
    int i;

    for (i=0; i<N; i++) {
      b[i].mag = hypot(a[i].r,a[i].i);
      b[i].ph = atan2(a[i].i,a[i].r);
    }
}

static void polar2complex(fpolar a[], fcomplex b[],int N)
{
    int i;

    for (i=0;i<N;i++) {
      b[i].r = a[i].mag*cos(a[i].ph);
      b[i].i = a[i].mag*sin(a[i].ph);
    }
}

/* Sort poles in decreasing order of magnitudes */
static void sortRoots(fcomplex roots[], int dim)
{
    fpolar plr[MAXPOLES];

    /* Convert roots to polar form */
    complex2polar(roots, plr, dim);

    /* Sort by their magnitudes */
    qsort(plr, dim, sizeof(fpolar), (int(*)(const void *, const void * ))sortfun);

    /* Convert back to complex form */
    polar2complex(plr,roots,dim);

}

/* Comparison function for sorting in DECREASING order */
static int sortfun(fpolar *a, fpolar *b)
{
    if (a->mag<b->mag)
      return 1;
    else if (a->mag==b->mag)
      return 0;
    else
      return -1;
}

/* nudgeMags - Pole magnitude nudging routine
 *
 * Find the largest-magnitude pole off the real axis
 * and nudge all non-real poles by a factor of the distance
 * of the largest pole to the unit circle (or zero if fact is -ve).
 *
 * This has the effect of changing the time-response of the filter
 * without affecting the overall frequency response characteristic.
 *
 */
static void nudgeMags(fpolar a[], fcomplex b[], int dim, sampleT fact)
{
    sampleT eps = .000001; /* To avoid underflow comparisons */
    sampleT nudgefact;
    int i;

    /* Check range of nudge factor */
    if (fact>0 && fact<=1) {
      /* The largest magnitude pole will be at the beginning of
       * the array since it was previously sorted by the init routine.
       */
      for (i=0;i<dim;i++)
        if (fabs(b[i].i)>eps) /* Check if pole is complex */
          break;

      nudgefact = 1 + (1/a[i].mag-1)*fact;

      /* Nudge all complex-pole magnitudes by this factor */
      for (i=dim-1;i>=0;i--)
        if (fabs(b[i].i)>eps)
          a[i].mag *= nudgefact;
    }
    else if (fact < 0 && fact >=-1) {

      nudgefact = (fact + 1);

      /* Nudge all complex-pole magnitudes by this factor */
      for (i=dim-1;i>=0;i--)
        if (fabs(b[i].i)>eps)
          a[i].mag *= nudgefact;
    }
    else {
      /* Factor is out of range, do nothing */
    }
}

/* nudgePhases - Pole phase nudging routine
 *
 * Multiply phases of all poles by factor
 */
static void nudgePhases(fpolar a[], fcomplex b[], int dim, sampleT fact)
{
    sampleT eps = .000001; /* To avoid underflow comparisons */
    sampleT nudgefact;
    int i;
    sampleT phmax=0.0;

    /* Check range of nudge factor */
    if (fact>0 && fact<=1) {
      /* Find the largest angled non-real pole */
      for (i=0;i<dim;i++)
        if (a[i].ph>phmax)
          phmax = a[i].ph;

      phmax /= M_PI; /* Normalize to radian frequency */

      nudgefact = 1 + (1-phmax)*fact;

      /* Nudge all complex-pole magnitudes by this factor */
      for (i=dim-1;i>=0;i--)
        if (fabs(b[i].i)>eps)
          a[i].ph *= nudgefact;
    }
    else if (fact < 0 && fact >=-1) {
      nudgefact = (fact + 1);

      /* Nudge all complex-pole magnitudes by this factor */
      for (i=dim-1;i>=0;i--)
        if (fabs(b[i].i)>eps)
          a[i].ph *= nudgefact;
    }
    else {
      /* Factor is out of range, do nothing */
    }
}

/* ------------------------------------------------------------ */

/* Code from Press, Teukolsky, Vettering and Flannery
 * Numerical Recipes in C, 2nd Edition, Cambridge 1992.
 */

#define EPSS (1.0e-7)
#define MR (8)
#define MT (10)
#define MAXIT (MT*MR)

/* Simple definition is sufficient */
#define FPMAX(a,b) (a>b ? a : b)

static void laguer(fcomplex a[], int m, fcomplex *x, int *its)
{
    int iter,j;
    sampleT abx,abp,abm,err;
    fcomplex dx,x1,b,d,f,g,h,sq,gp,gm,g2;
    static const sampleT frac[MR+1] = {0.0,0.5,0.25,0.75,0.13,0.38,0.62,0.88,1.0};

    for (iter=1; iter<=MAXIT; iter++) {
      *its = iter;
      b = a[m];
      err = Cabs(b);
      d = f = Complex(0.0,0.0);
      abx = Cabs(*x);
      for (j=m-1; j>=0; j--) {
        f = Cadd(Cmul(*x,f),d);
        d = Cadd(Cmul(*x,d),b);
        b = Cadd(Cmul(*x,b),a[j]);
        err = Cabs(b)+abx*err;
      }
      err *= EPSS;
      if (Cabs(b) <= err) return;
      g = Cdiv(d,b);
      g2 = Cmul(g,g);
      h = Csub(g2,RCmul(2.0,Cdiv(f,b)));
      sq = Csqrt(RCmul( (m-1),Csub(RCmul( m,h),g2)));
      gp = Cadd(g,sq);
      gm = Csub(g,sq);
      abp = Cabs(gp);
      abm = Cabs(gm);
      if (abp < abm) gp = gm;
      dx = ((FPMAX(abp,abm) > 0.0 ? Cdiv(Complex( m,0.0),gp)
           : RCmul(exp(log(1.0+abx)),
                   Complex(cos(iter),
                           sin(iter)))));
      x1 = Csub(*x,dx);
      if (x->r == x1.r && x->i == x1.i) return;
      if (iter % MT) *x = x1;
      else *x = Csub(*x,RCmul(frac[iter/MT],dx));
    }
    fprintf(stderr,"too many iterations in laguer");
    return;
}
#undef EPSS
#undef MR
#undef MT
#undef MAXIT
/* (C) Copr. 1986-92 Numerical Recipes Software *%&&"U^3. */

/* ------------------------------------------------------------ */

/* Code from Press, Teukolsky, Vettering and Flannery
 * Numerical Recipes in C, 2nd Edition, Cambridge 1992.
 */

#define EPS (2.0e-6)
#define MAXM (100)

static void zroots(fcomplex a[], int m, fcomplex roots[])
{
    int i,its,j,jj;
    fcomplex x,b,c,ad[MAXM];

    for (j=0; j<=m; j++) ad[j] = a[j];
    for (j=m; j>=1; j--) {
      x = Complex(0.0,0.0);
      laguer(ad,j,&x,&its);
      if (fabs(x.i) <= 2.0*EPS*fabs(x.r)) x.i = 0.0;
      roots[j] = x;
      b = ad[j];
      for (jj=j-1; jj>=0; jj--) {
        c = ad[jj];
        ad[jj] = b;
        b = Cadd(Cmul(x,b),c);
      }
    }
    /*    if (poleish) */
    for (j=1; j<=m; j++)
      laguer(a,m,&roots[j],&its);
    for (j=2; j<=m; j++) {
      x = roots[j];
      for (i=j-1; i>=1; i--) {
        if (roots[i].r <= x.r) break;
        roots[i+1] = roots[i];
      }
      roots[i+1] = x;
    }
}
#undef EPS
#undef MAXM
/* (C) Copr. 1986-92 Numerical Recipes Software *%&&"U^3. */

/* Code from Press, Teukolsky, Vettering and Flannery
 * Numerical Recipes in C, 2nd Edition, Cambridge 1992.
 */

static fcomplex Cadd(fcomplex a, fcomplex b)
{
    fcomplex c;
    c.r = a.r+b.r;
    c.i = a.i+b.i;
    return c;
}

static fcomplex Csub(fcomplex a, fcomplex b)
{
    fcomplex c;
    c.r = a.r-b.r;
    c.i = a.i-b.i;
    return c;
}

static fcomplex Cmul(fcomplex a, fcomplex b)
{
    fcomplex c;
    c.r = a.r*b.r-a.i*b.i;
    c.i = a.i*b.r+a.r*b.i;
    return c;
}

static fcomplex Complex(sampleT re, sampleT im)
{
    fcomplex c;
    c.r = re;
    c.i = im;
    return c;
}

/* fcomplex Conjg(fcomplex z) */
/* { */
/*     fcomplex c; */
/*     c.r = z.r; */
/*     c.i = -z.i; */
/*     return c; */
/* } */

static fcomplex Cdiv(fcomplex a, fcomplex b)
{
    fcomplex c;
    sampleT r,den;
    if (fabs(b.r) >= fabs(b.i)) {
      r   = b.i/b.r;
      den = b.r+r*b.i;
      c.r = (a.r+r*a.i)/den;
      c.i = (a.i-r*a.r)/den;
    }
    else {
      r   = b.r/b.i;
      den = b.i+r*b.r;
      c.r = (a.r*r+a.i)/den;
      c.i = (a.i*r-a.r)/den;
    }
    return c;
}

static sampleT Cabs(fcomplex z)
{
    sampleT x,y,ans;
    sampleT temp;
    x = fabs(z.r);
    y = fabs(z.i);
    if (x == 0.0)
      ans  = y;
    else if (y == 0.0)
      ans  = x;
    else if (x > y) {
      temp = (y/x);
      ans  = x*sqrt(1.0+temp*temp);
    }
    else {
      temp = (x/y);
      ans  = y*sqrt(1.0+temp*temp);
    }
    return ans;
}

static fcomplex Csqrt(fcomplex z)
{
    fcomplex c;
    sampleT w;
    sampleT x,y,r;
    if ((z.r == 0.0) && (z.i == 0.0)) {
      c.r = 0.0;
      c.i = 0.0;
      return c;
    }
    else {
      x = fabs(z.r);
      y = fabs(z.i);
      if (x >= y) {
        r   = y/x;
        w   = sqrt(x)*sqrt(0.5*(1.0+sqrt(1.0+r*r)));
      }
      else {
        r   = x/y;
        w   = sqrt(y)*sqrt(0.5*(r+sqrt(1.0+r*r)));
      }
      if (z.r >= 0.0) {
        c.r = w;
        c.i = z.i/(2.0*w);
      } else {
        c.i = (z.i >= 0.0) ? w : -w;
        c.r = z.i/(2.0*c.i);
      }
      return c;
    }
}

static fcomplex RCmul(sampleT x, fcomplex a)
{
    fcomplex c;
    c.r = x*a.r;
    c.i = x*a.i;
    return c;
}

#ifdef __FILTERTEST__

int main(int argc, char* argv[]){

  FILTER *f = (FILTER*) calloc(1, sizeof(FILTER));
  int na=1,nb=1;
  sampleT in[CS_KSMPS];
  sampleT out[CS_KSMPS];

  if(f == NULL){
    fprintf(stderr, "Could not allocate filter.\n");
    exit(1);
  }
  fprintf(stderr, "Filter allocated: %d bytes\n", sizeof(FILTER));

  f->numa = na;
  f->numb = nb;
  f->in = in;
  f->out= out;
  f->coeffs[0] = 0.5; // b[0], nb=1
  f->coeffs[1] = 0.5; // a[1], na=1 (a[0]=1)

  ifilter(f); // initialize

  // 4 buffers of samples
  int i,j,k;
  for (i=0; i<4; i++){    
    if(i==0){
      f->in[0]=1.0; // Measure impulse response
    }
    else{
      f->in[0]=0.0;
    }
    afilter(f, CS_KSMPS);
    for(j=0; j<CS_KSMPS; j++){
      fprintf(stdout, "%5.4f ", f->out[j]);
    }
    fprintf(stdout, "\n");
  }

  free_filter(f);
  exit(0);


}

#endif
