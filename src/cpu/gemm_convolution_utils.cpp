/*******************************************************************************
* Copyright 2016-2017 Intel Corporation
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

#include "mkldnn_types.h"

#include "c_types_map.hpp"
#include "utils.hpp"
#include "type_helpers.hpp"
#include "gemm_convolution_utils.hpp"

namespace mkldnn {
namespace impl {
namespace cpu {

using namespace mkldnn::impl::status;
using namespace mkldnn::impl::memory_format;
using namespace mkldnn::impl::utils;

namespace jit_gemm_convolution_utils {

void im2col(
    jit_gemm_conv_conf_t &jcp, const float *im, float *col) {
    const size_t im_step = jcp.ih * jcp.iw;
    const size_t col_step = jcp.ks * jcp.os;

    auto im2col_1st = [&](const float *im, float *col) {
        const size_t work_amount = jcp.oh * jcp.kh;
        OMP(omp parallel)//;
        {
            const int ithr = omp_get_thread_num();
            const int nthr = omp_get_num_threads();

            size_t start = 0, end = 0;
            int oh = 0, kh = 0;
            balance211(work_amount, nthr, ithr, start, end);
            nd_iterator_init(start, kh, jcp.kh, oh, jcp.oh);

            for (size_t iwork = start; iwork < end; ++iwork)
            {
                const int ih = oh * jcp.stride_h - jcp.t_pad + kh * (1 + jcp.dilate_h);
                if (ih < 0 || ih >= jcp.ih) {
                    nd_iterator_step(kh, jcp.kh, oh, jcp.oh);
                    continue;
                }

                for (int kw = 0; kw < jcp.kw; ++kw) {
                for (int ow = 0; ow < jcp.ow; ++ow) {
                    const int iw = ow * jcp.stride_w - jcp.l_pad + kw * (1 + jcp.dilate_w);
                    if (iw < 0 || iw >= jcp.iw) continue;

                    const size_t col_idx = ((kh*jcp.kw + kw)*jcp.oh+oh)*jcp.ow+ow;
                    const size_t im_idx = ih*jcp.iw + iw;
                    col[col_idx] = im[im_idx];
                }}
                nd_iterator_step(kh, jcp.kh, oh, jcp.oh);
            }
        }
    };

#define UNROLL_IM2COL 8
    auto im2col_common = [&](const float *im, float *col) {
        const size_t work_amount = floor(jcp.ic/UNROLL_IM2COL);
        size_t ic;
        const float *im_;
        float *col_;
        OMP(omp parallel)//;
        {
            const int ithr = omp_get_thread_num();
            const int nthr = omp_get_num_threads();

            size_t start = 0, end = 0, ichunk = 0;
            balance211(work_amount, nthr, ithr, start, end);
            nd_iterator_init(start, ichunk, work_amount);

            ic = ichunk * UNROLL_IM2COL;
            im_ = im + ic * im_step;
            col_ = col + ic * col_step;

            for (size_t iwork = start; iwork < end; ++iwork)
            {
                /* Where to put #pragma ivdep ? */
                for (int kh = 0; kh < jcp.kh; ++kh) {
                for (int oh = 0; oh < jcp.oh; ++oh) {
                    const int ih = oh * jcp.stride_h
                                   - jcp.t_pad + kh * (1 + jcp.dilate_h);
                    if (ih < 0 || ih >= jcp.ih) continue;

                    for (int kw = 0; kw < jcp.kw; ++kw) {
IVDEP()
                    for (int ow = 0; ow < jcp.ow; ++ow) {
                        const int iw = ow * jcp.stride_w
                                       - jcp.l_pad + kw * (1 + jcp.dilate_w);
                        if (iw < 0 || iw >= jcp.iw) continue;
UNROLL(UNROLL_IM2COL)
                        for(int i = 0; i < UNROLL_IM2COL; ++i) {
                            const size_t col_idx =  i*col_step +
                                                   ((kh * jcp.kw + kw) * jcp.oh+oh)
                                                   * jcp.ow + ow;
                            const size_t im_idx = i*im_step + ih*jcp.iw + iw;
                            col_[col_idx] = im_[im_idx];
                        }
                    }}
                }}
                im_ += im_step * UNROLL_IM2COL;
                col_ += col_step * UNROLL_IM2COL;

                nd_iterator_step(ichunk, work_amount);
            }
        }

        ic = UNROLL_IM2COL * work_amount;
        if(ic < jcp.ic) {
            im_ = im + ic * im_step;
            col_ = col + ic * col_step;

            switch(jcp.ic - ic) {
                case 1:
                    for (int kh = 0; kh < jcp.kh; ++kh) {
                    for (int oh = 0; oh < jcp.oh; ++oh) {
                        const int ih = oh * jcp.stride_h
                                       - jcp.t_pad + kh * (1 + jcp.dilate_h);
                        if (ih < 0 || ih >= jcp.ih) continue;
    
                        for (int kw = 0; kw < jcp.kw; ++kw) {
#pragma _NEC ivdep
                        for (int ow = 0; ow < jcp.ow; ++ow) {
                            const int iw = ow * jcp.stride_w
                                           - jcp.l_pad + kw * (1 + jcp.dilate_w);
                            if (iw < 0 || iw >= jcp.iw) continue;
    
                            const size_t col_idx = ((kh * jcp.kw + kw) * jcp.oh+oh)
                                                   * jcp.ow + ow;
                            const size_t im_idx = ih*jcp.iw + iw;
                            col_[col_idx] = im_[im_idx];
                        }}
                    }}
                    break;

                case 2:
                    for (int kh = 0; kh < jcp.kh; ++kh) {
                    for (int oh = 0; oh < jcp.oh; ++oh) {
                        const int ih = oh * jcp.stride_h
                                       - jcp.t_pad + kh * (1 + jcp.dilate_h);
                        if (ih < 0 || ih >= jcp.ih) continue;
    
                        for (int kw = 0; kw < jcp.kw; ++kw) {
#pragma _NEC ivdep
                        for (int ow = 0; ow < jcp.ow; ++ow) {
                            const int iw = ow * jcp.stride_w
                                           - jcp.l_pad + kw * (1 + jcp.dilate_w);
                            if (iw < 0 || iw >= jcp.iw) continue;
    
#pragma _NEC unroll(2)
                            for(int i = 0; i < 2; ++i) {
                                const size_t col_idx =  i*col_step +
                                                       ((kh * jcp.kw + kw) * jcp.oh+oh)
                                                       * jcp.ow + ow;
                                const size_t im_idx = i*im_step + ih*jcp.iw + iw;
                                col_[col_idx] = im_[im_idx];
                            }
                        }}
                    }}
                    break;

                case 3:
                    for (int kh = 0; kh < jcp.kh; ++kh) {
                    for (int oh = 0; oh < jcp.oh; ++oh) {
                        const int ih = oh * jcp.stride_h
                                       - jcp.t_pad + kh * (1 + jcp.dilate_h);
                        if (ih < 0 || ih >= jcp.ih) continue;
    
                        for (int kw = 0; kw < jcp.kw; ++kw) {
#pragma _NEC ivdep
                        for (int ow = 0; ow < jcp.ow; ++ow) {
                            const int iw = ow * jcp.stride_w
                                           - jcp.l_pad + kw * (1 + jcp.dilate_w);
                            if (iw < 0 || iw >= jcp.iw) continue;
    
#pragma _NEC unroll(3)
                            for(int i = 0; i < 3; ++i) {
                                const size_t col_idx =  i*col_step +
                                                       ((kh * jcp.kw + kw) * jcp.oh+oh)
                                                       * jcp.ow + ow;
                                const size_t im_idx = i*im_step + ih*jcp.iw + iw;
                                col_[col_idx] = im_[im_idx];
                            }
                        }}
                    }}
                    break;

                case 4:
                    for (int kh = 0; kh < jcp.kh; ++kh) {
                    for (int oh = 0; oh < jcp.oh; ++oh) {
                        const int ih = oh * jcp.stride_h
                                       - jcp.t_pad + kh * (1 + jcp.dilate_h);
                        if (ih < 0 || ih >= jcp.ih) continue;
    
                        for (int kw = 0; kw < jcp.kw; ++kw) {
#pragma _NEC ivdep
                        for (int ow = 0; ow < jcp.ow; ++ow) {
                            const int iw = ow * jcp.stride_w
                                           - jcp.l_pad + kw * (1 + jcp.dilate_w);
                            if (iw < 0 || iw >= jcp.iw) continue;
    
#pragma _NEC unroll(4)
                            for(int i = 0; i < 4; ++i) {
                                const size_t col_idx =  i*col_step +
                                                       ((kh * jcp.kw + kw) * jcp.oh+oh)
                                                       * jcp.ow + ow;
                                const size_t im_idx = i*im_step + ih*jcp.iw + iw;
                                col_[col_idx] = im_[im_idx];
                            }
                        }}
                    }}
                    break;

                case 5:
                    for (int kh = 0; kh < jcp.kh; ++kh) {
                    for (int oh = 0; oh < jcp.oh; ++oh) {
                        const int ih = oh * jcp.stride_h
                                       - jcp.t_pad + kh * (1 + jcp.dilate_h);
                        if (ih < 0 || ih >= jcp.ih) continue;
    
                        for (int kw = 0; kw < jcp.kw; ++kw) {
#pragma _NEC ivdep
                        for (int ow = 0; ow < jcp.ow; ++ow) {
                            const int iw = ow * jcp.stride_w
                                           - jcp.l_pad + kw * (1 + jcp.dilate_w);
                            if (iw < 0 || iw >= jcp.iw) continue;
    
#pragma _NEC unroll(5)
                            for(int i = 0; i < 5; ++i) {
                                const size_t col_idx =  i*col_step +
                                                       ((kh * jcp.kw + kw) * jcp.oh+oh)
                                                       * jcp.ow + ow;
                                const size_t im_idx = i*im_step + ih*jcp.iw + iw;
                                col_[col_idx] = im_[im_idx];
                            }
                        }}
                    }}
                    break;

                case 6:
                    for (int kh = 0; kh < jcp.kh; ++kh) {
                    for (int oh = 0; oh < jcp.oh; ++oh) {
                        const int ih = oh * jcp.stride_h
                                       - jcp.t_pad + kh * (1 + jcp.dilate_h);
                        if (ih < 0 || ih >= jcp.ih) continue;
    
                        for (int kw = 0; kw < jcp.kw; ++kw) {
#pragma _NEC ivdep
                        for (int ow = 0; ow < jcp.ow; ++ow) {
                            const int iw = ow * jcp.stride_w
                                           - jcp.l_pad + kw * (1 + jcp.dilate_w);
                            if (iw < 0 || iw >= jcp.iw) continue;
    
#pragma _NEC unroll(6)
                            for(int i = 0; i < 6; ++i) {
                                const size_t col_idx =  i*col_step +
                                                       ((kh * jcp.kw + kw) * jcp.oh+oh)
                                                       * jcp.ow + ow;
                                const size_t im_idx = i*im_step + ih*jcp.iw + iw;
                                col_[col_idx] = im_[im_idx];
                            }
                        }}
                    }}
                    break;

                case 7:
                    for (int kh = 0; kh < jcp.kh; ++kh) {
                    for (int oh = 0; oh < jcp.oh; ++oh) {
                        const int ih = oh * jcp.stride_h
                                       - jcp.t_pad + kh * (1 + jcp.dilate_h);
                        if (ih < 0 || ih >= jcp.ih) continue;
    
                        for (int kw = 0; kw < jcp.kw; ++kw) {
#pragma _NEC ivdep
                        for (int ow = 0; ow < jcp.ow; ++ow) {
                            const int iw = ow * jcp.stride_w
                                           - jcp.l_pad + kw * (1 + jcp.dilate_w);
                            if (iw < 0 || iw >= jcp.iw) continue;
    
#pragma _NEC unroll(7)
                            for(int i = 0; i < 7; ++i) {
                                const size_t col_idx =  i*col_step +
                                                       ((kh * jcp.kw + kw) * jcp.oh+oh)
                                                       * jcp.ow + ow;
                                const size_t im_idx = i*im_step + ih*jcp.iw + iw;
                                col_[col_idx] = im_[im_idx];
                            }
                        }}
                    }}
                    break;

                default:
                    printf("Bug - UNROLL IM2COL reset without changing remainder cases\n");
                    exit(1);
                    break;
            }
        }
    };

    if (jcp.ic != 1) {
        im2col_common(im, col);
    } else {
        im2col_1st(im, col);
    }
}

