#!/bin/sh
${CVI_SHOPTS}

# boot success, clean boot_cnt
if [ $(fw_printenv boot_cnt) != "boot_cnt=0" ]; then
    fw_setenv boot_cnt 0;
fi

# rootfs read only
rootfs_rw off > /dev/null 2>&1

# default app
if [ ! -f "/tmp/evb_init" ]; then
   echo 1 > /tmp/evb_init
   DEFAULT_APP=/mnt/system/default_app
   if [ -x $DEFAULT_APP ]; then
      $DEFAULT_APP > /dev/null 2>&1 &
      echo "$(realpath $DEFAULT_APP) started"
   fi
fi
