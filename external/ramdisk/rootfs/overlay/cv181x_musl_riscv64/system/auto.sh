#!/bin/sh
${CVI_SHOPTS}

export LD_LIBRARY_PATH="/lib:/lib/3rd:/lib/arm-linux-gnueabihf:/usr/lib:/usr/local/lib:/mnt/system/lib:/mnt/system/usr/lib:/mnt/system/usr/lib/3rd:/mnt/data/lib"
export PATH="/usr/local/bin:/usr/bin:/bin:/usr/local/sbin:/usr/sbin:/sbin:/mnt/system/usr/bin:/mnt/system/usr/sbin:/mnt/data/bin:/mnt/data/sbin"

# boot success, clean boot_cnt
if [ $(fw_printenv boot_cnt) != "boot_cnt=0" ]; then
    fw_setenv boot_cnt 0;
fi

# mount userdata partition
USERDATA_PARTITION="/dev/mmcblk0p6"
USERDATA_MOUNTPOINT="/userdata"
fs_type=$(blkid -o value -s TYPE $USERDATA_PARTITION)
echo "userdata partition type: $fs_type"
if [ "$fs_type" == "" ]; then
   mkfs.ext4 $USERDATA_PARTITION
fi
fs_type=$(blkid -o value -s TYPE $USERDATA_PARTITION)
if [ "$fs_type" != "" ]; then
   if [ ! -d $USERDATA_MOUNTPOINT ]; then
      mkdir -p $USERDATA_MOUNTPOINT
   fi
   mount $USERDATA_PARTITION $USERDATA_MOUNTPOINT
   result=$(mount | grep $USERDATA_MOUNTPOINT)
   if [ "$result" == "" ]; then
      echo "mount $USERDATA_PARTITION to $USERDATA_MOUNTPOINT failed"
   fi
fi

# default app
DEFAULT_APP=/mnt/system/default_app
if [ ! -f "/tmp/evb_init" ];then
   echo 1 > /tmp/evb_init
   if [ -x $DEFAULT_APP ]; then
      $DEFAULT_APP > /dev/null 2>&1 &
      echo "$(realpath $DEFAULT_APP) started"
   fi
else
   exit 1
fi