/* col[oh][ow][kh][kw][ic] <-- im2col_u8(im[ih][iw][ic]) */
void im2col_u8(
    jit_gemm_conv_conf_t &jcp, const uint8_t *im, uint8_t *col) {
    int num_thr = (jcp.mb != 1) ? omp_get_max_threads() : 1;
    MAYBE_UNUSED(num_thr);
    OMP(parallel for collapse(2) num_threads(num_thr))//;
    for (int oh = 0; oh < jcp.oh; ++oh) {
        for (int ow = 0; ow < jcp.ow; ++ow) {
            for (int kh = 0; kh < jcp.kh; ++kh) {
                const int ih = oh * jcp.stride_h
                    - jcp.t_pad + kh * (1 + jcp.dilate_h);
                if (ih < 0 || ih >= jcp.ih) continue;

                for (int kw = 0; kw < jcp.kw; ++kw) {
                    const int iw = ow * jcp.stride_w
                        - jcp.l_pad + kw * (1 + jcp.dilate_w);
                    if (iw < 0 || iw >= jcp.iw) continue;

                    const size_t col_idx = (((oh * jcp.ow + ow) * jcp.kh + kh)
                            * jcp.kw + kw) * jcp.ic;
                    const size_t im_idx
                        = (ih * jcp.iw + iw) * jcp.ngroups * jcp.ic;

                    OMPSIMD()//;
                    for (int ic = 0; ic < jcp.ic; ++ic) {
                        col[col_idx + ic] = im[im_idx + ic];
                    }
                }
            }
        }
    }
}

