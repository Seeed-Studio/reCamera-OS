#!/bin/bash


PROJECT_OUT=$OUTDIR/$1
export PROJECT_OUT

PROJECT_DIR=$(dirname $(realpath $DEFCONFIGS/${1}_defconfig))
export PROJECT_DIR

source $EXTERNAL/setenv.sh
defconfig $1
build_all || exit 1

##################################################
# gen packages
issue=$(cat $PROJECT_DIR/rootfs/etc/issue)
if [ -z "$issue" ]; then
target_name="${1%"_${STORAGE_TYPE}"}"
md5file=${target_name}_md5.txt
else
version_name=$(echo $issue | awk '{print $1}')
version_num=$(echo $issue | awk '{print $2}')
target_name=${CHIP}_${version_name}_${version_num}
md5file=${CHIP}_${version_name}_md5.txt
fi
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
    check_zip ${target_name}_emmc.zip $md5file
    check_zip ${target_name}_ota.zip $md5file
    check_zip ${target_name}_recovery.zip $md5file
    check_zip ${target_name}_sd.zip $md5file

    echo "Success"
    popd > /dev/null 2>&1
}

if [ $STORAGE_TYPE = "emmc" ]; then
    gen_emmc_zip ${target_name}_emmc || exit 1
    gen_rawimages_zip ${target_name}_ota || exit 1
    gen_sd_recovery_zip ${target_name}_recovery || exit 1
    gen_sd_zip ${target_name}_sd || exit 1
    gen_md5sum || exit 1
else
    gen_sd_zip ${target_name}_sd || exit 1

    pushd $OUTPUT_DIR/ > /dev/null 2>&1
    echo target_name: ${target_name}

    rm -rf $md5file
    check_zip ${target_name}_sd.zip $md5file
    
    echo "Success"
    popd > /dev/null 2>&1
fi