#!/bin/sh
MD5_FILE=sg2002_recamera_emmc_md5sum.txt
URL_FILE=url.txt

FIP_PART=/dev/mmcblk0boot0
BOOT_PART=/dev/mmcblk0p1
RECV_PART=/dev/mmcblk0p5
ROOTFS=/dev/mmcblk0p3
ROOTFS_B=/dev/mmcblk0p4
ROOTFS_FILE=rootfs_ext4.emmc

UPGRADE_FILES=/tmp/upgrade
mkdir -p "$UPGRADE_FILES"

USERDATA=/userdata
UPGRADE_TMP=$USERDATA/.upgrade
mkdir -p "$UPGRADE_TMP"

MountPath=""
Step=0
log_step() {
    let Step++
    echo "Step$Step: $1"
}

# exit
cleanup() {
    sync
    [ -n "$MountPath" ] && [ -d "$MountPath" ] && {
        umount "$MountPath" 2>/dev/null
        ! mount | grep -qw "$MountPath" && rm -rf "$MountPath"
    }
    rm -rf "$UPGRADE_FILES/$RUN_CASE.mutex"
}
trap cleanup SIGINT SIGTERM

exit_upgrade() {
    cleanup
    local file_result="$UPGRADE_FILES/$RUN_CASE"
    [ -n "$RUN_CASE" ] || file_result="$UPGRADE_FILES/result"
    [ -z "$1" ] && {
        [ -f "$file_result" ] || echo "Success" >"$file_result"
        echo "Success"
        exit 0
    }
    echo "Failed" >"$file_result"
    echo "Failed: $1"
    exit 1
}

# ps
ps_mutex() {
    local file="$UPGRADE_FILES/$RUN_CASE"
    [ -f "$file.mutex" ] && {
        echo "./upgrade.sh $RUN_CASE is running."
        exit 1
    }
    Step=0
    rm -f ${file}*
    echo $$ >"$file.mutex"
}

ps_stop() {
    local pid=$(cat "$UPGRADE_FILES/$RUN_CASE.mutex")
    [ -n "$pid" ] && {
        kill -9 $(($pid))
    }
    exit 0
}

# utils
mount_recovery() {
    local fs_type=$(blkid -o value -s TYPE "$RECV_PART")
    [ "$fs_type" != "ext4" ] && {
        mkfs.ext4 "$RECV_PART" || exit_upgrade "format recovery partition"
        fs_type=$(blkid -o value -s TYPE "$RECV_PART")
        [ "$fs_type" != "ext4" ] && exit_upgrade "recovery partition is not ext4!"
    }
    MountPath=$(mktemp -d)
    mount "$RECV_PART" "$MountPath" && return 0
    exit_upgrade "mount $RECV_PART on $MountPath failed."
}

is_use_partition_b() {
    local root_dev=$(mountpoint -n / | awk '{print $1}')
    [ "$root_dev" = "$(realpath "$ROOTFS_B")" ] && return 1 || return 0
}

write_upgrade_flag() {
    fw_setenv use_part_b $1 || exit_upgrade "write use_part_b"
    fw_setenv boot_cnt 0 || exit_upgrade "write boot_cnt"
    fw_setenv boot_failed_limits 5 || exit_upgrade "write boot_failed_limits"
    fw_setenv boot_rollback || exit_upgrade "write boot_rollback"
}

switch_partition() {
    log_step "Switch rootfs partition"
    is_use_partition_b
    local is_part_b=$?
    [ $is_part_b -eq 0 ] && write_upgrade_flag 1
    [ $is_part_b -eq 1 ] && write_upgrade_flag 0
    log_step "Please reboot to take effect."
}

get_upgrade_url() {
    local url=$1 full_url=$url
    [[ $url =~ .*\.txt$ ]] || {
        url=$(curl -skLi "$url" --connect-timeout 30 --max-time 60 | grep -i '^location:' | awk '{print $2}' | sed 's/^"//;s/"$//')
        url=$(echo "$url" | sed 's/tag/download/g')
        [ -z "$url" ] && return 1
        full_url="$url/$MD5_FILE"
    }
    echo "$full_url"
    return 0
}

wget_file() {
    url=$1 file=$2
    log_step "Get file size $url"
    size="$(wget --no-check-certificate --spider "$1" 2>&1 | grep 'Length' | awk '{print $2}')" || exit_upgrade "get size $url"
    echo "size=$((size))"

    log_step "Download $url"
    wget -q -c -T 10 -t 3 --no-check-certificate --show-progress "$url" -O "$file" || exit_upgrade "download $url"
    [ ! -s "$file" ] && exit_upgrade "download $file is empty"
}

