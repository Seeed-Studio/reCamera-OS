#!/bin/bash

export TARGET=$1
if [ -z "$TARGET" ]; then
    echo "Usage:  <project>"
    exit 1
fi
export PROJECT_OUT=$OUTDIR/$TARGET
export PROJECT_DIR=$(dirname $(realpath $DEFCONFIGS/${1}_defconfig))

export BR2_EXTERNAL="$EXTERNAL/br2-external"
export BUILDROOT_DIR=$(basename $(realpath $TOPDIR/buildroot*))
export UBOOT_DIR=$(basename $(realpath $TOPDIR/u-boot*))
export LINUX_DIR=$(basename $(realpath $TOPDIR/linux*))

CHANGELOG=$TOPDIR/CHANGELOG.md

source $EXTERNAL/setenv.sh || exit 1
defconfig ${TARGET} || exit 1

# copy project rootfs to buildroot overlay
rsync -av $PROJECT_DIR/rootfs/ $BR_OVERLAY_DIR

md5file=${TARGET}_md5sum.txt
ISSUE_FILE=$PROJECT_DIR/rootfs/etc/issue
if [ -f $ISSUE_FILE ]; then
    issue=$(cat $ISSUE_FILE)
fi
if [ -z "$issue" ]; then
    target_name="${1}"
else
    target_name=${CHIP}

    version_name=$issue
    target_name=${target_name}_${version_name}

    # TODO: get version from CHANGELOG.md base on project name
    if [ -f $CHANGELOG ]; then
        verison_num=$(awk -F' ' 'BEGIN {f=0} /^## .*/ && !f {print $2; f=1; exit}' $CHANGELOG)
        echo "$version_name $verison_num" >$BR_OVERLAY_DIR/etc/issue
        cp -fv $CHANGELOG $BR_OVERLAY_DIR/etc/
        target_name=${target_name}_${verison_num}
    fi

    target_name=${target_name}_${STORAGE_TYPE}
fi

export LIVE555_DIR=${TPU_SDK_INSTALL_PATH}
build_all || exit 1

##################################################
# gen packages
echo "Target name: ${target_name}"
echo "Output directory: ${OUTPUT_DIR}"

function gen_emmc_zip() {
    echo "Run ${FUNCNAME[0]}"

    pushd $OUTPUT_DIR
    if [ -f upgrade.zip ]; then
        rm -rf ${1}.zip
        cp -fv upgrade.zip ${1}.zip || exit 1
    fi
    popd

    echo "${FUNCNAME[0]} ok"
}

