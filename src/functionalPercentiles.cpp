/*F******************************************************************************
 *
 * openSMILE - open Speech and Music Interpretation by Large-space Extraction
 *       the open-source Munich Audio Feature Extraction Toolkit
 * Copyright (C) 2008-2009  Florian Eyben, Martin Woellmer, Bjoern Schuller
 *
 *
 * Institute for Human-Machine Communication
 * Technische Universitaet Muenchen (TUM)
 * D-80333 Munich, Germany
 *
 *
 * If you use openSMILE or any code from openSMILE in your research work,
 * you are kindly asked to acknowledge the use of openSMILE in your publications.
 * See the file CITING.txt for details.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 ******************************************************************************E*/


/*  openSMILE component:

functionals: percentiles and quartiles, and inter-percentile/quartile ranges

*/


#include <math.h>
#include <functionalPercentiles.hpp>


#define MODULE "cFunctionalPercentiles"


#define FUNCT_QUART1        0
#define FUNCT_QUART2        1
#define FUNCT_QUART3        2
#define FUNCT_IQR12         3
#define FUNCT_IQR23         4
#define FUNCT_IQR13         5
#define FUNCT_PERCENTILE    6
#define FUNCT_PCTLRANGE     7

#define N_FUNCTS  8

#define NAMES     "quartile1","quartile2","quartile3","iqr1-2","iqr2-3","iqr1-3","percentile","pctlrange"
#define IDX_VAR_FUNCTS 6  // start of percentile, pctlrange, etc. arrays of variable length

const char *percentilesNames[] = {NAMES};  // change variable name to your clas...

SMILECOMPONENT_STATICS(cFunctionalPercentiles)

SMILECOMPONENT_REGCOMP(cFunctionalPercentiles)
{
  SMILECOMPONENT_REGCOMP_INIT
  scname = COMPONENT_NAME_CFUNCTIONALPERCENTILES;
  sdescription = COMPONENT_DESCRIPTION_CFUNCTIONALPERCENTILES;

  // configure your component's configType:
  SMILECOMPONENT_CREATE_CONFIGTYPE
  SMILECOMPONENT_IFNOTREGAGAIN(
    ct->setField("quartiles","1/0=enable/disable computation of all quartiles (overrides individual settings)",1);
    ct->setField("quartile1","1/0=enable/disable computation of quartile1",0);
    ct->setField("quartile2","1/0=enable/disable computation of quartile2",0);
    ct->setField("quartile3","1/0=enable/disable computation of quartile3",0);
    ct->setField("iqr","1/0=enable/disable computation of all inter-quartile ranges (overrides individual settings)",1);
    ct->setField("iqr12","1/0=enable/disable computation of inter-quartile range 1-2",0);
    ct->setField("iqr23","1/0=enable/disable computation of inter-quartile range 2-3",0);
    ct->setField("iqr13","1/0=enable/disable computation of inter-quartile range 1-3",0);

    ct->setField("percentile","add computation of X (0..1) percent percentile : percentile[n] = X (n=0..N)",0.9,ARRAY_TYPE);
    ct->setField("pctlrange","add computation of inter percentile range 'n1-n2'","0-1",ARRAY_TYPE);
//    ct->setField("quickAlgo","do not sort input, use Dejan's quick estimation method instead",0);
    ct->setField("interp","linearly interpolate percentile values instead of rounding to nearest index in sorted array",1);

  )
  
  SMILECOMPONENT_MAKEINFO_NODMEM(cFunctionalPercentiles);
}

SMILECOMPONENT_CREATE(cFunctionalPercentiles)

//-----

cFunctionalPercentiles::cFunctionalPercentiles(const char *_name) :
  cFunctionalComponent(_name,N_FUNCTS,percentilesNames),
  pctl(NULL),
  pctlr1(NULL), pctlr2(NULL),
  tmpstr(NULL),
  quickAlgo(0),
  interp(0)
{
}

