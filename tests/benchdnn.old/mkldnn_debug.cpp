/*******************************************************************************
* Copyright 2017-2018 Intel Corporation
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

#include "mkldnn_debug.h"

#include "common.hpp"
#include "mkldnn_debug.hpp"

const char *status2str(mkldnn_status_t status) {
    return mkldnn_status2str(status);
}

const char *dt2str(mkldnn_data_type_t dt) {
    return mkldnn_dt2str(dt);
}

mkldnn_data_type_t str2dt(const char *str) {
#define CASE(_dt) \
    if (!strcasecmp(STRINGIFY(_dt), str) \
            || !strcasecmp(STRINGIFY(CONCAT2(mkldnn_, _dt)), str)) \
        return CONCAT2(mkldnn_, _dt);
    CASE(s8);
    CASE(u8);
    CASE(s16);
    CASE(s32);
    CASE(f32);
#undef CASE
    assert(!"unknown data type");
    return mkldnn_f32;
}

const char *rmode2str(mkldnn_round_mode_t rmode) {
#define CASE(_rmode) \
    if (CONCAT2(mkldnn_round_, _rmode) == rmode) return STRINGIFY(_rmode)
    CASE(nearest);
    CASE(down);
#undef CASE
    assert(!"unknown round mode");
    return "unknown round mode";
}

mkldnn_round_mode_t str2rmode(const char *str) {
#define CASE(_rmd) do { \
    if (!strncasecmp(STRINGIFY(_rmd), str, strlen(STRINGIFY(_rmd)))) \
        return CONCAT2(mkldnn_round_, _rmd); \
} while (0)
    CASE(nearest);
    CASE(down);
#undef CASE
    assert(!"unknown round_mode");
    return mkldnn_round_nearest;
}

const char *fmt2str(mkldnn_memory_format_t fmt) {
    return mkldnn_fmt2str(fmt);
}

mkldnn_memory_format_t str2fmt(const char *str) {
#define CASE(_fmt) do { \
    if (!strcmp(STRINGIFY(_fmt), str) \
            || !strcmp("mkldnn_" STRINGIFY(_fmt), str)) \
        return CONCAT2(mkldnn_, _fmt); \
} while (0)
    CASE(x);
    CASE(nc);
    CASE(nchw);
    CASE(nhwc);
    CASE(chwn);
#if 1 || MKLDNN_JIT_TYPES > 0
    CASE(nChw8c);
    CASE(nChw16c);
#endif
    CASE(ncdhw);
    CASE(ndhwc);
#if 1 || MKLDNN_JIT_TYPES > 0
    CASE(nCdhw8c);
    CASE(nCdhw16c);
#endif
    CASE(oi);
    CASE(io);
    CASE(oihw);
    CASE(ihwo);
    CASE(hwio);
    CASE(dhwio);
    CASE(oidhw);
#if 1 || MKLDNN_JIT_TYPES > 0
    CASE(OIdhw8i8o);
    CASE(OIdhw8o8i);
    CASE(Odhwi8o);
    CASE(OIdhw16i16o);
    CASE(OIdhw16o16i);
    CASE(Oidhw16o);
    CASE(Odhwi16o);
    CASE(oIhw8i);
    CASE(oIhw16i);
    CASE(oIdhw8i);
    CASE(oIdhw16i);
    CASE(OIhw8i8o);
    CASE(OIhw16i16o);
    CASE(OIhw8o8i);
    CASE(OIhw16o16i);
    CASE(IOhw16o16i);
    CASE(OIhw8i16o2i);
    CASE(OIdhw8i16o2i);
    CASE(OIhw8o16i2o);
    CASE(OIhw4i16o4i);
    CASE(Oihw8o);
    CASE(Oihw16o);
    CASE(Ohwi8o);
    CASE(Ohwi16o);
#endif
    CASE(goihw);
    CASE(hwigo);
#if 1 || MKLDNN_JIT_TYPES > 0
    CASE(gOIhw8i8o);
    CASE(gOIhw16i16o);
    CASE(gOIhw8i16o2i);
    CASE(gOIdhw8i16o2i);
    CASE(gOIhw8o16i2o);
    CASE(gOIhw4i16o4i);
    CASE(gOihw8o);
    CASE(gOihw16o);
    CASE(gOhwi8o);
    CASE(gOhwi16o);
    CASE(Goihw8g);
    CASE(Goihw16g);
    CASE(gOIhw8o8i);
    CASE(gOIhw16o16i);
    CASE(gIOhw16o16i);
#endif
    CASE(goidhw);
#if 1 || MKLDNN_JIT_TYPES > 0
    CASE(gOIdhw8i8o);
    CASE(gOIdhw8o8i);
    CASE(gOdhwi8o);
    CASE(gOIdhw16i16o);
    CASE(gOIdhw16o16i);
    CASE(gOidhw16o);
    CASE(gOdhwi16o);
#endif
    CASE(ntc);
    CASE(tnc);
    CASE(ldsnc);
    CASE(ldigo);
    CASE(ldigo_p);
    CASE(ldgoi);
    CASE(ldgoi_p);
    CASE(ldgo);
    //CASE(wino_fmt); // ?
#undef CASE
    assert(!"unknown memory format");
    return mkldnn_format_undef;
}
