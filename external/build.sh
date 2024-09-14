#!/bin/bash

export PROJECT_OUT=$OUTDIR/$1
export PROJECT_DIR=$(dirname $(realpath $DEFCONFIGS/${1}_defconfig))

export BR2_EXTERNAL="$EXTERNAL/br2-external"
export BUILDROOT_DIR=$(basename $(realpath $TOPDIR/buildroot*))
export UBOOT_DIR=$(basename $(realpath $TOPDIR/u-boot*))
export LINUX_DIR=$(basename $(realpath $TOPDIR/linux*))

CHANGELOG=$TOPDIR/CHANGELOG.md

source $EXTERNAL/setenv.sh
defconfig $1

md5file=${1}_md5sum.txt
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
        echo "$version_name $verison_num" > $BR_OVERLAY_DIR/etc/issue
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
    md5sum fip.bin boot.emmc rootfs_ext4.emmc > md5sum.txt
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
    
    echo OUTPUT_DIR=$OUTPUT_DIR
    pushd $OUTPUT_DIR
    rm -rfv *.swu
    cd rawimages/
    cp ${PROJECT_OUT}/build/tools/common/sw-description .
    FILES="sw-description rootfs_ext4.emmc"
    for i in $FILES; do
        echo $i;
    done | cpio -ov -H crc > ${1}.swu
    mv -fv *.swu ../
    cd ..
    zip -j ${1}_swu.zip *.swu || exit 1
    popd

    echo "${FUNCNAME[0]} ok"
}

function check_zip() {
    file=$1
    if [ -f ${file} ]; then
        md5sum ${file} >> ${2}
    else
        echo "Gen ${file} failed!"
        exit 1
    fi
}

function gen_md5sum() {
    echo "Run ${FUNCNAME[0]}"

    pushd $OUTPUT_DIR/ > /dev/null 2>&1
    rm -rf $md5file

    LIST=$(find . -maxdepth 1 -name "${target_name}*.zip")
    while IFS= read -r file; do
        file=$(basename $file)
        check_zip $file $md5file
    done <<< "$LIST"

    echo "Success"
    popd > /dev/null 2>&1
}

if [ $STORAGE_TYPE = "emmc" ]; then
    gen_emmc_zip ${target_name} || exit 1
    gen_rawimages_zip ${target_name}_ota || exit 1
    gen_sd_recovery_zip ${target_name}_recovery || exit 1
    gen_sd_zip ${target_name}_sd_compat || exit 1
    gen_swu ${target_name} || exit 1

    gen_md5sum || exit 1
else
    gen_sd_zip ${target_name} || exit 1

    gen_md5sum || exit 1
fi