dd_calc_md5() {
    local file=$1 size=$2
    [ -e $file ] || return 1
    [ -z $size ] && size=$(stat -c %s "$file" 2>/dev/null)
    [ $((size)) -eq 0 ] && return 1
    local md5=$(dd if="$file" bs=1M 2>/dev/null | head -c "$size" | md5sum | awk '{print $1}')
    [ -z "$md5" ] && return 1
    echo "$md5"
}

parse_md5() { # sg2002_recamera_emmc_md5sum.txt
    local info=$(grep ".*ota.zip" "$2" 2>/dev/null)
    [ -z "$info" ] && return 1
    [ "$1" = "name" ] && {
        echo "$info" | awk '{print $2}'
        return 0
    }
    [ "$1" = "md5" ] && {
        echo "$info" | awk '{print $1}'
        return 0
    }
    [ "$1" = "os" ] && {
        echo $(echo "$info" | awk '{print $2}' | cut -d'_' -f2)
        return 0
    }
    [ "$1" = "version" ] && {
        echo $(echo "$info" | awk '{print $2}' | cut -d'_' -f3)
        return 0
    }
    return 1
}

zip_get_size() { # zip=$1 file=$2
    local size=$(unzip -l "$1" 2>/dev/null | grep "$2" | awk '{print $1}')
    [ -z "$size" ] && return 1
    echo $((size))
}

zip_read_md5() {
    zip=$1 file=$2
    md5=$(unzip -p "$zip" md5sum.txt 2>/dev/null | grep "$file" | awk '{print $1}')
    [ -z "$md5" ] && return 1
    echo $md5
}

zip_calc_md5() {
    zip=$1 file=$2
    md5=$(unzip -p $zip $file 2>/dev/null | md5sum | awk '{print $1}')
    [ -z "$md5" ] && return 1
    echo $md5
}

zip_write_part() {
    local zip=$1 part=$2 file=$3
    log_step "Write $part with $file"
    unzip -p $zip $file 2>/dev/null >$part
    [ $? -ne 0 ] && exit_upgrade "write $part"
}

check_zip() { # ota.zip
    local dir=$1 md5file=$dir/$MD5_FILE
    [ -s $md5file ] || exit_upgrade "$md5file is empty"
    file=$dir/$(parse_md5 name "$md5file")
    [ -s $file ] || exit_upgrade "$file is empty"
    md5=$(dd_calc_md5 "$file")
    [ "$md5" != "$(parse_md5 md5 "$md5file")" ] && exit_upgrade "md5 mismatch"
}

ota_boot() { # fip.bin boot.emmc
    local zip=$1 part=$2 file=$3
    local size read_md5 calc_md5
    log_step "Check $part and $file"
    size=$(zip_get_size $zip $file) || exit_upgrade "get size $file"
    read_md5=$(zip_read_md5 $zip $file) || exit_upgrade "read md5 $file"
    calc_md5=$(dd_calc_md5 $part $size) || exit_upgrade "calc md5 $part"
    [ "$read_md5" = "$calc_md5" ] && { echo "Skiped with md5 match"; } && { return 1; }

    calc_md5=$(zip_calc_md5 $zip $file) || exit_upgrade "calc md5 $part"
    [ "$part" = "$FIP_PART" ] && echo 0 >/sys/block/mmcblk0boot0/force_ro
    [ "$read_md5" = "$calc_md5" ] && zip_write_part $zip $part $file
    [ "$part" = "$FIP_PART" ] && echo 1 >/sys/block/mmcblk0boot0/force_ro
}

#####
latest() {
    RUN_CASE=$1
    local cmd=$2
    local file_result="$UPGRADE_FILES/$RUN_CASE"
    [ "$2" = "q" ] && {
        [ -f $file_result ] && cat $file_result
        exit 0
    }
    [ "$cmd" = "x" ] && ps_stop
    [ -z "$2" ] && {
        echo "Usage: $0 $RUN_CASE <url> | [q]"
        exit 1
    }
    ps_mutex

    local url="$2"
    log_step "Parse $url"
    md5_url=$(get_upgrade_url "$url") || exit_upgrade "parse $url"
    md5_path="$UPGRADE_FILES/$MD5_FILE"
    rm -f "$md5_path"
    wget_file "$md5_url" "$md5_path"

    # RESULT
    local os_name version
    os_name=$(parse_md5 os "$md5_path") || exit_upgrade "parse os $md5_path"
    version=$(parse_md5 version "$md5_path") || exit_upgrade "parse version $md5_path"
    echo "$os_name $version" >$file_result
    echo "$(echo ${md5_url%/*})" >"$UPGRADE_FILES/$URL_FILE"

    log_step "Result $os_name@$version"
    exit_upgrade
}

