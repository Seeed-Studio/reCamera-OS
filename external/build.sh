#!/bin/bash


PROJECT_OUT=$OUTDIR/$1
export PROJECT_OUT


source $EXTERNAL/setenv.sh
defconfig $1
build_all

if [ $STORAGE_TYPE = "emmc" ]; then
echo "INFO: \$STORAGE_TYPE is "emmc", skip gen image."
elif [ -n "$SUDO_PWD" ]; then
echo "INFO: \$STORAGE_TYPE is "sd", run sd_gen_burn_image.sh."
pushd $PROJECT_OUT
echo $SUDO_PWD | sudo -S ./build/tools/common/sd_tools/sd_gen_burn_image.sh $(realpath install/soc_$1) $1
popd
fi