void cFunctionalPercentiles::fetchConfig()
{
//  quickAlgo = getInt("quickAlgo");
  interp = getInt("interp");

  enab[FUNCT_QUART1] = enab[FUNCT_QUART2] = enab[FUNCT_QUART3] = 1;
  if (getInt("quartile1")) enab[FUNCT_QUART1] = 1;
  if (getInt("quartile2")) enab[FUNCT_QUART2] = 1;
  if (getInt("quartile3")) enab[FUNCT_QUART3] = 1;
  if (isSet("quartiles")) {
    enab[FUNCT_QUART1] = enab[FUNCT_QUART2] = enab[FUNCT_QUART3] = getInt("quartiles");
  }
  
  enab[FUNCT_IQR12] = enab[FUNCT_IQR23] = enab[FUNCT_IQR13] = 1;
  if (getInt("iqr12")) enab[FUNCT_IQR12] = 1;
  if (getInt("iqr23")) enab[FUNCT_IQR23] = 1;
  if (getInt("iqr13")) enab[FUNCT_IQR13] = 1;
  if (isSet("iqr")) {
    enab[FUNCT_IQR12] = enab[FUNCT_IQR23] = enab[FUNCT_IQR13] = getInt("iqr");
  }

  int i;
  nPctl = getArraySize("percentile");
  nPctlRange = getArraySize("pctlrange");
  if (nPctl > 0) {
    enab[FUNCT_PERCENTILE] = 1;
    pctl = (double*)calloc(1,sizeof(double)*nPctl);
    // load percentile info
    for (i=0; i<nPctl; i++) {
      pctl[i] = getDouble_f(myvprint("percentile[%i]",i));
      if (pctl[i] < 0.0) {
        SMILE_WRN(2,"(inst '%s') percentile[%i] is out of range [0..1] : %f (clipping to 0.0)",getInstName(),i,pctl[i]);
        pctl[i] = 0.0;
      }
      if (pctl[i] > 1.0) {
        SMILE_WRN(2,"(inst '%s') percentile[%i] is out of range [0..1] : %f (clipping to 1.0)",getInstName(),i,pctl[i]);
        pctl[i] = 1.0;
      }
    }
    if (nPctlRange > 0) {
      enab[FUNCT_PCTLRANGE] = 1;
      pctlr1 = (int*)calloc(1,sizeof(int)*nPctlRange);
      pctlr2 = (int*)calloc(1,sizeof(int)*nPctlRange);
      for (i=0; i<nPctlRange; i++) {
        char *tmp = strdup(getStr_f(myvprint("pctlrange[%i]",i)));
        char *orig = strdup(tmp);
        int l = (int)strlen(tmp);
        int err=0;
        // remove spaces and tabs at beginning and end
//        while ( (l>0)&&((*tmp==' ')||(*tmp=='\t')) ) { *(tmp++)=0; l--; }
//        while ( (l>0)&&((tmp[l-1]==' ')||(tmp[l-1]=='\t')) ) { tmp[l-1]=0; l--; }
        // find '-'
        char *s2 = strchr(tmp,'-');
        if (s2 != NULL) {
          *(s2++) = 0;
          char *ep=NULL;
          int r1 = strtol(tmp,&ep,10);
          if ((r1==0)&&(ep==tmp)) { err=1; }
          else if ((r1 < 0)||(r1>=nPctl)) {
                 SMILE_ERR(1,"(inst '%s') percentile range [%i] = '%s' (X-Y):: X (=%i) is out of range (allowed: [0..%i])",getInstName(),i,orig,r1,nPctl);
               }
          ep=NULL;
          int r2 = strtol(s2,&ep,10);
          if ((r2==0)&&(ep==tmp)) { err=1; }
          else {
               if ((r2 < 0)||(r2>=nPctl)) {
                 SMILE_ERR(1,"(inst '%s') percentile range [%i] = '%s' (X-Y):: Y (=%i) is out of range (allowed: [0..%i])",getInstName(),i,orig,r2,nPctl);
               } else {
                 if ((r2 == r1)) {
                   SMILE_ERR(1,"(inst '%s') percentile range [%i] = '%s' (X-Y):: X must be != Y !!",getInstName(),i,orig);
                 }
               }
             }
          if (!err) {
            pctlr1[i] = r1;
            pctlr2[i] = r2;
          }
        } else { err=1; }

        if (err==1) {
          SMILE_ERR(1,"(inst '%s') Error parsing percentile range [%i] = '%s'! (Range must be X-Y, where X and Y are positive integer numbers!)",getInstName(),i,orig);
          pctlr1[i] = -1;
          pctlr2[i] = -1;
        }
        free(orig);
        free(tmp);
      }
    }
  }

  cFunctionalComponent::fetchConfig();
  if (enab[FUNCT_PERCENTILE]) nEnab += nPctl-1;
  if (enab[FUNCT_PCTLRANGE]) nEnab += nPctlRange-1;
}

const char* cFunctionalPercentiles::getValueName(long i)
{
  if (i<IDX_VAR_FUNCTS) {
    return cFunctionalComponent::getValueName(i);
  }
  if (i>=IDX_VAR_FUNCTS) {
    int j=IDX_VAR_FUNCTS;
    int pr=0;
    // check, if percentile or pctlrange is referenced:
    i -= IDX_VAR_FUNCTS;
    if (i>=nPctl) { j++; i-= nPctl; pr = 1; }
    const char *n = cFunctionalComponent::getValueName(j);
    // append [i]
    if (tmpstr != NULL) free(tmpstr);
    if (!pr) {
      tmpstr = myvprint("%s%.1f",n,pctl[i]*100.0);
    } else {
      tmpstr = myvprint("%s%i-%i",n,pctlr1[i],pctlr2[i]);
    }
    return tmpstr;
  }
  return "UNDEF";
}