void col2im(
    jit_gemm_conv_conf_t &jcp, const float *col, float *im) {
    const size_t col_step = jcp.ks * jcp.os;
    const size_t im_step = jcp.ih * jcp.iw;
    const int iS = jcp.ih * jcp.iw;

    int num_thr = (jcp.mb != 1) ? omp_get_max_threads() : 1;
    MAYBE_UNUSED(num_thr);
    OMP(parallel for  num_threads(num_thr))//;
    for (int ic = 0; ic < jcp.ic; ++ic) {
        for (int is = 0; is < iS; ++is) im[is] = 0.;

        for (int oh = 0; oh < jcp.oh; ++oh) {
        for (int kh = 0; kh < jcp.kh; ++kh) {
            const int ih = oh * jcp.stride_h - jcp.t_pad + kh * (1 + jcp.dilate_h);
            if (ih < 0 || ih >= jcp.ih) continue;

            for (int ow = 0; ow < jcp.ow; ++ow) {
            for (int kw = 0; kw < jcp.kw; ++kw) {
                const int iw = ow * jcp.stride_w - jcp.l_pad + kw * (1 + jcp.dilate_w);
                if (iw < 0 || iw >= jcp.iw) continue;

                const size_t col_idx = ((kh*jcp.kw + kw)*jcp.oh+oh)*jcp.ow+ow;
                const size_t im_idx = ih*jcp.iw + iw;
                im[im_idx] += col[col_idx];
            }
            }
        }
        }
        col += col_step;
        im += im_step;
    }
}

