/*******************************************************************************
* Copyright 2017 NEC Laboratories America
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

/** \file
 * precalc inner kernel loop limits to avoid conditionals */
#include "conv/conv.hpp"
#include "idiv.hpp"

namespace conv {

// BWD + dilate is not fast for these loops (and mkl-dnn doesn't allow it yet)
extern void refconv_2_bwd_d(const prb_t *p, dnn_mem_t &diff_src_m,
        dnn_mem_t &wei_m, dnn_mem_t &diff_dst_m);

static void chk( bool cond, char const* msg, char const* file, int const lineno ){
    if (!cond){ printf("@@@ error: %s : [%s:%d]\n", msg, file, lineno); exit(1); }
}
//static bool trivial( int const verb, bool const cond, char const* msg,
//                     char const* file, int const lineno ){
//    if (verb > verbose && cond){
//        printf("@@@ trivial: %s : [%s:%d]\n", msg, file, lineno); fflush(stdout);
//    }
//
//    return cond;
//}
//#define MUST( COND )
#define MUST( COND )    chk(    (COND), #COND, __PRETTY_FUNCTION__, __LINE__)
#define PRINTF(...)     do{ printf(__VA_ARGS__); fflush(stdout);}while(0)
#define TRIVIAL( COND ) COND
//#define TRIVIAL( COND ) trivial(1, (COND), #COND, __PRETTY_FUNCTION__, __LINE__)

#if defined(NDEBUG)
#define DPRINTF(...)
#define DMUST(...)
#else
#define DPRINTF(...)  do{printf(__VA_ARGS__); fflush(stdout);}while(0)
#define DMUST(...)   MUST(__VA_ARGS__)
#endif

#define USE_REF_CONV2 0 /* debug */

//----------------------------

/** hoist `A+iB in range [C,D)` condition out of a loop for i in [imin,imax].
 * When 
 * Original:
 * \code
 * for(i=imin; i<imax; ++i){       // original loop
 *   int const ApiB = a + i*b;      // linear fn, ( b>=0 ? )
 *   if( ApiB < c || ApiB > d ) continue;
 *   // Loop Body
 * }
 * \endcode
 * Transformed:
 * \code
 * int const ibeg, iend;
 * hoist_ApiB_in( ibeg, iend, imin,imax, a,b, c,d );
 * for(i=ibeg; i<iend; ++i){       // original loop
 *   int const ApiB = a + i*b;
 *   // GONE: if( ApiB < c || ApiB > d ) continue;
 *   // Loop Body
 * }
 * \endcode
 * \pre \c b > 0, (c, d?)
 */
static inline void hoist_ApiB_in( int& beg, int& end,
        const int imin, const int imax,
        const int a, const int b, const int c, const int d)
{
    RT_ASSERT( b > 0 );
    // int i*B < A    iff    i < (A    )/B
    // int i*B > A    iff    i > (A+B-1)/A
    // A+iB >= c ... iB >= c-A  ... i >= (c-A + B-1 )/B
#if 1
    beg = div_floor( c-a+b-1, b );
#else
    beg = c-a + b-1;
    if( beg >= 0 ){
        beg /= b;
    } else {
        int const fmul=(-beg + b)/b;
        RT_ASSERT( beg + fmul*b >= 0 );
        beg = (beg + fmul*b) / b - fmul;
    }
#endif
    //print(0, "i in [%d,%d), lin(a,b)=%d+i*%d in [c,d]=[%d,%d), beg=%d? f+c-a+b-1=%d\n",
    //        imin,imax, a,b, c,d, beg, f+c-a+b-1);
    DMUST( a + (beg-1)*b < c );
    DMUST( a + (beg  )*b >= c );
    if( beg < imin ) beg = imin;

    // A+iB < d ... iB < d-A    ... i < (d-A) / B
#if 1
    end = div_floor( d-a+b-1, b );
#else
    end = d-a +b-1;
    if( end >= 0 ){
        end /= b;
    } else {
        int const fmul=(-end + b)/b;
        RT_ASSERT( end + fmul*b >= 0 );
        end = (end + fmul*b) / b - fmul;
    }
#endif
    //print(0, "i in [%d,%d), lin(a,b)=%d+i*%d in [c,d]=[%d,%d), end=%d? f+d-a=%d\n",
    //        imin,imax, a,b, c,d, end, f+d-a);
    DMUST( a + (end-1)*b < d );
    DMUST( a + (end  )*b >= d );
    if( end > imax ) end = imax;
}

/** Integer \c i so A-iB is <em>just below</em> \c target.
 * \pre \c B>0 (unchecked). */
static inline int AmiB_lt_target( const int a, const int b, const int target)
{
    int ibelow = a-target + b;
    // XXX use idiv.hpp here too
    if( ibelow >= 0 ){
        ibelow /= b;
    } else {
        int const fmul=(-ibelow + b)/b;
        RT_ASSERT( ibelow + fmul*b >= 0 );
        ibelow = (ibelow + fmul*b) / b - fmul;
    }
    DMUST( a - (ibelow-1)*b >= target );
    DMUST( a - (ibelow  )*b <  target );
    return ibelow;
}
/** Get range if \c i so A-iB is in [c,d), and then further
 * restrict to range [imin,imax).  Note that \c -B means as \c i
 * increases, we move from \c d-1 downwards to \c c. */
static inline void hoist_AmiB_in( int& beg, int& end,
        const int imin, const int imax,
        const int a, const int b, const int c, const int d)
{
    RT_ASSERT( b > 0 );
    beg = AmiB_lt_target( a, b, d );
    if( beg < imin ) beg = imin;

    end = AmiB_lt_target( a, b, c );
    if( end > imax ) end = imax;
}
void refconv_3_fwd(const prb_t *p, dnn_mem_t &src_m,
        dnn_mem_t &wei_m, dnn_mem_t &bia_m, dnn_mem_t &dst_m) {
    MUST( p->kh > 0 && p->kw > 0 );
    MUST( p->dh >= 0 && p->dw >= 0 );
    MUST( p->sh >= 0 && p->sw >= 0 );

#if USE_REF_CONV2
  auto ker = [](
                const prb_t *p, const dnn_mem_t &src_m, const dnn_mem_t &wei_m,
                float &d, int g, int mb, int oc, int oh, int ow) {
    for (int ic = 0; ic < p->ic/p->g; ++ic) {
      for (int kh = 0; kh < p->kh; ++kh) {
        const int ih = oh * p->sh - p->ph + kh * (p->dh + 1);
        if (ih < 0 || ih >= p->ih) continue;

        for (int kw = 0; kw < p->kw; ++kw) {
          const int iw = ow * p->sw - p->pw + kw * (p->dw + 1);
          if (iw < 0 || iw >= p->iw) continue;

          size_t src_off = src_off_f(p, mb, g, ic, ih, iw);
          size_t wei_off = wei_off_f(p, g, oc, ic, kh, kw);
          d += ((float*)src_m)[src_off] * ((float*)wei_m)[wei_off];
        }
      }
    }
  };

  OMP(parallel for collapse(5))//;
  for (int g = 0; g < p->g; ++g) {
    for (int mb = 0; mb < p->mb; ++mb) {
      for (int oc = 0; oc < p->oc/p->g; ++oc) {
        for (int oh = 0; oh < p->oh; ++oh) {
          for (int ow = 0; ow < p->ow; ++ow) {
            size_t dst_off = dst_off_f(p, mb, g, oc, oh, ow);
            size_t bia_off = bia_off_f(p, g, oc);
            float &d = ((float*)dst_m)[dst_off];
            d = (p->dir & FLAG_BIA) ? ((float*)bia_m)[bia_off] : 0;
            ker( p, src_m, wei_m,
                 d, g, mb, oc, oh, ow);
            if (p->merge == RELU && d < 0)
              d = 0;
          }
        }
      }
    }
  }
#else // !USE_REF_CONV2
    auto ker = [](
            const prb_t *p, dnn_mem_t &src_m, dnn_mem_t &wei_m,
            float &d, const int g, const int mb, const int oc, const int oh, const int ow
            , const int kh_beg, const int kh_end, const int kw_beg, const int kw_end
            ) {
        for (int ic = 0; ic < p->ic/p->g; ++ic) {
            for (int kh = kh_beg; kh < kh_end; ++kh)
            {
                const int ih = oh * p->sh - p->ph + kh * (p->dh + 1);
                for (int kw = kw_beg; kw < kw_end; ++kw) {
                    const int iw = ow * p->sw - p->pw + kw * (p->dw + 1); // loop vars: ow, kw
                    size_t src_off = src_off_f(p, mb, g, ic, ih, iw);
                    size_t wei_off = wei_off_f(p, g, oc, ic, kh, kw);
                    d += ((float*)src_m)[src_off] * ((float*)wei_m)[wei_off];
                }
            }
        }
    };

    // writes go to  dst_off_f(p, mb, g, oc, oh, ow);
    OMP(parallel for collapse(5))//;
    for (int g = 0; g < p->g; ++g) {
    for (int mb = 0; mb < p->mb; ++mb) {
        for (int oc = 0; oc < p->oc/p->g; ++oc) {
        for (int oh = 0; oh < p->oh; ++oh) {
        for (int ow = 0; ow < p->ow; ++ow) {
            size_t dst_off = dst_off_f(p, mb, g, oc, oh, ow);
            size_t bia_off = bia_off_f(p, g, oc);
            float &d = ((float*)dst_m)[dst_off];
            d = (p->dir & FLAG_BIA)? ((float*)bia_m)[bia_off] : 0;
            int kh_beg, kh_end;
            hoist_ApiB_in( kh_beg, kh_end,
                    /*i  in   */ 0, p->kh,
                    /*ih=A+iB */ (oh * p->sh - p->ph), (p->dh + 1),
                    /*ih in   */ 0, p->ih);
            if (kh_beg < kh_end){
            int kw_beg, kw_end;
            hoist_ApiB_in( kw_beg, kw_end,
                    /*i  in   */ 0, p->kw,
                    /*iw=A+iB */ (ow * p->sw - p->pw), (p->dw + 1),
                    /*iw in   */ 0, p->iw);
            if (kw_beg < kw_end){
                DPRINTF(".");
                ker( p, src_m, wei_m,
                        d, g, mb, oc, oh, ow
                        , kh_beg, kh_end, kw_beg, kw_end
                   );
            }
            }
            if (p->merge == RELU && d < 0)
                d = 0;
        }
        }
        }
    }
    }
#endif // USE_REF_CONV2
}

void refconv_3_bwd_d(const prb_t *p, dnn_mem_t &diff_src_m,
    dnn_mem_t &wei_m, dnn_mem_t &diff_dst_m) {
  // XXX TODO inline the kernel and play with loop ordering

  if (p->dh != 0) { // A fast version here does not support dilation
    // This is no big deal, since mkl-dnn does not even allow you to
    // create the descriptors for it.
    refconv_2_bwd_d(p, diff_src_m, wei_m, diff_dst_m);
  }

#if USE_REF_CONV2
  auto ker = [](
                const prb_t *p, const dnn_mem_t &diff_dst_m, const dnn_mem_t &wei_m,
                float &ds, int g, int mb, int ic, int ih, int iw) {
    for (int oc = 0; oc < p->oc/p->g; ++oc) {
      for (int kh = 0; kh < p->kh; ++kh) {
        int oh = ih - kh * (p->dh + 1) + p->ph;
        if (oh < 0 || oh % p->sh) continue;
        oh /= p->sh;
        if (oh >= p->oh) continue;

        for (int kw = 0; kw < p->kw; ++kw) {
          int ow = iw - kw * (p->dw + 1) + p->pw;
          if (ow < 0 || ow % p->sw) continue;
          ow /= p->sw;
          if (ow >= p->ow) continue;

          size_t dst_off = dst_off_f(p, mb, g, oc, oh, ow);
          size_t wei_off = wei_off_f(p, g, oc, ic, kh, kw);
          ds += ((float*)diff_dst_m)[dst_off]
            * ((float*)wei_m)[wei_off];
        }
      }
    }
  };

  OMP(parallel for collapse(5))//;
  for (int g = 0; g < p->g; ++g) {
    for (int mb = 0; mb < p->mb; ++mb) {
      for (int ic = 0; ic < p->ic/p->g; ++ic) {
        for (int ih = 0; ih < p->ih; ++ih) {
          for (int iw = 0; iw < p->iw; ++iw) {
            size_t src_off = src_off_f(p, mb, g, ic, ih, iw);
            float &ds = ((float*)diff_src_m)[src_off];
            ds = 0;
            ker( p, diff_dst_m, wei_m,
                 ds, g, mb, ic, ih, iw);
          }
        }
      }
    }
  }
#else // !USE_REF_CONV2
  auto ker = [](
      const prb_t *p, const dnn_mem_t &diff_dst_m, const dnn_mem_t &wei_m,
      float &ds, int g, int mb, int ic, int ih, int iw
      , const int kh_beg, const int kh_end
      , const int kw_beg, const int kw_end
      ) {
    RT_ASSERT(p->sh > 0);
    //bool const s0 = kh_beg >= kh_end || p->sh <= 1; // this IMPLIES skips remains 0
    for (int oc = 0; oc < p->oc/p->g; ++oc) {
      // next: loop over allowed oh values, THEN over kh
      //int const khh = p->sh; // only for p->dh==0 !!!
      for (int kh = kh_beg; kh < kh_end; kh+=p->sh) {
        int oh = ih+p->ph - kh /* * (p->dh + 1) */; // loop vars: kh, ih
        oh /= p->sh;

        int const kww = p->sw; // perhaps lcd of p-sh and p->dh+1 ? XXX
        for (int kw = kw_beg; kw < kw_end; kw += kww) {
          int ow = iw - kw * (p->dw + 1) + p->pw; // loop vars: iw, kw
          //if (ow < 0 || ow % p->sw) continue;
          ow /= p->sw;
          //if (ow >= p->ow) continue;

          size_t dst_off = dst_off_f(p, mb, g, oc, oh, ow);
          size_t wei_off = wei_off_f(p, g, oc, ic, kh, kw);
          ds += ((float*)diff_dst_m)[dst_off]
            * ((float*)wei_m)[wei_off];
        }
      }
    }
  };

  OMP(parallel for collapse(4))//;
  for (int g = 0; g < p->g; ++g) {
    for (int mb = 0; mb < p->mb; ++mb) {
      for (int ic = 0; ic < p->ic/p->g; ++ic) {
        for (int ih = 0; ih < p->ih; ++ih) {
          int kh_beg, kh_end;
          hoist_AmiB_in( kh_beg, kh_end,
              /*i  in   */ 0, p->kh,
              /*oh=A+iB */ (ih + p->ph), (p->dh+1),
              /*oh in   */ 0, p->oh*p->sh );
          { // jump kh_beg up to 1st non-skipped index
            int oh_beg = ih+p->ph - kh_beg * (p->dh+1);
            int rem_beg = oh_beg % p->sh;
            if (rem_beg){
              // XXX closed-form formula ???  (should be easy now that know p->dh==0)
              // ref_conv4 has a strictly correct similar corrective example
              do {
                ++kh_beg;
                oh_beg = ih+p->ph - kh_beg * (p->dh+1);
              } while( oh_beg % p->sh != 0 && kh_beg < kh_end );
              //print(0, " ... kh_beg --> %d, oh_beg --> %d\n", kh_beg, oh_beg);
              DMUST( kh_beg >= kh_end || (ih+p->ph - kh_beg * (p->dh+1)) % p->sh == 0 );
            }
          }
          for (int iw = 0; iw < p->iw; ++iw) {
            int kw_beg, kw_end;
            hoist_AmiB_in( kw_beg, kw_end,
                /*i  in   */ 0, p->kw,
                /*ow=A+iB */ (iw + p->pw), (p->dw+1),
                /*ow in   */ 0, p->ow*p->sw );
            { // jump kw_beg up to 1st non-skipped index
              //int iwpw = iw + p->pw;
              // XXX still have the modulus in the loop...
              int ow_beg = iw+p->pw - kw_beg * (p->dw+1);
              int rem_beg = ow_beg % p->sw;
              if (rem_beg){
                // XXX closed-form formula ???  (should be easy now that know p->dw==0)
                // XXX see ref_conv3.cpp skips counter for hints about how.
                do {
                  ++kw_beg;
                  ow_beg = iw+p->pw - kw_beg * (p->dw+1);
                } while( ow_beg % p->sw != 0 && kw_beg < kw_end );
                //print(0, " ... kw_beg --> %d, ow_beg --> %d\n", kw_beg, ow_beg);
                DMUST( kw_beg >= kw_end || (iw+p->pw - kw_beg * (p->dw+1)) % p->sw == 0 );
              }
            }
            size_t src_off = src_off_f(p, mb, g, ic, ih, iw);
            float &ds = ((float*)diff_src_m)[src_off];
            ds = 0;
            //if( TRIVIAL(kh_beg >= kh_end || kw_beg >= kw_end) ){
            //  DPRINTF("t");
            //} else {
              DPRINTF(".");
              ker( p, diff_dst_m, wei_m,
                  ds, g, mb, ic, ih, iw
                  , kh_beg, kh_end, kw_beg, kw_end
                 );
            //}
          }
        }
      }
    }
  }
#endif // USE_REF_CONV2
}

/** This one just reuses the hoisting function from FWD.
 * g,oc,ic,kh,kw,mb,oh,ow,[iw],[ih] */
void refconv_3_bwd_w(const prb_t *p, dnn_mem_t &src_m,
    dnn_mem_t &diff_wei_m, dnn_mem_t &diff_bia_m, dnn_mem_t &diff_dst_m) {
#define NEW 0
  auto ker = [](
      const prb_t *p, const dnn_mem_t &diff_dst_m, const dnn_mem_t &src_m,
      float &dw, int g, int oc, int ic, int kh, int kw
#if NEW
      , const int oh_beg, const int oh_end, const int ow_beg, const int ow_end
#endif
      ) {
    for (int mb = 0; mb < p->mb; ++mb) {
#if !NEW
      for (int oh = 0; oh < p->oh; ++oh) {
        for (int ow = 0; ow < p->ow; ++ow) {
          const int ih = oh * p->sh - p->ph + kh * (p->dh + 1);
          const int iw = ow * p->sw - p->pw + kw * (p->dw + 1);
          if (ih < 0 || ih >= p->ih) continue;
          if (iw < 0 || iw >= p->iw) continue;
#else
          for (int oh = oh_beg; oh < oh_end; ++oh) {
            for (int ow = ow_beg; ow < ow_end; ++ow) {
              const int ih = oh * p->sh - p->ph + kh * (p->dh + 1);
              const int iw = ow * p->sw - p->pw + kw * (p->dw + 1);
#endif
              size_t src_off = src_off_f(p, mb, g, ic, ih, iw);
              size_t dst_off = dst_off_f(p, mb, g, oc, oh, ow);
              dw += ((float*)diff_dst_m)[dst_off]
                * ((float*)src_m)[src_off];
            }
          }
        }
      };

      OMP(parallel for collapse(5))//;
      for (int g = 0; g < p->g; ++g) {
        for (int oc = 0; oc < p->oc/p->g; ++oc) {
          for (int ic = 0; ic < p->ic/p->g; ++ic) {
            for (int kh = 0; kh < p->kh; ++kh) {
              for (int kw = 0; kw < p->kw; ++kw) {
#if NEW
                int oh_beg, oh_end;
                hoist_ApiB_in( oh_beg, oh_end,
                    /*i  in   */ 0, p->oh,
                    /*ih=A+iB */ (kh * (p->dh+1) - p->ph), p->sh,
                    /*ih in   */ 0, p->ih);
                int ow_beg, ow_end;
                hoist_ApiB_in( ow_beg, ow_end,
                    /*i  in   */ 0, p->ow,
                    /*iw=A+iB */ (kw * (p->dw+1) - p->pw), p->sw,
                    /*iw in   */ 0, p->iw);
#endif
                size_t wei_off = wei_off_f(p, g, oc, ic, kh, kw);
                float &dw = ((float*)diff_wei_m)[wei_off];
                dw = 0;
                ker( p, diff_dst_m, src_m,
                    dw, g, oc, ic, kh, kw
#if NEW
                    , oh_beg, oh_end, ow_beg, ow_end
#endif
                   );
              }
            }
          }
        }
      }

      if (!(p->dir & FLAG_BIA)) return;

      OMP(parallel for collapse(2))//;
      for (int g = 0; g < p->g; ++g) {
        for (int oc = 0; oc < p->oc/p->g; ++oc) {
          size_t bia_off = bia_off_f(p, g, oc);
          float &db = ((float*)diff_bia_m)[bia_off];
          db = 0;

          for (int mb = 0; mb < p->mb; ++mb) {
            for (int oh = 0; oh < p->oh; ++oh) {
              for (int ow = 0; ow < p->ow; ++ow) {
                size_t dst_off = dst_off_f(p, mb, g, oc, oh, ow);
                db += ((float*)diff_dst_m)[dst_off];
              }
            }
          }
        }
      }
    }

}
// vim: et ts=2 sw=2 cindent nopaste ai cino=^=l0,\:0,N-s foldmethod=indent foldlevel=3
