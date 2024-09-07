#!/bin/sh

OVERLAY_PATH=""
OVERLAY_WORK_PATH=""

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
   mkdir -p /home

   mount_dir /bin
   mount_dir /etc
   mount_dir /lib
   mount_dir /home
   mount_dir /root
   mount_dir /usr
   mount_dir /var

   mount -a
}

# mount userdata partition
USERDATA_PARTITION="/dev/mmcblk0p6"
USERDATA_MOUNTPOINT="/userdata"
fs_type=$(/sbin/blkid -o value -s TYPE $USERDATA_PARTITION)
echo "userdata partition type: ${fs_type}"
if [ "$fs_type" == "" ]; then
   mkfs.ext4 $USERDATA_PARTITION
   fs_type=$(blkid -o value -s TYPE $USERDATA_PARTITION)
fi
if [ "$fs_type" != "" ]; then
   if [ ! -d $USERDATA_MOUNTPOINT ]; then
      mkdir -p $USERDATA_MOUNTPOINT
   fi

   mp=$(mountpoint -n $USERDATA_MOUNTPOINT | awk '{print $1}')
   echo "mp=$mp"
   if [ $mp != $USERDATA_PARTITION ]; then
      mount $USERDATA_PARTITION $USERDATA_MOUNTPOINT
   else
      echo "$USERDATA_MOUNTPOINT is already mounted"
   fi
   mp=$(mountpoint -n $USERDATA_MOUNTPOINT | awk '{print $1}')
   if [ $mp == $USERDATA_PARTITION ]; then
      echo "userdata partition mounted successfully"
      rootfs_overlay $USERDATA_MOUNTPOINT
   fi
fi
