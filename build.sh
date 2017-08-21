#!/bin/bash

ROOT_DIR=$(pwd)
BUILD_DIR=$ROOT_DIR/engine/build
BIN_DIR=$ROOT_DIR/bin


if [ ! -d "$BUILD_DIR" ]; then
    mkdir $BUILD_DIR
fi

cd $BUILD_DIR

cmake $ROOT_DIR/engine
make -j$(nproc)

cd $ROOT_DIR
mkdir $BIN_DIR
cp $BUILD_DIR/{vegastrike,vegaserver,setup/vssetup,objconv/mesh_tool} $BIN_DIR
