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

#include "perf.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <float.h>
#include <math.h>

#include "mkldnn.h"

#include "common.hpp"
#include "mkldnn_common.hpp"
#include "mkldnn_memory.hpp"

#include "self/self.hpp"
#include "conv/conv.hpp"
#include "conv/deconv.hpp"
#include "ip/ip.hpp"
#include "reorder/reorder.hpp"
#include "bnorm/bnorm.hpp"
#include "rnn/rnn.hpp"

#if defined(_OPENMP)
#include <omp.h>
#else
inline int omp_get_max_threads() { return 1; }
inline int omp_get_num_threads() { return 1; }
inline int omp_get_thread_num() { return 0; }
inline int omp_in_parallel() { return 0; }
#endif

int verbose {0};
bench_mode_t bench_mode {CORR};
stat_t benchdnn_stat {0};

double max_ms_per_prb {3e3};
int min_times_per_prb {5};
int fix_times_per_prb {0};

int main(int argc, char **argv) {
    prim_t prim = DEF;
    --argc; ++argv;

    while (argc > 0) {
        if (!strcmp("--self", argv[0])) prim = SELF;
        else if (!strcmp("--conv", argv[0])) prim = CONV;
        else if (!strcmp("--deconv", argv[0])) prim = DECONV;
        else if (!strcmp("--ip", argv[0])) prim = IP;
        else if (!strcmp("--reorder", argv[0])) prim = REORDER;
        else if (!strcmp("--bnorm", argv[0])) prim = BNORM;
        else if (!strcmp("--rnn", argv[0])) prim = RNN;
        else if (!strncmp("--mode=", argv[0], 7))
            bench_mode = str2bench_mode(argv[0] + 7);
        else if (!strncmp("-v", argv[0], 2))
            verbose = atoi(argv[0] + 2);
        else if (!strncmp("--verbose=", argv[0], 10))
            verbose = atoi(argv[0] + 10);
        else break;

        --argc;
        ++argv;
    }

    int omp_max_thr = omp_get_max_threads();
    printf("benchdnn --%s --mode=%s -v%d ... init omp_max_thr=%d ",
                (prim==CONV? "conv": prim==IP? "ip": prim==SELF? "self"
                 : prim==REORDER? "reorder": prim==BNORM? "bnorm": "Huh?"),
                bench_mode2str(bench_mode), verbose, omp_max_thr);
    fflush(stdout);

    init();
    printf(" OK\n"); fflush(stdout);
    // [ejk] perf stuff is a stub init for rdtsc/rdpmc/frequency governor.
    //       It is not necessary with 'ticks' removed from benchdnn.
    //       If reintroduced, an alternate approach is to expose xbyak's
    //       rdtsc support within mkldnn headers.
    perf_t const * perf_data = perf_begin();
    if(perf_data == nullptr) { // [ejk] may need some timing system init
        printf("ERROR: perf_begin failed!\n");
        exit(-1);
    }

    switch (prim) {
    case SELF: self::bench(argc, argv); break;
    case CONV: conv::bench(argc, argv); break;
    case DECONV: deconv::bench(argc, argv); break;
    case IP: ip::bench(argc, argv); break;
    case REORDER: reorder::bench(argc, argv); break;
    case BNORM: bnorm::bench(argc, argv); break;
    case RNN: rnn::bench(argc, argv); break;
    default: fprintf(stderr, "err: unknown driver\n");
    }

    perf_end(perf_data);
    finalize();

    printf("tests:%d impls:%d %s:%d skipped:%d mistrusted:%d unimplemented:%d "
            "failed:%d",
            benchdnn_stat.tests, benchdnn_stat.impls,
            (bench_mode&CORR? "correct": "passed"), benchdnn_stat.passed,
            benchdnn_stat.skipped, benchdnn_stat.mistrusted,
            benchdnn_stat.unimplemented, benchdnn_stat.failed);
    if (bench_mode & PERF)
        printf(" total perf: min(ms):%g avg(ms):%g\n",
                benchdnn_stat.ms[benchdnn_timer_t::min],
                benchdnn_stat.ms[benchdnn_timer_t::avg]);
    if ((bench_mode & TEST))
        printf(" test_fail: %d", benchdnn_stat.test_fail);
    printf("\n");

    return !!benchdnn_stat.failed;
}
