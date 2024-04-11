#!/bin/bash

mkdir -p $PROJECT_OUT
rsync -a --exclude='.git' ./build $PROJECT_OUT/
rsync -a --exclude='.git' ./freertos $PROJECT_OUT/
rsync -a --exclude='.git' ./fsbl $PROJECT_OUT/
rsync -a --exclude='.git' ./isp_tuning $PROJECT_OUT/
rsync -a --exclude='.git' ./linux_5.10 $PROJECT_OUT/
rsync -a --exclude='.git' ./middleware $PROJECT_OUT/
rsync -a --exclude='.git' ./opensbi $PROJECT_OUT/
rsync -a --exclude='.git' ./osdrv $PROJECT_OUT/
rsync -a --exclude='.git' ./ramdisk $PROJECT_OUT/
rsync -a --exclude='.git' ./u-boot-2021.10 $PROJECT_OUT/
rsync -a --exclude='.git' ./buildroot-2021.05 $PROJECT_OUT/

###################################
# patch externals
###################################
rsync -a $EXTERNAL/build $PROJECT_OUT/
rsync -a $EXTERNAL/fsbl $PROJECT_OUT/


###################################
# modify cvisetup.sh
###################################
sed -i 's/TOOLCHAIN_PATH=.*/TOOLCHAIN_PATH="\$TOPDIR"\/host-tools/' $PROJECT_OUT/build/cvisetup.sh
echo "INFO: Change TOOLCHAIN_PATH to \"$TOPDIR/host-tools in cvisetup.sh"\"
sed -i 's/^TOP_DIR=\$\(.*\)/TOP_DIR=\$PROJECT_OUT/' $PROJECT_OUT/build/cvisetup.sh
echo "INFO: Change TOP_DIR to \"$PROJECT_OUT in cvisetup.sh"\"

source $PROJECT_OUT/build/cvisetup.sh

###################################
# overwrite cvisetup.sh functions
###################################