function gen_rawimages_zip() {
    echo "Run ${FUNCNAME[0]}"

    pushd $OUTPUT_DIR/rawimages
    rm -rfv ../*rawimages.zip
    cp -fv ../fip.bin . || exit 1
    md5sum fip.bin boot.emmc rootfs_ext4.emmc >md5sum.txt
    zip -j rawimages.zip fip.bin boot.emmc rootfs_ext4.emmc md5sum.txt || exit 1
    rm -rf fip.bin
    mv -fv rawimages.zip ../${1}.zip
    popd

    echo "${FUNCNAME[0]} ok"
}

function gen_sd_recovery_zip() {
    echo "Run ${FUNCNAME[0]}"

    echo PROJECT_OUT=$PROJECT_OUT
    pushd $PROJECT_OUT
    ./build/tools/common/sd_tools/sd_gen_recovery_image.sh ${OUTPUT_DIR} ${1}
    popd

    echo "${FUNCNAME[0]} ok"
}

function gen_sd_zip() {
    echo "Run ${FUNCNAME[0]}"

    echo PROJECT_OUT=$PROJECT_OUT
    pushd $PROJECT_OUT
    ./build/tools/common/sd_tools/sd_gen_burn_image.sh ${OUTPUT_DIR} ${1}
    popd

    echo "${FUNCNAME[0]} ok"
}

function gen_swu() {
    echo "Run ${FUNCNAME[0]}"

    pushd $OUTPUT_DIR
    rm -rfv *.swu
    cd rawimages/
    cp ${PROJECT_OUT}/build/tools/common/sw-description .
    FILES="sw-description rootfs_ext4.emmc"
    for i in $FILES; do
        echo $i
    done | cpio -ov -H crc >${1}.swu
    mv -fv *.swu ../
    cd ..
    zip -j ${1}_swu.zip *.swu || exit 1
    popd

    echo "${FUNCNAME[0]} ok"
}

function check_zip() {
    file=$1
    if [ -f ${file} ]; then
        md5sum ${file} >>${2}
    else
        echo "Gen ${file} failed!"
        exit 1
    fi
}

function gen_md5sum() {
    echo "Run ${FUNCNAME[0]}"

    pushd $OUTPUT_DIR/ >/dev/null 2>&1
    rm -rf $md5file

    LIST=$(find . -maxdepth 1 -name "${target_name}*.zip")
    while IFS= read -r file; do
        file=$(basename $file)
        check_zip $file $md5file
    done <<<"$LIST"

    echo "Success"
    popd >/dev/null 2>&1
}

function gen_sdk() {
    echo "Run ${FUNCNAME[0]}"

    pushd $OUTDIR
    rm -rf ${1}.tar.gz
    SDK_LIST=(
        "${TARGET}/buildroot-2021.05/output/cvitek_CV181X_musl_riscv64/host/riscv64-buildroot-linux-musl/sysroot/usr/include"
        "${TARGET}/buildroot-2021.05/output/cvitek_CV181X_musl_riscv64/host/riscv64-buildroot-linux-musl/sysroot/usr/lib"
        "${TARGET}/cvi_mpi/include"
        "${TARGET}/cvi_mpi/lib"
        "${TARGET}/cvi_mpi/modules"
        "${TARGET}/osdrv/interdrv"
        "${TARGET}/cvi_rtsp/install"
        "${TARGET}/install/soc_${TARGET}/rootfs/mnt/system/lib"
        "${TARGET}/install/soc_${TARGET}/tpu_musl_riscv64/cvitek_tpu_sdk/include"
        "${TARGET}/install/soc_${TARGET}/tpu_musl_riscv64/cvitek_tpu_sdk/lib"
        "${TARGET}/install/soc_${TARGET}/tpu_musl_riscv64/cvitek_tpu_sdk/opencv"
    )
    MISSING=0
    for p in "${SDK_LIST[@]}"; do
        if [ ! -e "$p" ]; then
            echo "[gen_sdk] Missing path: $p" >&2
            MISSING=1
        fi
    done
    if [ $MISSING -eq 1 ]; then
        echo "[gen_sdk] Abort: one or more required paths are missing." >&2
        popd
        return 2
    fi
    # Use --warning=no-file-changed to reduce noise; build deterministic list file
    printf "%s\n" "${SDK_LIST[@]}" >.sdk_filelist.txt
    tar -czvf ${1}.tar.gz -T .sdk_filelist.txt || {
        echo "[gen_sdk] tar failed" >&2
        popd
        return 3
    }
    rm -f .sdk_filelist.txt
    mv -fv ${1}.tar.gz "$OUTPUT_DIR" || {
        echo "[gen_sdk] move failed" >&2
        popd
        return 4
    }
    popd
}

if [ $STORAGE_TYPE = "emmc" ]; then
    gen_emmc_zip ${target_name} || exit 1
    gen_rawimages_zip ${target_name}_ota || exit 1
    gen_sd_recovery_zip ${target_name}_recovery || exit 1
    gen_sd_zip ${target_name}_sd_compat || exit 1
    gen_swu ${target_name} || exit 1
else
    gen_sd_zip ${target_name} || exit 1
fi

gen_sdk ${target_name}_sdk || exit 1
gen_md5sum || exit 1