void init_conf(
    jit_gemm_conv_conf_t &jcp, const convolution_desc_t &cd,
    const memory_desc_wrapper &src_d, const memory_desc_wrapper &weights_d,
    const memory_desc_wrapper &dst_d,
    bool with_relu, float relu_negative_slope) {

    const bool with_groups = weights_d.ndims() == src_d.ndims() + 1;
    jcp.prop_kind = cd.prop_kind;

    jcp.ngroups = with_groups ? weights_d.dims()[0] : 1;
    jcp.mb = src_d.dims()[0];

    jcp.oc = dst_d.dims()[1] / jcp.ngroups;
    jcp.ic = src_d.dims()[1] / jcp.ngroups;

    jcp.ih = src_d.dims()[2];
    jcp.iw = src_d.dims()[3];
    jcp.oh = dst_d.dims()[2];
    jcp.ow = dst_d.dims()[3];

    jcp.kh = weights_d.dims()[with_groups + 2];
    jcp.kw = weights_d.dims()[with_groups + 3];

    jcp.t_pad = cd.padding[0][0];
    jcp.l_pad = cd.padding[0][1];

    jcp.stride_h = cd.strides[0];
    jcp.stride_w = cd.strides[1];

    jcp.dilate_h = cd.dilates[0];
    jcp.dilate_w = cd.dilates[1];

    jcp.src_fmt = src_d.format();
    jcp.with_bias
        = cd.bias_desc.format != memory_format::undef
        || cd.diff_bias_desc.format != memory_format::undef;
    jcp.with_relu = with_relu;
    jcp.relu_negative_slope = relu_negative_slope;

    jcp.os = jcp.oh * jcp.ow;
    jcp.ks = jcp.kh * jcp.kw;
    jcp.need_im2col = !(jcp.oh == jcp.ih && jcp.ow == jcp.iw && jcp.ks == 1);
}

