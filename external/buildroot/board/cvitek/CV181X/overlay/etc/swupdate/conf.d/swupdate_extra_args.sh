root_dev=$(mountpoint -n /)
root_dev=${root_dev%% *}

# clear boot_cnt
if [ $(fw_printenv boot_cnt) != "boot_cnt=0" ]; then
    fw_setenv boot_cnt 0;
fi

# determine upgrade target
if [ "$root_dev" = "$(realpath /dev/mmcblk0p4)" ]; then
    SWUPDATE_ARGS="$SWUPDATE_ARGS -e stable,b_write_a"
else
    SWUPDATE_ARGS="$SWUPDATE_ARGS -e stable,a_write_b"
fi