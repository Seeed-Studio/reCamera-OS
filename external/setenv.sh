#!/bin/bash

function rsync_dir()
{
    mkdir -p $PROJECT_OUT/$2
    echo "rsync $1 -> $PROJECT_OUT/$2"; rsync -a --exclude='.git' $1 $PROJECT_OUT/$2 || exit 1
}

###################################
# rsync codes
###################################
mkdir -p $PROJECT_OUT
rsync_dir ./build
rsync_dir ./freertos
rsync_dir ./FreeRTOS-Kernel/ freertos/Source/
rsync_dir ./Lab-Project-FreeRTOS-POSIX/ freertos/Source/FreeRTOS-Plus-POSIX/
rsync_dir ./fsbl
rsync_dir ./opensbi
rsync_dir ./u-boot-2021.10
rsync_dir ./linux_5.10
rsync_dir ./ramdisk
rsync_dir ./osdrv
rsync_dir ./middleware
rsync_dir ./isp_tuning
rsync_dir ./oss
rsync_dir ./SensorSupportList/ middleware/v2/component/isp/
rsync_dir ./buildroot-2021.05

###################################
# patch externals
###################################
rsync_dir $EXTERNAL/build .
# rsync_dir $EXTERNAL/rootfs_overlay/ buildroot-2021.05/board/cvitek/CV181X/overlay/

# patches=`find $EXTERNAL/patches/ -name "*.patch" | sort`
# for patch in ${patches}; do
#     echo "patch -p1 -s -f -N -d \"${PROJECT_OUT}/buildroot-2021.05/\" < ${patch}" ; \
#         patch -p1 -s -f -N -d "${PROJECT_OUT}/buildroot-2021.05/" < ${patch}
# done

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

