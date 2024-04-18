#!/bin/bash

###################################
# rsync codes
###################################
mkdir -p $PROJECT_OUT
echo "rsync ./build"; rsync -a --exclude='.git' ./build $PROJECT_OUT/
echo "rsync ./freertos"; rsync -a --exclude='.git' ./freertos $PROJECT_OUT/
echo "rsync ./fsbl"; rsync -a --exclude='.git' ./fsbl $PROJECT_OUT/
echo "rsync ./isp_tuning"; rsync -a --exclude='.git' ./isp_tuning $PROJECT_OUT/
echo "rsync ./linux_5.10"; rsync -a --exclude='.git' ./linux_5.10 $PROJECT_OUT/
echo "rsync ./middleware"; rsync -a --exclude='.git' ./middleware $PROJECT_OUT/
echo "rsync ./opensbi"; rsync -a --exclude='.git' ./opensbi $PROJECT_OUT/
echo "rsync ./osdrv"; rsync -a --exclude='.git' ./osdrv $PROJECT_OUT/
echo "rsync ./ramdisk"; rsync -a --exclude='.git' ./ramdisk $PROJECT_OUT/
echo "rsync ./u-boot-2021.10"; rsync -a --exclude='.git' ./u-boot-2021.10 $PROJECT_OUT/
echo "rsync ./buildroot-2021.05"; rsync -a --exclude='.git' ./buildroot-2021.05 $PROJECT_OUT/
echo "rsync ./SensorSupportList"; rsync -a --exclude='.git' ./SensorSupportList/ $PROJECT_OUT/middleware/v2/component/isp/

###################################
# patch externals
###################################
echo "rsync $EXTERNAL/build"; rsync -av $EXTERNAL/build $PROJECT_OUT/
echo "rsync $EXTERNAL/fsbl"; rsync -av $EXTERNAL/fsbl $PROJECT_OUT/

patches=`find $EXTERNAL/patches/ -name "*.patch" | sort`
for patch in ${patches}; do
    echo "patch -p1 -s -f -N -d \"${PROJECT_OUT}/buildroot-2021.05/\" < ${patch}" ; \
        patch -p1 -s -f -N -d "${PROJECT_OUT}/buildroot-2021.05/" < ${patch}
done

ln -sf $TOPDIR/host-tools $PROJECT_OUT/

###################################
# modify cvisetup.sh
###################################
# sed -i 's/TOOLCHAIN_PATH=.*/TOOLCHAIN_PATH="\$TOPDIR"\/host-tools/' $PROJECT_OUT/build/cvisetup.sh
# echo "INFO: Change TOOLCHAIN_PATH to \"$TOPDIR/host-tools in cvisetup.sh"\"

sed -i 's/^TOP_DIR=\$\(.*\)/TOP_DIR=\$PROJECT_OUT/' $PROJECT_OUT/build/cvisetup.sh
echo "INFO: Change TOP_DIR to \"$PROJECT_OUT in cvisetup.sh"\"

sed -i 's/CVI_TARGET_PACKAGES_LIBDIR=.*/CVI_TARGET_PACKAGES_LIBDIR=$(make --no-print-directory print-target-packages-libdir)/' \
    $PROJECT_OUT/build/cvisetup.sh
sed -i 's/CVI_TARGET_PACKAGES_INCLUDE=.*/CVI_TARGET_PACKAGES_INCLUDE=$(make --no-print-directory print-target-packages-include)/' \
    $PROJECT_OUT/build/cvisetup.sh
echo "INFO: Fixed CVI_TARGET_PACKAGES_LIBDIR & CVI_TARGET_PACKAGES_INCLUDE in cvisetup.sh"\"

source $PROJECT_OUT/build/cvisetup.sh

###################################
# overwrite cvisetup.sh functions
###################################

