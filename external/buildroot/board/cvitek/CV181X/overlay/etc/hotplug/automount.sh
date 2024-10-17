#!/bin/sh

function umount_dir() {
  if [ -d $1 ]; then
      umount $1
      rm -rf $1
  fi
}

MOUNT_POINT=/tmp/sd
LINK_POINT=/mnt/sd

# echo "$1 $2"
if [ "$1" == "mmcblk1p1" ]; then
  if [ "$2" = "add" ]; then
      #dir=$(mktemp -d)
      dir=$MOUNT_POINT
      umount_dir $dir
      mkdir -p $dir
      # chown -R recamera:recamera $dir
      mount $1 $dir

      rootfs_rw on
      if [ -L $LINK_POINT ]; then
        rm $LINK_POINT
      fi
      ln -s $dir /mnt/
      rootfs_rw off
  else
      dir=$(realpath $LINK_POINT)
      umount_dir $dir
      rootfs_rw on
      rm $LINK_POINT
      rootfs_rw off
  fi
fi