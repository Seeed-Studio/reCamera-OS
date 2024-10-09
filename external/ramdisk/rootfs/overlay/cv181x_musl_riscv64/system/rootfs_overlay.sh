#!/bin/sh

OVERLAY_PATH=""
OVERLAY_WORK_PATH=""

# file permissions
if [ -f /usr/bin/fw_setenv ]; then
   chmod 500 /usr/bin/fw_setenv
fi

# rootfs_overlay
function mount_dir() {
   local lower=$1
   local upper=${OVERLAY_PATH}${lower}
   local work=${OVERLAY_WORK_PATH}${lower}
   mkdir -p $upper $work
   mount -t overlay overlay -o lowerdir=$lower,upperdir=$upper,workdir=$work $lower
   echo "overlay $lower on $upper"
}

function rootfs_overlay() {
   OVERLAY_PATH=$1/.overlay_fs
   OVERLAY_WORK_PATH=$OVERLAY_PATH/.work

   if [ ! -d $OVERLAY_PATH ]; then
      mkdir -p $OVERLAY_PATH $OVERLAY_WORK_PATH
   fi
   if [ ! -d $OVERLAY_PATH ]; then
      echo "$OVERLAY_PATH not exist"
      exit 1
   fi

   # mount overlay directories
   mount_dir /bin
   mount_dir /etc
   mount_dir /lib
   mount_dir /home
   mount_dir /root
   mount_dir /usr
   mount_dir /var

   mount -a
}

USERDATA_PARTITION="/dev/mmcblk0p6"

# reset factory
if [ ! -d /sys/class/gpio/gpio510 ]; then
   echo "510" > /sys/class/gpio/export
fi
if [ -d /sys/class/gpio/gpio510 ]; then
   echo "in" > /sys/class/gpio/gpio510/direction
   if [ $(cat /sys/class/gpio/gpio510/value) == "0" ]; then
      sleep 1
      if [ $(cat /sys/class/gpio/gpio510/value) == "0" ]; then
         /mnt/system/upgrade.sh recovery
      fi
   fi
fi

if [ $(fw_printenv factory_reset) == "factory_reset=1" ]; then
   echo "factory reset"
   echo none > /sys/devices/platform/leds/leds/red/trigger
   echo 255 > /sys/devices/platform/leds/leds/red/brightness

   # step1: recovery rootfs
   /mnt/system/upgrade.sh start .
   # step2: erase userdata partition
   dd if=/dev/zero of=$USERDATA_PARTITION bs=1M count=5
   # step3: clear flag
   fw_setenv factory_reset
   reboot
fi

# mount userdata partition
if [ -e $USERDATA_PARTITION ]; then
USERDATA_MOUNTPOINT="/userdata"
MKFS_FLAG="N"
fs_type=$(/sbin/blkid -o value -s TYPE $USERDATA_PARTITION)
echo "userdata partition type: ${fs_type}"
if [ "$fs_type" == "" ]; then
   mkfs.ext4 $USERDATA_PARTITION
   MKFS_FLAG="Y"
   fs_type=$(blkid -o value -s TYPE $USERDATA_PARTITION)
fi
if [ "$fs_type" != "" ]; then
   if [ ! -d $USERDATA_MOUNTPOINT ]; then
      mkdir -p $USERDATA_MOUNTPOINT
   fi

   mp=$(mountpoint -n $USERDATA_MOUNTPOINT | awk '{print $1}')
   echo "mountpoint=$mp"
   if [ $mp != $USERDATA_PARTITION ]; then
      mount $USERDATA_PARTITION $USERDATA_MOUNTPOINT
   fi
   mp=$(mountpoint -n $USERDATA_MOUNTPOINT | awk '{print $1}')
   if [ $mp == $USERDATA_PARTITION ]; then
      echo "userdata partition mounted successfully"
      if [ $MKFS_FLAG == "Y" ]; then
         chown -R recamera:recamera $USERDATA_MOUNTPOINT
         rm -rf $USERDATA_MOUNTPOINT/*
      fi
      rootfs_overlay $USERDATA_MOUNTPOINT
   fi
fi
fi
