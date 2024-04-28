#!/bin/bash


PROJECT_OUT=$OUTDIR/$1
export PROJECT_OUT


source $EXTERNAL/setenv.sh
defconfig $1
build_all

if [ -n "$SUDO_PWD" ]; then
pushd $PROJECT_OUT
echo $SUDO_PWD | sudo -S ./build/tools/common/sd_tools/sd_gen_burn_image.sh $(realpath install/soc_$1) $1
popd
fi