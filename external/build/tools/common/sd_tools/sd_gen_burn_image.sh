#!/bin/bash
# a sd image generator for sophpi
# usage
if [ "$#" -ne "2" ]
then
	echo "usage: sudo ./sd_gen_burn_image.sh OUTPUT_DIR IMAGE_NAME"
	echo ""
	echo "       The script is used to create a sdcard image with two partitions, "
	echo "       one is fat32 with 128MB, the other is ext4 with 256MB."
	echo "       You can modify the capacities in this script as you wish!"
	echo ""
	echo "Note:  Please backup you sdcard files before using this image!"

	exit
fi

boot_cap=8M
boot_label="BOOT"
bootenv_cap=128K
rootfs_cap=256M
rootfs_label="ROOTFS"

function size2sectors() {
    local f=0
    for v in "${@}"
    do
    local p=$(echo "$v" | awk \
      'BEGIN{IGNORECASE = 1}
       function printsectors(n,b,p) {printf "%u\n", n*b^p/512}
       /B$/{     printsectors($1,  1, 0)};
       /K(iB)?$/{printsectors($1,  2, 10)};
       /M(iB)?$/{printsectors($1,  2, 20)};
       /G(iB)?$/{printsectors($1,  2, 30)};
       /T(iB)?$/{printsectors($1,  2, 40)};
       /KB$/{    printsectors($1, 10,  3)};
       /MB$/{    printsectors($1, 10,  6)};
       /GB$/{    printsectors($1, 10,  9)};
       /TB$/{    printsectors($1, 10, 12)}')
    for s in $p
    do
        f=$((f+s))
    done

    done
    echo $f
}

boot_start="1"
boot_size=$(size2sectors ${boot_cap})
env_start=$((boot_start+boot_size))
env_size=$(size2sectors ${bootenv_cap})
rootfs_start=$((env_start+env_size))
rootfs_size=$(size2sectors ${rootfs_cap})

function create_disk_mbr() {
    echo "Run ${FUNCNAME[0]}"
    if [ -z "${1}" ]; then
        echo "image name is empty"
        exit 1
    fi

    image=$1
    local img_size=$((boot_size+env_size+rootfs_size))
    dd if=/dev/zero of=./${image} bs=512 count=${img_size}

    # Create the disk image
    (
        echo "label: dos"
        echo "label-id: 0x48617373"
        echo "unit: sectors"
        echo "boot  : start= ${boot_start},     size= ${boot_size},     type=c, bootable"   #create the boot partition
        echo "env   : start= ${env_start},      size= ${env_size},      type=5"             #Make an env partition
        echo "rootfs: start= ${rootfs_start},   size= ${rootfs_size},   type=83"            #Make a rootfs partition
    ) | sfdisk --force -uS ${image}

    echo "${FUNCNAME[0]} ok"
}

function write_boot_part() {
    echo "Run ${FUNCNAME[0]}"
    if [ -z $1 ]; then
        echo "image name is empty"
        exit 1
    fi

    local part=$(mktemp)

    ls -l ${part}
    dd if=/dev/zero of=${part} bs=512 count=${boot_size}
    mkfs.vfat -n ${boot_label} ${part}

    mcopy -i ${part} fip.bin ::
    mcopy -i ${part} rawimages/boot.* ::

    dd if=${part} of=${1} seek=0 bs=512 count=${boot_size} conv=notrunc,sparse

    rm -rf ${part}
    echo "${FUNCNAME[0]} ok"
}

function write_rootfs_part() {
    echo "Run ${FUNCNAME[0]}"
    if [ -z $1 ]; then
        echo "image name is empty"
        exit 1
    fi

    dd if=rawimages/rootfs_ext4.emmc of=${1} seek=$((boot_size+env_size)) bs=512 conv=notrunc,sparse

    echo "${FUNCNAME[0]} ok"
}

# Start gen image
pushd $1 || exit 1
# target=$2_`date +%Y%m%d%H%M`
target=$2
image_name=${target}.img
echo "Image: ${image_name}"
rm -rf ${target}*
create_disk_mbr $image_name || exit 1
write_boot_part $image_name || exit 1
write_rootfs_part $image_name || exit 1

zip -j ${target}.zip ${image_name}
rm -rf ${image_name}
popd