download() {
    RUN_CASE=$1
    local cmd=$2
    local file_result="$UPGRADE_FILES/$RUN_CASE"
    [ "$cmd" = "q" ] && {
        [ -f $file_result ] && {
            cat $file_result
            exit 0
        }
        local file=$(cat "$file_result.file" 2>/dev/null)
        local size=$(cat "$file_result.size" 2>/dev/null)
        [ $((size)) -ne 0 ] && {
            local dl_size=$(stat -c %s "$file")
            dl_size=$(($dl_size * 100))
            dl_size=$(($dl_size / $size))
            echo "$dl_size"
        }
        exit 0
    }
    [ "$cmd" = "x" ] && ps_stop
    [ ! -z "$cmd" ] && {
        echo "Usage: $0 $RUN_CASE [q]"
        exit 1
    }
    ps_mutex
    mount_recovery

    local tmpdir="$UPGRADE_TMP"
    local url_path_tmp="$tmpdir/$URL_FILE"
    local md5_path_tmp="$tmpdir/$MD5_FILE"
    local md5_path_latest="$UPGRADE_FILES/$MD5_FILE"
    local md5_path_now="$MountPath/$MD5_FILE"

    log_step "Check files"
    [ -s $md5_path_latest ] && {
        [ -s "$md5_path_now" ] && {
            [ -z "$(diff "$md5_path_latest" "$md5_path_now")" ] && {
                echo "OTA is up to date."
                exit_upgrade
            }
        }
        ([ ! -s "$md5_path_tmp" ] || [ ! -z "$(diff "$md5_path_latest" "$md5_path_tmp")" ]) && {
            log_step "Copy latest files"
            rm -rf $tmpdir/*
            cp -f "$md5_path_latest" "$md5_path_tmp"
            cp -f "$UPGRADE_FILES/$URL_FILE" "$url_path_tmp"
        }
    }

    ([ -s "$md5_path_tmp" ] && [ -s "$url_path_tmp" ]) || exit_upgrade "run 'upgrade.sh latest' first"

    local filename url
    filename=$(parse_md5 name "$md5_path_tmp") || exit_upgrade "parse name $md5_path_tmp"
    url=$(cat "$url_path_tmp")/$filename
    wget_file "$url" "$tmpdir/$filename"

    log_step "Check $filename md5sum"
    check_zip "$tmpdir"

    log_step "Sync files"
    rm -rf $MountPath/*
    cp -f $tmpdir/*.zip $MountPath/ || exit_upgrade "copy $tmpdir/*.zip"
    cp -f $tmpdir/*.txt $MountPath/ || exit_upgrade "copy $tmpdir/*.txt"
    rm -rf $tmpdir

    # EXIT
    exit_upgrade
}

start() {
    RUN_CASE=$1
    local cmd=$2
    local file_result="$UPGRADE_FILES/$RUN_CASE"

    [ "$cmd" = "x" ] && ps_stop
    [ "$2" = "q" ] && { [ -f "$file_result" ] && { cat "$file_result"; } && exit 0; }
    ps_mutex
    mount_recovery

    zip="$2"
    log_step "Check ota pack $zip"
    [ ! -s "$zip" ] && { zip="$MountPath/$(parse_md5 name "$MountPath/$MD5_FILE")"; }
    [ ! -s "$zip" ] && { exit_upgrade "not found ota.zip"; }
    log_step "OTA will use $zip"

    ota_boot "$zip" "$FIP_PART" "fip.bin"
    ota_boot "$zip" "$BOOT_PART" "boot.emmc"

    local target="$ROOTFS_B"
    is_use_partition_b
    [ $? -eq 1 ] && target="$ROOTFS"
    zip_write_part "$zip" "$target" "$ROOTFS_FILE"

    log_step "Check md5 $target"
    local calc_md5="" read_md5="" size=""
    size=$(zip_get_size "$zip" "$ROOTFS_FILE") || exit_upgrade "get size $zip $ROOTFS_FILE"
    read_md5=$(zip_read_md5 "$zip" "$ROOTFS_FILE") || exit_upgrade "read md5 $zip $ROOTFS_FILE"
    calc_md5=$(dd_calc_md5 $target $size) || exit_upgrade "calc_md5 $target $size"
    [ "$read_md5" == "$calc_md5" ] || exit_upgrade "md5 mismatch $target"

    switch_partition
    exit_upgrade
}

rollback() {
    switch_partition
    exit_upgrade
}

recovery() {
    fw_setenv factory_reset 1
    [ $? -ne 0 ] && { exit_upgrade "recovery"; }
    echo "Please reboot."
    exit_upgrade
}

clean() {
    rm -rf $UPGRADE_FILES
    rm -rf $UPGRADE_TMP/*
    echo "Success"
}

# call function
[ "$(type $1)" = "$1 is a function" ] && { $1 $@; } || { echo "Not found"; }