template <typename src_t>
status_t prepare_ws_col(jit_gemm_conv_conf_t &jcp, src_t **col) {
    if (!jcp.need_im2col) {
        *col = nullptr;
        return status::success;
    }

    const size_t nthr = omp_get_max_threads();
    const size_t im2col_sz_per_thr = jcp.os * jcp.ks * jcp.ic;
    const size_t im2col_sz = nthr * im2col_sz_per_thr;

    *col = (src_t *)malloc(im2col_sz * sizeof(src_t), 64);
    if (*col == nullptr) return status::out_of_memory;

    OMP(parallel for)//;
    for (size_t i = 0; i < im2col_sz; ++i) (*col)[i] = (src_t)0;

    return status::success;
}

template status_t prepare_ws_col<float>(jit_gemm_conv_conf_t &jcp,
        float **col);
template status_t prepare_ws_col<uint8_t>(jit_gemm_conv_conf_t &jcp,
        uint8_t **col);

status_t prepare_ws_wei_reduction(jit_gemm_conv_conf_t &jcp,
        float **wei_reduction, size_t wei_sz) {
    const size_t nthr = omp_get_max_threads();
    if (jcp.mb == 1 || nthr == 1)
        return status::success;

    const size_t sz_per_thr = jcp.ngroups * wei_sz; // XXX: why groups?
    *wei_reduction = (float *)malloc(nthr * sz_per_thr, 64);
    if (*wei_reduction == nullptr) return status::out_of_memory;

    return status::success;
}

template <typename acc_t>
status_t prepare_ws_acc(jit_gemm_conv_conf_t &jcp, acc_t **acc) {
    const size_t nthr = omp_get_max_threads();
    const size_t acc_sz_per_thr = jcp.os * jcp.oc;
    const size_t acc_sz = nthr * acc_sz_per_thr;

    *acc = (int32_t *)malloc(acc_sz * sizeof(acc_t), 64);
    if (*acc == nullptr) return status::out_of_memory;
    return status::success;
}

template status_t prepare_ws_acc<int32_t>(jit_gemm_conv_conf_t &jcp,
        int32_t **acc);

void bwd_weights_balance(int ithr, int nthr, int ngroups, int mb, int &ithr_g,
        int &nthr_g, int &ithr_mb, int &nthr_mb) {
    nthr_g = nstl::min(ngroups, nthr);
    nthr_mb = nstl::min(mb, nthr / nthr_g);
    if (ithr / nthr_mb >= ngroups) {
        ithr_g = ithr_mb = -1;
    } else {
        ithr_g = ithr / nthr_mb;
        ithr_mb = ithr % nthr_mb;
    }
}

void bwd_weights_reduction_par(int ithr, int nthr, const jit_gemm_conv_conf_t &jcp,
        const float *weights_reduce_ws, float *weights) {
    const size_t weights_g_size = jcp.ic * jcp.oc * jcp.ks;

    size_t weights_start{0}, weights_end{0};
    balance211(weights_g_size, nthr, ithr, weights_start, weights_end);

    for (int i = 0; i < nthr; ++i) {
        const float *ws_i = weights_reduce_ws + i * weights_g_size;
        for (size_t s = weights_start; s < weights_end; ++s)
            weights[s] = (i == 0 ? 0 : weights[s]) + ws_i[s];
    }
}

};

}
}
}
// vim: et ts=4 sw=4 cindent nopaste ai cino=^=l0,\:0,N-s
