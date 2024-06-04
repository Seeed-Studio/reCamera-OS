#!/bin/bash


PROJECT_OUT=$OUTDIR/$1
export PROJECT_OUT

PROJECT_DIR=$(dirname $(realpath $DEFCONFIGS/${1}_defconfig))
export PROJECT_DIR

source $EXTERNAL/setenv.sh
defconfig $1
build_all

# Pack rawimages.zip
pushd $OUTPUT_DIR/rawimages
cp -fv ../fip.bin . || exit 1
md5sum fip.bin boot.emmc rootfs_ext4.emmc > md5sum.txt || exit 1
zip -j rawimages.zip fip.bin boot.emmc rootfs_ext4.emmc md5sum.txt || exit 1
rm -rf fip.bin
mv -fv rawimages.zip ../
sync
popd

if [ $STORAGE_TYPE = "emmc" ]; then
echo "INFO: \$STORAGE_TYPE is "emmc", skip gen image."
elif [ -n "$SUDO_PWD" ]; then
echo "INFO: \$STORAGE_TYPE is "sd", run sd_gen_burn_image.sh."
pushd $PROJECT_OUT
echo $SUDO_PWD | sudo -S ./build/tools/common/sd_tools/sd_gen_burn_image.sh $(realpath install/soc_$1) $1
popd
fi