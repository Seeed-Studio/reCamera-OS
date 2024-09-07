#!/bin/sh
${CVI_SHOPTS}

export LD_LIBRARY_PATH="/lib:/lib/3rd:/lib/arm-linux-gnueabihf:/usr/lib:/usr/local/lib:/mnt/system/lib:/mnt/system/usr/lib:/mnt/system/usr/lib/3rd:/mnt/data/lib"
export PATH="/usr/local/bin:/usr/bin:/bin:/usr/local/sbin:/usr/sbin:/sbin:/mnt/system/usr/bin:/mnt/system/usr/sbin:/mnt/data/bin:/mnt/data/sbin"

# boot success, clean boot_cnt
if [ $(fw_printenv boot_cnt) != "boot_cnt=0" ]; then
    fw_setenv boot_cnt 0;
fi

# rootfs read only
rootfs_rw off

# default app
if [ ! -f "/tmp/evb_init" ]; then
   echo 1 > /tmp/evb_init
   DEFAULT_APP=/mnt/system/default_app
   if [ -x $DEFAULT_APP ]; then
      $DEFAULT_APP > /dev/null 2>&1 &
      echo "$(realpath $DEFAULT_APP) started"
   fi
fi
