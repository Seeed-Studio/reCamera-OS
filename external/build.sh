#!/bin/bash


PROJECT_OUT=$OUTDIR/$1
export PROJECT_OUT

PROJECT_DIR=$(dirname $(realpath $DEFCONFIGS/${1}_defconfig))
export PROJECT_DIR

source $EXTERNAL/setenv.sh
defconfig $1
build_all || exit 1
# build_middleware
# exit 0

##################################################
# gen packages
issue=$(cat $PROJECT_DIR/rootfs/etc/issue)
version_name=$(echo $issue | awk '{print $1}')
version_num=$(echo $issue | awk '{print $2}')
target_name=${version_name}_${version_num}
echo "Target name: ${target_name}"

function gen_emmc_zip() {
    echo "Run ${FUNCNAME[0]}"

    pushd $OUTPUT_DIR
    if [ -f upgrade.zip ]; then
        rm -rf ${target_name}_emmc.zip
        mv -fv upgrade.zip ${target_name}_emmc.zip || exit 1
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
    mv -fv rawimages.zip ../${target_name}_rawimages.zip
    popd

    echo "${FUNCNAME[0]} ok"
}

function gen_sd_zip() {
    echo "Run ${FUNCNAME[0]}"

    echo PROJECT_OUT=$PROJECT_OUT
    pushd $PROJECT_OUT
    ./build/tools/common/sd_tools/sd_gen_burn_image.sh ${OUTPUT_DIR} ${target_name}_sd
    popd

    echo "${FUNCNAME[0]} ok"
}

function check_zip() {
    file=$1
    if [ -f ${file} ]; then
        md5sum ${file} > ${md5file}
    else
        echo "Gen ${file} failed!"
        exit 1
    fi
}

function gen_md5sum() {
    echo "Run ${FUNCNAME[0]}"

    pushd $OUTPUT_DIR/ > /dev/null 2>&1
    md5file=${target_name}.md5

    check_zip ${target_name}_emmc.zip
    check_zip ${target_name}_rawimages.zip
    check_zip ${target_name}_sd.zip

    echo "Success"
    popd > /dev/null 2>&1
}

gen_emmc_zip || exit 1
gen_rawimages_zip || exit 1
gen_sd_zip || exit 1
gen_md5sum || exit 1