// convert percentile to absolute index
long cFunctionalPercentiles::getPctlIdx(double p, long N)
{
  long ret = (long)round(p*(double)(N-1));
  if (ret<0) return 0;
  if (ret>=N) return N-1;
  return ret;
}

// get linearly interpolated percentile
FLOAT_DMEM cFunctionalPercentiles::getInterpPctl(double p, FLOAT_DMEM *sorted, long N)
{
  double idx = p*(double)(N-1);
  long i1,i2;
  i1=(long)floor(idx);
  i2=(long)ceil(idx);
  if (i1<0) i1=0;
  if (i2<0) i2=0;
  if (i1>=N) i1=N-1;
  if (i2>=N) i2=N-1;
  if (i1!=i2) {
    double w1,w2;
    w1 = idx-(double)i1;
    w2 = (double)i2 - idx;
    return sorted[i1]*(FLOAT_DMEM)w2 + sorted[i2]*(FLOAT_DMEM)w1;
  } else {
    return sorted[i1];
  }
}

long cFunctionalPercentiles::process(FLOAT_DMEM *in, FLOAT_DMEM *inSorted, FLOAT_DMEM *out, long Nin, long Nout)
{
  long i;
  if ((Nin>0)&&(out!=NULL)) {
    int n=0;
    FLOAT_DMEM q1, q2, q3;

    if (quickAlgo) {
      // Not yet implemented....
      /*
      // use *in array...

      //find min/max:
      FLOAT_DMEM *in0 = in;
      FLOAT_DMEM *inE = in+Nin;
      FLOAT_DMEM max = *in;
      FLOAT_DMEM min = *in;
      while (in<inE) {
        if (*in < min) min = *in;
        if (*in > max) max = *(in++);
      }
      
      //create range buffers:
      long oversample = 10;
      long nBins = Nin * oversample;
      FLOAT_DMEM unit = (max-min)/(FLOAT_DMEM)nBins;
      long *bins = (long*)calloc(1,sizeof(long)*nBins);

      in=in0;
      FLOAT_DMEM cunit = min;
      for (i=0; i<Nin; i++) {
//NO!! compute bin index from value!        if ((*in > cuint)&&(*in < cuint+unit)) { bins[i]++; }
        FLOAT_DMEM idx = (*in-min)/unit;
        bins
        in++; cunit += unit;
      }
      
      // we need all required percentiles (sorted! wiht corresp. indicies) in an array.



      free(bins);
      */
    } else {
      long minpos=0, maxpos=0;
      if (inSorted == NULL) {
        SMILE_ERR(1,"(inst '%s') expected sorted input, however got NULL!",getInstName());
      }
      // quartiles:
      if (interp) {
        q1 = getInterpPctl(0.25,inSorted,Nin);
        q2 = getInterpPctl(0.50,inSorted,Nin);
        q3 = getInterpPctl(0.75,inSorted,Nin);
      } else {
        q1 = inSorted[getPctlIdx(0.25,Nin)];
        q2 = inSorted[getPctlIdx(0.50,Nin)];
        q3 = inSorted[getPctlIdx(0.75,Nin)];
      }
      if (enab[FUNCT_QUART1]) out[n++]=q1;
      if (enab[FUNCT_QUART2]) out[n++]=q2;
      if (enab[FUNCT_QUART3]) out[n++]=q3;
      if (enab[FUNCT_IQR12]) out[n++]=q2-q1;
      if (enab[FUNCT_IQR23]) out[n++]=q3-q2;
      if (enab[FUNCT_IQR13]) out[n++]=q3-q1;

      // percentiles
      if ((enab[FUNCT_PERCENTILE])||(enab[FUNCT_PCTLRANGE])) {
        int n0 = n; // start of percentiles array (used later for computation of pctlranges)
        if (interp) {
          for (i=0; i<nPctl; i++) {
            out[n++] = getInterpPctl(pctl[i],inSorted,Nin);
          }
        } else {
          for (i=0; i<nPctl; i++) {
            out[n++] = inSorted[getPctlIdx(pctl[i],Nin)];
          }
        }
        if (enab[FUNCT_PCTLRANGE]) {
          for (i=0; i<nPctlRange; i++) {
            if ((pctlr1[i]>=0)&&(pctlr2[i]>=0)) {
              out[n++] = fabs(out[n0+pctlr2[i]] - out[n0+pctlr1[i]]);
            } else { out[n++] = 0.0; }
          }
        }
      }
    }

    return n;
  }
  return 0;
}

/*
long cFunctionalPercentiles::process(INT_DMEM *in, INT_DMEM *inSorted, INT_DMEM *out, long Nin, long Nout)
{

  return 0;
}
*/

cFunctionalPercentiles::~cFunctionalPercentiles()
{
  if(pctl!=NULL) free(pctl);
  if(pctlr1!=NULL) free(pctlr1);
  if(pctlr2!=NULL) free(pctlr2);
  if(tmpstr!=NULL) free(tmpstr);
}

