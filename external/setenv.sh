#!/bin/bash

function rsync_dir()
{
    if [ ! -d $1 ]; then
        echo "$1 not exist"
        return
    fi
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
# rsync_dir ./isp_tuning
rsync_dir ./oss
rsync_dir ./SensorSupportList/ middleware/v2/component/isp/
rsync_dir ./buildroot-2021.05

###################################
# patch externals
###################################
rsync_dir $EXTERNAL/build .
rsync_dir $EXTERNAL/buildroot/ buildroot*/
rsync_dir $EXTERNAL/isp_tuning .
rsync_dir $EXTERNAL/ramdisk/ ramdisk/
rsync_dir $EXTERNAL/u-boot/ u-boot*/

rsync -av --delete $EXTERNAL/buildroot/package/nodejs/ $PROJECT_OUT/buildroot*/package/nodejs/

# copy project-specific files
rsync_dir $PROJECT_DIR/rootfs/ buildroot*/board/cvitek/CV181X/overlay/

# patches=`find $EXTERNAL/patches/ -name "*.patch" | sort`
# for patch in ${patches}; do
#     echo "patch -p1 -s -f -N -d \"${PROJECT_OUT}/buildroot-2021.05/\" < ${patch}" ; \
#         patch -p1 -s -f -N -d "${PROJECT_OUT}/buildroot-2021.05/" < ${patch}
# done

ln -sf $TOPDIR/host-tools $PROJECT_OUT/

###################################
# modify build/Makefile
###################################
sed -i 's/${Q}$(MAKE) -C $(BR_DIR).*/${Q}$(MAKE) -C $(BR_DIR) $(BR_DEFCONFIG) BR2_TOOLCHAIN_EXTERNAL_PATH=$(CROSS_COMPILE_PATH) O=$(TARGET_OUTPUT_DIR)/' \
    $PROJECT_OUT/build/Makefile
sed -i 's/${Q}$(MAKE) -j${NPROC} -C $(BR_DIR).*/${Q}$(MAKE) -j${NPROC} -C $(BR_DIR) O=$(TARGET_OUTPUT_DIR)/' \
    $PROJECT_OUT/build/Makefile

###################################
# modify cvisetup.sh
###################################
sed -i 's/^TOP_DIR=$(.*)/TOP_DIR=$PROJECT_OUT/' $PROJECT_OUT/build/cvisetup.sh
echo "INFO: Change TOP_DIR to \"$PROJECT_OUT in cvisetup.sh"\"

sed -i 's/CVI_TARGET_PACKAGES_LIBDIR=.*/CVI_TARGET_PACKAGES_LIBDIR=$(make --no-print-directory print-target-packages-libdir)/' \
    $PROJECT_OUT/build/cvisetup.sh
sed -i 's/CVI_TARGET_PACKAGES_INCLUDE=.*/CVI_TARGET_PACKAGES_INCLUDE=$(make --no-print-directory print-target-packages-include)/' \
    $PROJECT_OUT/build/cvisetup.sh
echo "INFO: Fixed CVI_TARGET_PACKAGES_LIBDIR & CVI_TARGET_PACKAGES_INCLUDE in cvisetup.sh"\"

# overide build_middleware function
sed -i 's/function build_middleware()/function _build_middleware_()/g' $PROJECT_OUT/build/cvisetup.sh

# source cvisetup.sh
source $PROJECT_OUT/build/cvisetup.sh

###################################
# overwrite cvisetup.sh functions
###################################
function build_middleware()
{(
    print_notice "Run ${FUNCNAME[0]}() overided by $0"

    _build_middleware_

    pushd "$MW_PATH"
    cp -f sample/audio/sample_audio*  ${SYSTEM_OUT_DIR}/usr/bin
    popd
)}