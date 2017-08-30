#!/bin/bash

#---------------------- INFO -------------------------------------------#
#									#
# commit: 	b2e46cd619c2589a99080aa12bb5f1209d24c61c		#
# date:		21.08.17						#
# author:	Anth0rx							#
#									#
#---------------------- DESCRIPTION ------------------------------------#
#									#
# This scipt is making a clean build of VegaStrike-taose.		#
# After this is copies the relevant files to the 'bin' directory.	#
#									#
# The steps for creating this script were taken from the projects wiki:	#
# https://github.com/Taose/Vegastrike-taose/wiki/How-to-Build		#
#									#
#-----------------------------------------------------------------------#


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
