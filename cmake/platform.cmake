#===============================================================================
# Copyright 2016-2018 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#===============================================================================

# Manage platform-specific quirks
#===============================================================================

if(platform_cmake_included)
    return()
endif()
set(platform_cmake_included true)

add_definitions(-DMKLDNN_DLL -DMKLDNN_DLL_EXPORTS)

# UNIT8_MAX-like macros are a part of the C99 standard and not a part of the
# C++ standard (see C99 standard 7.18.2 and 7.18.4)
add_definitions(-D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS)

set(CMAKE_CCXX_FLAGS)
set(CMAKE_CCXX_NOWARN_FLAGS)
set(DEF_ARCH_OPT_FLAGS)

function(error_if_SX)
    if(NECSX)
        message(WARNING "Error: should not be here for NECSX")
    endif()
endfunction()

message(STATUS " WIN32 : ${WIN32}")
message(STATUS " NECSX : ${NECSX}")
message(STATUS " APPLE : ${APPLE}")
message(STATUS " UNIX  : ${UNIX}")
message(STATUS " begin platform.cmake with CMAKE_CXX_FLAGS =${CMAKE_CXX_FLAGS}")
if(MSVC)
    message(STATUS "cmake/platform.cmake MSVC branch")
    set(USERCONFIG_PLATFORM "x64")
    if(${CMAKE_CXX_COMPILER_ID} STREQUAL MSVC)
        set(CMAKE_CCXX_FLAGS "${CMAKE_CCXX_FLAGS} /MP")
        # int -> bool
        set(CMAKE_CCXX_NOWARN_FLAGS "${CMAKE_CCXX_NOWARN_FLAGS} /wd4800")
        # unknown pragma
        set(CMAKE_CCXX_NOWARN_FLAGS "${CMAKE_CCXX_NOWARN_FLAGS} /wd4068")
        # double -> float
        set(CMAKE_CCXX_NOWARN_FLAGS "${CMAKE_CCXX_NOWARN_FLAGS} /wd4305")
        # UNUSED(func)
        set(CMAKE_CCXX_NOWARN_FLAGS "${CMAKE_CCXX_NOWARN_FLAGS} /wd4551")
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Intel")
        set(CMAKE_CCXX_FLAGS "${CMAKE_CCXX_FLAGS} /MP")
        set(DEF_ARCH_OPT_FLAGS "-QxHOST")
        # disable: loop was not vectorized with "simd"
        set(CMAKE_CCXX_NOWARN_FLAGS
            "${CMAKE_CCXX_NOWARN_FLAGS} -Qdiag-disable:15552")
    endif()
    #set(CTESTCONFIG_PATH "$ENV{PATH}")
    #string(REPLACE ";" "\;" CTESTCONFIG_PATH "${CTESTCONFIG_PATH}")
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # Clang cannot vectorize some loops with #pragma omp simd and gets
        # very upset. Tell it that it's okay and that we love it
        # unconditionnaly.
        set(CMAKE_CCXX_NOWARN_FLAGS
            "${CMAKE_CCXX_NOWARN_FLAGS} -Wno-pass-failed")
    endif()
elseif(NECSX)
    message(STATUS "cmake/platform.cmake NECSX branch")
    #show_cmake_stuff("NECSX, after project(... C CXX)")
    message(STATUS "Other NEC SX options here ...")
    # OHOH. this appear *before* -lmkldnn, and we want it to appear *after*
    #set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${SX_LINK_LIBRARIES}")
    #set(CMAKE_STATIC_LINKER_FLAGS "${CMAKE_STATIC_LINKER_FLAGS} ${SX_LINK_LIBRARIES}")
    #
    # -----------------------------------------------------------------------
    # These changes may eventually percolate into dev-cmake-sx and Platform/
    #
    #   override to better run SX dbx debugger ... (was with -Cvopt)
    #   Original: -g -ftrace -Cvopt
    #       (should be -gv -ftrace -CVopt ???)
    #
    # Note: do not use -Cdbeug or -Cnoopt (semantics of language changes?)
    set(CMAKE_C_FLAGS_DEBUG "-g2 -traceback -Nipa -Nv -pi,noinline -math,inline,scalar -dir,noopt,nopar,novec -pvctl,fullmsg")
    set(CMAKE_CXX_FLAGS_DEBUG "-g2 -traceback -Nipa -Nv -pi,noinline -math,inline,scalar -dir,noopt,nopar,novec -pvctl,fullmsg")
    #set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-g") # should NOT conflict (but did!)
    # -----------------------------------------------------------------------
    show_cmake_stuff("NECSX, after project(... C CXX)")
    #else()
    #  project(${PROJECT_NAME} C CXX)
    #  include("cmake/MKL.cmake")
    #  set(CMAKE_C_FLAGS_DEBUG "-g -O0")
    #  set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
