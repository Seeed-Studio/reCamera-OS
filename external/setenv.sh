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
rsync_dir ./cvi_mpi
rsync_dir ./oss
rsync_dir ./SensorSupportList/ cvi_mpi/component/isp
rsync_dir ./buildroot-2021.05
rsync_dir ./cnpy
rsync_dir ./cvibuilder
rsync_dir ./cvikernel
rsync_dir ./cvimath
rsync_dir ./cviruntime
rsync_dir ./flatbuffers
rsync_dir ./tdl_sdk
rsync_dir ./cvi_rtsp

###################################
# patch externals
###################################
rsync_dir $EXTERNAL/build .
rsync_dir $EXTERNAL/buildroot/ buildroot*/
rsync_dir $EXTERNAL/isp_tuning .
rsync_dir $EXTERNAL/ramdisk/ ramdisk/
rsync_dir $EXTERNAL/u-boot/ u-boot*/

# notic: sg200x is soft link to cv182x
rsync -av $EXTERNAL/SensorSupportList/sensor/sg200x/ $PROJECT_OUT/cvi_mpi/component/isp/sensor/cv182x/

cp -v $TOPDIR/isp_tuning/copyBin.sh $PROJECT_OUT/isp_tuning/

rsync -av --delete $EXTERNAL/buildroot/package/nodejs/ $PROJECT_OUT/buildroot*/package/nodejs/
rsync -av --delete $EXTERNAL/buildroot/package/swupdate/ $PROJECT_OUT/buildroot*/package/swupdate/

ln -sf $TOPDIR/host-tools $PROJECT_OUT/

if [ ! -e "$PROJECT_OUT/cvi_rtsp/.git" ]; then
ln -s ../../../.git/modules/cvi_rtsp/ $PROJECT_OUT/cvi_rtsp/.git
ls -l $PROJECT_OUT/cvi_rtsp/.git
fi

if [ ! -e "$PROJECT_OUT/cvi_mpi/.git" ]; then
ln -s ../../../.git/modules/cvi_mpi/ $PROJECT_OUT/cvi_mpi/.git
ls -l $PROJECT_OUT/cvi_mpi/.git
fi

cp -fv $PROJECT_DIR/rootfs/etc/issue $PROJECT_OUT/buildroot*/board/cvitek/CV181X/overlay/etc/
cp -fv $TOPDIR/CHANGELOG.md $PROJECT_OUT/buildroot*/board/cvitek/CV181X/overlay/etc/

###################################
# modify build/Makefile
###################################
sed -i 's/${Q}$(MAKE) -C $(BR_DIR).*/${Q}$(MAKE) -C $(BR_DIR) $(BR_DEFCONFIG) BR2_TOOLCHAIN_EXTERNAL_PATH=$(CROSS_COMPILE_PATH) O=$(TARGET_OUTPUT_DIR)/' \
    $PROJECT_OUT/build/Makefile
sed -i 's/${Q}$(MAKE) -j${NPROC} -C $(BR_DIR).*/${Q}$(MAKE) -j${NPROC} -C $(BR_DIR) O=$(TARGET_OUTPUT_DIR)/' \
    $PROJECT_OUT/build/Makefile

sed -i '/EXTRA_LDFLAGS = $(LIBS).*/aEXTRA_LDFLAGS += -latomic' \
    $PROJECT_OUT/cvi_mpi/sample/venc/Makefile

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
sed -i 's/function pack_cfg/function _pack_cfg_/g' $PROJECT_OUT/build/common_functions.sh

# build flatbuffers
sed -i 's/cmake -G Ninja -DFLATBUFFERS_PATH=$FLATBUFFERS_HOST_PATH/cmake -G Ninja -DFLATBUFFERS_BUILD_TESTS=OFF -DFLATBUFFERS_PATH=$FLATBUFFERS_HOST_PATH/g' $PROJECT_OUT/cviruntime/build_tpu_sdk.sh

# move libcvi_rtsp.so to /mnt/system/lib
echo 'install(FILES ${CVI_RTSP_LIBPATH} DESTINATION ${CMAKE_INSTALL_PREFIX}/lib)' >> $PROJECT_OUT/tdl_sdk/cmake/cvi_rtsp.cmake

# source cvisetup.sh
TPU_REL=1
source $PROJECT_OUT/build/cvisetup.sh

###################################
# overwrite cvisetup.sh functions
###################################
function build_middleware()
{(
    print_notice "Run ${FUNCNAME[0]}() overided by $0"

    _build_middleware_ || return $?

    pushd "$MW_PATH"
    cp -f sample/audio/sample_audio*  ${SYSTEM_OUT_DIR}/usr/bin
    popd
)}

function pack_cfg
{(
    print_notice "Run ${FUNCNAME[0]}() overided by $0"

    _dir="$OUTPUT_DIR/rootfs/mnt/cfg/param"
    mkdir -p $_dir

    pushd "$ISP_TUNING_PATH"
    ./copyBin.sh $_dir "$SENSOR_TUNING_PARAM"
    popd

    mkdir -p $BR_OVERLAY_DIR/mnt
    cp -arf $OUTPUT_DIR/rootfs/mnt/cfg $BR_OVERLAY_DIR/mnt/
)}