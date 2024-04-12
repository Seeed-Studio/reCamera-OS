#!/bin/bash

PROJECT_OUT=$OUTDIR/$1

export PROJECT_OUT

source $EXTERNAL/setenv.sh

defconfig $1

# build_uboot
# echo build_uboot $?

build_kernel
echo $?

build_osdrv
echo $?
