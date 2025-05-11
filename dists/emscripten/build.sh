#!/bin/bash
#
# .dists/emscripten/build.sh -- Sets up an emscripten build environment and builds ScummVM for webassembly
#
# ScummVM is the legal property of its developers, whose names
# are too numerous to list here. Please refer to the COPYRIGHT
# file distributed with this source distribution.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#



# exit when any command fails
set -e

ROOT_FOLDER=$(pwd)
DIST_FOLDER="$ROOT_FOLDER/dists/emscripten"
LIBS_FOLDER="$DIST_FOLDER/libs"
TASKS=()
CONFIGURE_ARGS=()
_verbose=false
EMSDK_VERSION="4.0.8"
EMSCRIPTEN_VERSION="$EMSDK_VERSION"

usage="\
Usage: ./dists/emscripten/build.sh [TASKS] [OPTIONS]

Output the configuration name of the system \`$me' is run on.

Tasks: 
  (space separated) List of tasks to run. See ./dists/emscripten/README.md for details.    

Options:
  -h, --help         print this help, then exit
  -v, --verbose      print all commands run by the script
  --*                all other options are passed on to the configure script
                     Note: --enable-a52, --enable-faad, --enable-mad, --enable-mpeg2,
                     --enable-theoradec and --enable-vpx also fetche and build the dependency
"

_liba52=false
_libfaad=false
_libmad=false
_libmpeg2=false
_libtheoradec=false
_libvpx=false
_fluidlite=false

# parse inputs
for i in "$@"; do
  case $i in
  --enable-a52)
    _liba52=true
    CONFIGURE_ARGS+=" $i"
    ;;
  --enable-faad)
    _libfaad=true
    CONFIGURE_ARGS+=" $i"
    ;;
  --enable-fluidlite)
    _fluidlite=true
    CONFIGURE_ARGS+=" $i"
    ;;
  --enable-mad)
    _libmad=true
    CONFIGURE_ARGS+=" $i"
    ;;
  --enable-mpeg2)
    _libmpeg2=true
    CONFIGURE_ARGS+=" $i"
    ;;
  --enable-theoradec)
    _libtheoradec=true
    CONFIGURE_ARGS+=" $i"
    ;;
  --enable-vpx)
    _libvpx=true
    CONFIGURE_ARGS+=" $i"
    ;;
  -h | --help)
    echo "$usage"
    exit
    ;;
  -v | --verbose)
    _verbose=true
    ;;
  -* | --*)
    CONFIGURE_ARGS+=" $i"
    ;;
  *)
    TASKS+="|$i" # save positional arg
    shift        # past argument
    ;;
  esac
done

TASKS="${TASKS:1}"
if [[ -z "$TASKS" ]]; then
  echo "$usage"
  exit
fi

# print commands
if [[ "$_verbose" = true ]]; then
  set -o xtrace
fi

#################################
# Setup Toolchain
#################################

# Download Emscripten
if [[ ! -d "$DIST_FOLDER/emsdk-$EMSDK_VERSION" ]]; then
  echo "$DIST_FOLDER/emsdk-$EMSDK_VERSION not found. Installing Emscripten"
  cd "$DIST_FOLDER"
  if [[ "$EMSDK_VERSION" = "tot" ]]; then
    git clone "https://github.com/emscripten-core/emsdk/" emsdk-tot
  else
    wget -nc --content-disposition --no-check-certificate "https://github.com/emscripten-core/emsdk/archive/refs/tags/${EMSDK_VERSION}.tar.gz"
    tar -xf "emsdk-${EMSDK_VERSION}.tar.gz"
  fi
fi

# Activate Emscripten
cd "$DIST_FOLDER/emsdk-${EMSDK_VERSION}"
ret=0 # https://stackoverflow.com/questions/18621990/bash-get-exit-status-of-command-when-set-e-is-active
./emsdk activate ${EMSCRIPTEN_VERSION} || ret=$?
if [[ $ret != 0 ]]; then
  echo "install missing emscripten version"
  cd "$DIST_FOLDER/emsdk-${EMSDK_VERSION}"
  ./emsdk install ${EMSCRIPTEN_VERSION}

  cd "$DIST_FOLDER/emsdk-${EMSDK_VERSION}"
  ./emsdk activate ${EMSCRIPTEN_VERSION}
fi

source "$DIST_FOLDER/emsdk-$EMSDK_VERSION/emsdk_env.sh"
LIBS_FLAGS=""

cd "$ROOT_FOLDER"
#################################
# Clean
#################################
if [[ "clean" =~ $(echo ^\(${TASKS}\)$) ]]; then
  emmake make clean || true
  emmake make distclean || true
  emcc --clear-ports --clear-cache
  rm -rf ./build-emscripten/ || true
  rm scummvm.debug.wasm || true
  rm scummvm.wasm || true
  rm scummvm.js || true
fi

#################################
# Download + Install Libraries (if not part of Emscripten-Ports, these are handled by configure)
#################################
#TODO: THese could/should use the external port facility https://github.com/emscripten-core/emscripten/pull/21316
if [[ ! -d "$LIBS_FOLDER/build" ]]; then
  mkdir -p "$LIBS_FOLDER/build"
fi

if [ "$_liba52" = true ]; then
  if [[ ! -f "$LIBS_FOLDER/build/lib/liba52.a" ]]; then
    echo "building a52dec-0.7.4"
    cd "$LIBS_FOLDER"
    wget -nc "https://liba52.sourceforge.io/files/a52dec-0.7.4.tar.gz"
    tar -xf a52dec-0.7.4.tar.gz
    cd "$LIBS_FOLDER/a52dec-0.7.4/"
    CFLAGS="-fPIC -Oz" emconfigure ./configure --host=wasm32-unknown-none --build=wasm32-unknown-none --prefix="$LIBS_FOLDER/build/"
    emmake make -j 5
    emmake make install
  fi
  LIBS_FLAGS="${LIBS_FLAGS} --with-a52-prefix=$LIBS_FOLDER/build"
fi

if [ "$_libfaad" = true ]; then
  if [[ ! -f "$LIBS_FOLDER/build/lib/libfaad.a" ]]; then
    echo "building faad2-2.8.8"
    cd "$LIBS_FOLDER"
    wget -nc "https://sourceforge.net/projects/faac/files/faad2-src/faad2-2.8.0/faad2-2.8.8.tar.gz"
    tar -xf faad2-2.8.8.tar.gz
    cd "$LIBS_FOLDER/faad2-2.8.8/"
    CFLAGS="-fPIC -Oz" emconfigure ./configure --host=wasm32-unknown-none --build=wasm32-unknown-none --prefix="$LIBS_FOLDER/build/"
    emmake make -j 5
    emmake make install
  fi
  LIBS_FLAGS="${LIBS_FLAGS} --with-faad-prefix=$LIBS_FOLDER/build"
fi

if [ "$_fluidlite" = true ]; then
  if [[ ! -f "$LIBS_FOLDER/build/lib/libfluidlite.a" ]]; then
    echo "building fluidlite-d59d232"
    cd "$LIBS_FOLDER"
    wget -nc --content-disposition "https://github.com/divideconcept/FluidLite/archive/d59d232.tar.gz"
    tar -xf FluidLite-d59d2328818f913b7d1a6a59aed695c47a8ce388.tar.gz
    cd "$LIBS_FOLDER/FluidLite-d59d2328818f913b7d1a6a59aed695c47a8ce388/"
    emcmake cmake -B "build/"   -DFLUIDLITE_BUILD_STATIC:BOOL="1"  -DCMAKE_INSTALL_PREFIX="$LIBS_FOLDER/build/" -DCMAKE_INSTALL_LIBDIR="lib"
    cmake --build "build/"  
    cmake --install "build/"  
  fi
  LIBS_FLAGS="${LIBS_FLAGS} --with-fluidlite-prefix=$LIBS_FOLDER/build"
fi


if [ "$_libmad" = true ]; then
  if [[ ! -f "$LIBS_FOLDER/build/lib/libmad.a" ]]; then
    echo "building libmad-0.15.1b"
    cd "$LIBS_FOLDER"
    wget -nc "https://downloads.sourceforge.net/mad/libmad-0.15.1b.tar.gz"
    tar -xf libmad-0.15.1b.tar.gz
    cd "$LIBS_FOLDER/libmad-0.15.1b/"
    # libmad needs patching as -fforce-mem has been removed in GCC 4.3 and later
    sed -i -e 's/-fforce-mem//g' configure
    CFLAGS="-Oz" emconfigure ./configure --host=wasm32-unknown-none --build=wasm32-unknown-none --prefix="$LIBS_FOLDER/build/" --with-pic --enable-fpm=no
    emmake make -j 5
    emmake make install
  fi
  LIBS_FLAGS="${LIBS_FLAGS} --with-mad-prefix=$LIBS_FOLDER/build"
fi

if [ "$_libmpeg2" = true ]; then
  if [[ ! -f "$LIBS_FOLDER/build/lib/libmpeg2.a" ]]; then
    echo "building libmpeg2-0.5.1"
    cd "$LIBS_FOLDER"
    wget -nc "http://libmpeg2.sourceforge.net/files/libmpeg2-0.5.1.tar.gz"
    tar -xf libmpeg2-0.5.1.tar.gz
    cd "$LIBS_FOLDER/libmpeg2-0.5.1/"
    CFLAGS="-fPIC -Oz" emconfigure ./configure --build=wasm32-unknown-none --prefix="$LIBS_FOLDER/build/" --disable-sdl
    emmake make -j 5
    emmake make install
  fi
  LIBS_FLAGS="${LIBS_FLAGS} --with-mpeg2-prefix=$LIBS_FOLDER/build"
fi

if [ "$_libtheoradec" = true ]; then
  if [[ ! -f "$LIBS_FOLDER/build/lib/libtheora.a" ]]; then
    echo "build libtheora-1.1.1"
    cd "$LIBS_FOLDER"
    wget -nc "https://downloads.xiph.org/releases/theora/libtheora-1.1.1.tar.xz"
    tar -xf libtheora-1.1.1.tar.xz
    cd "$LIBS_FOLDER/libtheora-1.1.1/"
    CFLAGS="-fPIC -s USE_OGG=1 -Oz" emconfigure ./configure --host=wasm32-unknown-none --build=wasm32-unknown-none --prefix="$LIBS_FOLDER/build/" --disable-asm
    emmake make -j 5
    emmake make install
  fi
  LIBS_FLAGS="${LIBS_FLAGS} --with-theoradec-prefix=$LIBS_FOLDER/build"
fi

if [ "$_libvpx" = true ]; then
  if [[ ! -f "$LIBS_FOLDER/build/lib/libvpx.a" ]]; then
    echo "build libvpx-1.15.0"
    cd "$LIBS_FOLDER"
    wget -nc --content-disposition "https://github.com/webmproject/libvpx/archive/refs/tags/v1.15.0.tar.gz"
    tar -xf libvpx-1.15.0.tar.gz
    cd "$LIBS_FOLDER/libvpx-1.15.0/"
    CFLAGS="-fPIC -Oz" emconfigure ./configure --disable-vp8-encoder --disable-vp9-encoder --prefix="$LIBS_FOLDER/build/" 
    emmake make -j 5
    emmake make install
  fi
  LIBS_FLAGS="${LIBS_FLAGS} --with-vpx-prefix=$LIBS_FOLDER/build"
fi

#################################
# Configure
#################################
if [[ "configure" =~ $(echo ^\(${TASKS}\)$) || "build" =~ $(echo ^\(${TASKS}\)$) ]]; then
  cd "${ROOT_FOLDER}"
  echo "Running configure"
  # TODO: Figure out how configure could guess the host
  emconfigure ./configure --host=wasm32-unknown-emscripten --build=wasm32-unknown-emscripten ${CONFIGURE_ARGS} ${LIBS_FLAGS}

  # TODO: configure currently doesn't clean up all files it creates
  rm scummvm-conf.*
fi

#################################
# Make / Compile
#################################
if [[ "make" =~ $(echo ^\(${TASKS}\)$) || "build" =~ $(echo ^\(${TASKS}\)$) ]]; then
  cd "${ROOT_FOLDER}"
  echo "Running make"
  emmake make
fi

# The following steps copy stuff to build-emscripten:
mkdir -p "${ROOT_FOLDER}/build-emscripten/"

#################################
# Bundle everything into a neat package
#################################
if [[ "dist" =~ $(echo ^\(${TASKS}\)$) || "build" =~ $(echo ^\(${TASKS}\)$) ]]; then
  echo "Bundle ScummVM for static file hosting"
  emmake make dist-emscripten
fi

#################################
# Run Development Server
#################################
if [[ "run" =~ $(echo ^\(${TASKS}\)$) ]]; then
  echo "Run ScummVM"
  cd "${ROOT_FOLDER}/build-emscripten/"
  emrun --browser=chrome scummvm.html
fi
