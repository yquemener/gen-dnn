#!/bin/bash
#
#################
if [ `uname` == 'SUPER-UX' ]; then
	#BUILD=build-sx
	# let's take the MOST RECENT of build-sx or build-sxd directories ...
	BUILD=`ls -ldst build-sx* | grep ' drwx' | head -1 | sed 's/.*\(build.*\)/\1/'`
	LOGDIR=guest/sx
	mkdir -p guest/sx
	chmod ugo+w guest
	chmod ugo+w guest/sx
else
	#     select one:
	BUILD=build
	#BUILD=buildd
	#BUILD=build-jit
	#BUILD=build-jitd
	(cd ${BUILD}/tests/benchdnn && VERBOSE=1 make) || { echo "Compile issues?"; exit; }
	LOGDIR=./
fi
#################
#
BENCHDIR=${BUILD}/tests/benchdnn
if [ `uname` == 'SUPER-UX' ]; then
	chmod -R ugo+w ${BUILD}
	chmod ugo+w ${BENCHDIR}
fi
if [ "${MKLROOT}" != "" ]; then
	module unload icc >& /dev/null || echo "module icc unloaded"
	if [ "${MKLROOT}" != "" ]; then
		echo "Please compile in an environment without MKLROOT"
		exit -1;
	fi
	# export -n MKLROOT
	# export MKL_THREADING_LAYER=INTEL # maybe ???
fi
echo "BUILD    directory : ${BUILD}"
echo "LOGDIR   directory : ${LOGDIR}"
echo "benchdnn directory : ${BENCHDIR}"
ls -ld ${BENCHDIR}
cat <<EOF
Here are the output fields for performance benchmarks:
	string: perf
	convolution name
	full conv-desc
	number of giga ops calculated
	effective cpu frequency in GHz (amb clocks[min] / time[min])
	minimum time spent in ms
	best gigaops (since it corresponds to mimimum time)
	average time spent in ms
	average gigaops (since it corresponds to average time)
as reported by the default output template
	perf,%n,%d,%GO,%GF,%-t,%-Gp,%0t,%0Gp
EOF
echo "Bench Convolution Performance for just fwd alexnet:conv1 ..."
RUNME=./benchdnn
#
# Comment these out to use system default (all for Intel)
#
THREADS=1
export OMP_NUM_THREADS=${THREADS}
export MKL_NUM_THREADS=${THREADS}
{
(cd ${BENCHDIR} && ./benchdnn --conv --mode=CP -v3 --cfg=f32 --dir=FWD_B mb1_ic3ih227iw227_oc96oh55ow55_kh11kw11_sh4sw4ph0pw0_nalexnet:conv1 ) || { echo "Ohoh"; exit; }
(cd ${BENCHDIR} && ./benchdnn --conv --mode=CP -v3 --cfg=f32 --dir=FWD_B mb12_ic3ih227iw227_oc96oh55ow55_kh11kw11_sh4sw4ph0pw0_nalexnet:conv1 ) || { echo "Ohoh"; exit; }
(cd ${BENCHDIR} && ./benchdnn --conv --mode=CP -v3 --cfg=f32 --dir=FWD_B mb32_ic3ih227iw227_oc96oh55ow55_kh11kw11_sh4sw4ph0pw0_nalexnet:conv1 ) || { echo "Ohoh"; exit; }
} 2>&1 | tee ${LOGDIR}/bench-quick-t${THREADS}.log

if [ `uname` == 'SUPER-UX' ]; then
	echo "Skipping long tests"
else
	echo "Bench Convolution Performance for default fwd conv layers (many!) ..."
	LGBASE="${LOGDIR}/bench-convP-t${THREADS}"
	(cd ${BENCHDIR} && ./benchdnn --conv --mode=P -v3 --cfg=f32 --dir=FWD_B ) 2>&1 | tee ${LGBASE}-tmp.log \
	&& mv ${LGBASE}-tmp.log ${LGBASE}.log && echo "${LGBASE}.log OK" \
	|| { echo "${LGBASE}-tmp.log ERROR"; exit; }

	echo "Bench Convolution Correctness for default fwd conv layers (many!) ..."
	LGBASE="${LOGDIR}/bench-convC-t${THREADS}"
	(cd ${BENCHDIR} && ./benchdnn --conv          -v3 --cfg=f32 --dir=FWD_B ) 2>&1 | tee ${LGBASE}-tmp.log \
	&& mv ${LGBASE}-tmp.log ${LGBASE}.log && echo "${LGBASE}.log OK" \
	|| { echo "${LGBASE}-tmp.log ERROR"; exit; }
fi
#
