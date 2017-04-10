/*******************************************************************************
* Copyright 2017 Intel Corporation
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

#include <assert.h>
#include <math.h>

#include <float.h>

#include "c_types_map.hpp"
#include "type_helpers.hpp"

#include "nchw_pooling.hpp"

namespace mkldnn {
namespace impl {
namespace cpu {

template <impl::data_type_t data_type>
void nchw_pooling_fwd_t<data_type>::execute_forward() {
    using namespace alg_kind;

    auto src = reinterpret_cast<const data_t *>(this->input_memory(0));
    auto dst = reinterpret_cast<data_t*>(this->memory(0));
    auto ws = conf_.desc()->alg_kind == alg_kind::pooling_avg ? nullptr
        : reinterpret_cast<int*>(this->memory(1));

    const int MB = conf_.MB();
    const int C = conf_.C();
    const int OH = conf_.OH();
    const int OW = conf_.OW();
    const int IH = conf_.IH();
    const int IW = conf_.IW();
    const int KH = conf_.KH();
    const int KW = conf_.KW();
    const int SH = conf_.KSH();
    const int SW = conf_.KSW();
    const int padT = conf_.padT();
    const int padL = conf_.padL();

    auto ker_max = [=](data_t *d, int mb, int c, int oh, int ow) {
        for (int kh = 0; kh < KH; ++kh) {
            for (int kw = 0; kw < KW; ++kw) {
                const int ih = oh * SH - padT + kh;
                const int iw = ow * SW - padL + kw;

                if (ih < 0 || ih >= IH) continue;
                if (iw < 0 || iw >= IW) continue;

                auto src_offset = mb*C*IH*IW + c*IH*IW + ih*IW + iw;
                auto s = src[src_offset];
                if (s > d[0]) {
                    d[0] = s;
                    if (ws) {
                        auto ws_offset = mb*C*OH*OW + c*OH*OW + oh*OW + ow;
                        ws[ws_offset] = kh*KW + kw;
                    }
                }
            }
        }
    };

    auto ker_avg = [=](data_t *d, int mb, int c, int oh, int ow) {
        for (int kh = 0; kh < KH; ++kh) {
            for (int kw = 0; kw < KW; ++kw) {
                const int ih = oh * SH - padT + kh;
                const int iw = ow * SW - padL + kw;

                if (ih < 0 || ih >= IH) continue;
                if (iw < 0 || iw >= IW) continue;

                auto src_offset = mb*C*IH*IW + c*IH*IW + ih*IW + iw;
                d[0] += src[src_offset];
            }
        }
    };


    if (conf_.desc()->alg_kind == pooling_max) {
#       pragma omp parallel for collapse(4) schedule(static)
        for (int mb = 0; mb < MB; ++mb) {
            for (int c = 0; c < C; ++c) {
                for (int oh = 0; oh < OH; ++oh) {
                    for (int ow = 0; ow < OW; ++ow) {
                        auto dst_offset = mb*C*OH*OW + c*OH*OW + oh*OW + ow;
                        data_t *d = &dst[dst_offset];
                        d[0] = -FLT_MAX;
                        ker_max(d, mb, c, oh, ow);
                    }
                }
            }
        }
    } else {
#       pragma omp parallel for collapse(4) schedule(static)
        for (int mb = 0; mb < MB; ++mb) {
            for (int c = 0; c < C; ++c) {
                for (int oh = 0; oh < OH; ++oh) {
                    for (int ow = 0; ow < OW; ++ow) {
                        auto dst_offset = mb*C*OH*OW + c*OH*OW + oh*OW + ow;
                        data_t *d = &dst[dst_offset];
                        d[0] = 0;
                        ker_avg(d, mb, c, oh, ow);
                        d[0] /= KW*KH;
                    }
                }
            }
        }
    }
}

template <impl::data_type_t data_type>
void nchw_pooling_bwd_t<data_type>::execute_backward() {
    using namespace alg_kind;

    auto diff_dst = reinterpret_cast<const data_t *>(this->input_memory(0));
    auto ws = conf_.desc()->alg_kind == alg_kind::pooling_avg ? nullptr
        : reinterpret_cast<const int*>(this->input_memory(1));
    auto diff_src = reinterpret_cast<data_t*>(this->memory(0));

    const memory_desc_wrapper ws_d(conf_.workspace_pd());

    const int MB = conf_.MB();
    const int C = conf_.C();
    const int OH = conf_.OH();
    const int OW = conf_.OW();
    const int IH = conf_.IH();
    const int IW = conf_.IW();
    const int KH = conf_.KH();
    const int KW = conf_.KW();
    const int SH = conf_.KSH();
    const int SW = conf_.KSW();
    const int padT = conf_.padT();
    const int padL = conf_.padL();

    auto ker_zero = [=](int mb, int c) {
       auto diff_src_offset = mb*C*IH*IW + c*IH*IW;
        for (int ih = 0; ih < IH; ++ih) {
            for (int iw = 0; iw < IW; ++iw) {
                diff_src[diff_src_offset++] = 0;
            }
        }
    };


    auto ker_max = [=](const data_t *d, int mb, int c, int oh, int ow) {
        auto b_c = ws_d.blocking_desc().block_dims[1];
        auto ws_offset = ws_d.blk_off(mb, c / b_c, oh, ow) + c % b_c;
        const int index = ws[ws_offset];
        const int kw = index % KW;
        const int kh = index / KW;
        const int ih = oh * SH - padT + kh;
        const int iw = ow * SW - padL + kw;

        auto diff_src_offset = mb*C*IH*IW + c*IH*IW + ih*IW + iw;
        diff_src[diff_src_offset] += d[0];
    };

    auto ker_avg = [=](const data_t *d, int mb, int c, int oh, int ow) {
        for (int kh = 0; kh < KH; ++kh) {
            for (int kw = 0; kw < KW; ++kw) {
                const int ih = oh * SH - padT + kh;
                const int iw = ow * SW - padL + kw;

                if (ih < 0 || ih >= IH) continue;
                if (iw < 0 || iw >= IW) continue;

                auto diff_src_offset = mb*C*IH*IW + c*IH*IW + ih*IW + iw;
                diff_src[diff_src_offset] += d[0] / (KH * KW);
            }
        }
    };

    if (conf_.desc()->alg_kind == pooling_max) {
#       pragma omp parallel for collapse(2) schedule(static)
        for (int mb = 0; mb < MB; ++mb) {
            for (int c = 0; c < C; ++c) {
                auto diff_dst_offset = mb*C*OH*OW + c*OH*OW;
                ker_zero(mb, c);
                for (int oh = 0; oh < OH; ++oh) {
                    for (int ow = 0; ow < OW; ++ow) {
                        const data_t *d = &diff_dst[diff_dst_offset++];
                        ker_max(d, mb, c, oh, ow);
                    }
                }
            }
        }
    } else {
#       pragma omp parallel for collapse(2) schedule(static)
        for (int mb = 0; mb < MB; ++mb) {
            for (int c = 0; c < C; ++c) {
                auto diff_dst_offset = mb*C*OH*OW + c*OH*OW;
                ker_zero(mb, c);
                for (int oh = 0; oh < OH; ++oh) {
                    for (int ow = 0; ow < OW; ++ow) {
                        const data_t *d = &diff_dst[diff_dst_offset++];
                        ker_avg(d, mb, c, oh, ow);
                    }
                }
            }
        }
    }
}

template struct nchw_pooling_fwd_t<data_type::f32>;
template struct nchw_pooling_bwd_t<data_type::f32>;

}
}
}

// vim: et ts=4 sw=4 cindent cino^=l0,\:0,N-s
