#!/bin/sh

MOUNT_POINT=/tmp/sd
LINK_POINT=/mnt/sd

# echo "<<<<<<<<<<<<<$1 $2>>>>>>>>>>>>>"
if [ "$1" == "mmcblk1p1" ]; then
  disk="/dev/$1"
  if [ "$2" = "add" ]; then
      mkdir -p $MOUNT_POINT
      umount $MOUNT_POINT
      mount $disk $MOUNT_POINT

      rootfs_rw on
      if [ -L $LINK_POINT ]; then
        rm $LINK_POINT
      fi
      ln -s $MOUNT_POINT /mnt/
      rootfs_rw off
  else
      umount $MOUNT_POINT
      rootfs_rw on
      rm $LINK_POINT
      rootfs_rw off
  fi
fi