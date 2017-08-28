#-------------------------#
# Created 21. August 2017 #
#-------------------------#

#!/bin/bash

ROOT_DIR=$(pwd)
BUILD_DIR=$ROOT_DIR/engine/build
BIN_DIR=$ROOT_DIR/bin


if [ ! -d "$BUILD_DIR" ]; then
    mkdir $BUILD_DIR
fi

cd $BUILD_DIR

cmake -DCMAKE_BUILD_TYPE=Release $ROOT_DIR/engine
make clean && make -j$(nproc)

cd $ROOT_DIR

if [ ! -d "$BIN_DIR" ]; then
    mkdir $BIN_DIR
fi

cp $BUILD_DIR/{vegastrike,vegaserver,setup/vssetup,objconv/mesh_tool} $BIN_DIR