elseif(UNIX OR APPLE OR MINGW)
    set(CMAKE_CCXX_FLAGS "${CMAKE_CCXX_FLAGS} -Wall")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99")

    if(NECVE)
        add_definitions(-DSXAURORA -D_GNU_SOURCE)
        set(CMAKE_CCXX_FLAGS "${CMAKE_CCXX_FLAGS} -finline -finline-functions -pthread")
        # Kudoh-san gave internal option for recursive function limit (formal option coming soon)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11 -We,--pending_instantiations=100")
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fvisibility-inlines-hidden")
	set(CMAKE_CCXX_FLAGS "${CMAKE_CCXX_FLAGS} -Werror -fvisibility=internal -Wno-unknown-pragmas")
    endif()
    # compiler specific settings
    if(NECVE) # masquerades as GNU 6.0.0, but does not quite support all the flags
        set(CMAKE_CCXX_FLAGS "${CMAKE_CCXX_FLAGS} -fdiag-parallel=2 -ffast-math")
        if(VEJIT)
            set(CMAKE_CCXX_FLAGS "${CMAKE_CCXX_FLAGS} -DVEJIT=100")
        else()
            set(CMAKE_CCXX_FLAGS "${CMAKE_CCXX_FLAGS} -DVEJIT=0")
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # Clang cannot vectorize some loops with #pragma omp simd and gets
        # very upset. Tell it that it's okay and that we love it
        # unconditionnaly.
        set(CMAKE_CCXX_NOWARN_FLAGS
            "${CMAKE_CCXX_NOWARN_FLAGS} -Wno-pass-failed")
    elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        if(NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 5.0)
            set(DEF_ARCH_OPT_FLAGS "-march=native -mtune=native")
        endif()
        if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 6.0)
            # suppress warning on assumptions made regarding overflow (#146)
            set(CMAKE_CCXX_NOWARN_FLAGS
                "${CMAKE_CCXX_NOWARN_FLAGS} -Wno-strict-overflow")
	    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=native -ffast-math")
	    # maybe also -ffinite-math or -funsafe-math-optimizations ?
	else()
	    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=native -ffast-math -fbuiltin")
        endif()
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Intel")
        set(DEF_ARCH_OPT_FLAGS "-xHOST")
        # workaround for Intel Compiler 16.0 that produces error caused
        # by pragma omp simd collapse(..)
        if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "17.0")
            set(CMAKE_CCXX_NOWARN_FLAGS
                "${CMAKE_CCXX_NOWARN_FLAGS} -diag-disable:13379")
        endif()
        set(CMAKE_CCXX_NOWARN_FLAGS
            "${CMAKE_CCXX_NOWARN_FLAGS} -diag-disable:15552")
    endif()
endif()

if(WIN32)
    set(CTESTCONFIG_PATH "$ENV{PATH}")
    string(REPLACE ";" "\;" CTESTCONFIG_PATH "${CTESTCONFIG_PATH}")
endif()

if(UNIX OR APPLE OR MINGW)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Intel")
        # Link Intel libraries statically (except for iomp5)
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -liomp5 -static-intel")
        # Tell linker to not complain about missing static libraries
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -diag-disable:10237")
    endif()
endif()

if(ARCH_OPT_FLAGS STREQUAL "HostOpts")
    set(ARCH_OPT_FLAGS "${DEF_ARCH_OPT_FLAGS}")
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${CMAKE_CCXX_FLAGS} ${ARCH_OPT_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CCXX_FLAGS} ${ARCH_OPT_FLAGS}")

if(APPLE)
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
    # FIXME: this is ugly but required when compiler does not add its library
    # paths to rpath (like Intel compiler...)
    foreach(_ ${CMAKE_C_IMPLICIT_LINK_DIRECTORIES})
        set(_rpath "-Wl,-rpath,${_}")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${_rpath}")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${_rpath}")
    endforeach()
endif()
message(STATUS " end platform.cmake with CMAKE_CXX_FLAGS =${CMAKE_CXX_FLAGS}")
# vim: et sw=4 ts=4 ai
