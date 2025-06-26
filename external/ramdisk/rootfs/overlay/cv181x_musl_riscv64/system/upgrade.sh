#!/bin/sh

MD5_FILE=sg2002_recamera_emmc_md5sum.txt
URL_FILE=url.txt
ISSUE_FILE=/etc/issue

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
    [ -z "$1" ] && {
        echo "Success" >$file_result
        echo "Success"
        exit 0
    }
    echo "failed" >$file_result
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

# utils
mount_recovery() {
    local fs_type=$(blkid -o value -s TYPE "$RECV_PART")
    if [ "$fs_type" != "ext4" ]; then
        mkfs.ext4 "$RECV_PART" || exit_upgrade "format recovery partition"
        fs_type=$(blkid -o value -s TYPE "$RECV_PART")
        [ "$fs_type" != "ext4" ] && exit_upgrade "recovery partition is not ext4!"
    fi

    MountPath=$(mktemp -d)
    mount "$RECV_PART" "$MountPath" && { return 0; }
    exit_upgrade "mount $RECV_PART on $MountPath failed."
}

is_use_partition_b() {
    local root_dev=$(mountpoint -n / | awk '{print $1}')
    [ "$root_dev" = "$(realpath "$ROOTFS_B")" ] && return 1 || return 0
}

write_upgrade_flag() {
    fw_setenv use_part_b $1 || { exit_upgrade "write use_part_b"; }
    fw_setenv boot_cnt 0 || { exit_upgrade "write boot_cnt"; }
    fw_setenv boot_failed_limits 5 || { exit_upgrade "write boot_failed_limits"; }
    fw_setenv boot_rollback || { exit_upgrade "write boot_rollback"; }
}

switch_partition() {
    is_use_partition_b
    local is_part_b=$?
    [ $is_part_b -eq 0 ] && write_upgrade_flag 1
    [ $is_part_b -eq 1 ] && write_upgrade_flag 0
    echo "Please reboot."
}

get_upgrade_url() {
    local url=$1 full_url=$url
    [[ $url =~ .*\.txt$ ]] || {
        url=$(curl -skLi "$url" --connect-timeout 30 --max-time 60 | grep -i '^location:' | awk '{print $2}' | sed 's/^"//;s/"$//')
        [ -z "$url" ] && {
            echo ""
            return 1
        }
        url=$(echo "$url" | sed 's/tag/download/g')
        full_url="$url/$MD5_FILE"
    }
    echo "$full_url"
    return 0
}

wget_file() {
    wget -q -c -T 10 -t 3 --no-check-certificate "$1" -O "$2" >/dev/null 2>&1
    [ $? -ne 0 ] && { exit_upgrade "download $1"; }
}

wget_size() {
    echo "$(wget --no-check-certificate --spider "$1" 2>&1 | grep 'Length' | awk '{print $2}')"
    ([ $? -ne 0 ] || [ $((size)) -eq 0 ]) && { exit_upgrade "get size $1"; }
}

calc_md5() {
    local file=$1 size=$2
    [ -e $file ] || exit_upgrade "file is not exist"
    [ -z $size ] && size=$(stat -c %s "$file" 2>/dev/null)
    [ $((size)) -eq 0 ] && { exit_upgrade "get size $file"; }

    local md5=$(dd if="$file" bs=1M 2>/dev/null | head -c "$size" | md5sum | awk '{print $1}')
    [ $? -ne 0 ] && { exit_upgrade "calc md5 $file"; }
    echo "$md5"
}

parse_md5() { # sg2002_recamera_emmc_md5sum.txt
    local info=$(grep ".*ota.zip" "$2" 2>/dev/null)
    [ -z "$info" ] && { exit_upgrade "parse md5 $2"; }
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
}

check_zip() { # ota.zip
    local dir=$1
    local md5file=$dir/$MD5_FILE
    [ -s $md5file ] || { exit_upgrade "md5 file is empty"; }
    local file=$dir/$(parse_md5 name "$md5file")
    [ -s $file ] || { exit_upgrade "zip is empty"; }
    local md5=$(calc_md5 "$file")
    [ "$md5" != "$(parse_md5 md5 "$md5file")" ] && { exit_upgrade "md5 mismatch"; }
}

zip_filesize() {
    local zip=$1 file=$2
    local size=$(unzip -l "$zip" 2>/dev/null | grep "$file" | awk '{print $1}')
    [ $? -ne 0 ] && { exit_upgrade "get size $zip/$file"; }
    echo $size
}

zip_read_md5() {
    local zip=$1 file=$2
    local md5=$(unzip -p "$zip" md5sum.txt 2>/dev/null | grep "$file" | awk '{print $1}')
    [ $? -ne 0 ] && { exit_upgrade "get md5 $zip/$file"; }
    echo $md5
}

zip_calc_md5() {
    local zip=$1 file=$2
    local md5=$(unzip -p $zip $file 2>/dev/null | md5sum | awk '{print $1}')
    [ $? -ne 0 ] && { exit_upgrade "calc md5 $zip/$file"; }
    echo $md5
}

zip_write_file() {
    local zip=$1 file=$2 part=$3
    unzip -p $zip $file 2>/dev/null >$part
}

ota_boot() { # fip.bin boot.emmc
    local zip=$1 part=$2 file=$3
    local size=$(zip_filesize $zip $file)
    local read_md5=$(zip_read_md5 $zip $file)

    local part_md5=$(calc_md5 $part $size)
    [ "$read_md5" = "$part_md5" ] && { echo "MD5 match skiped $part"; } && { return 1; }

    local file_md5=$(zip_calc_md5 $zip $file)
    [ "$part" = "$FIP_PART" ] && { echo 0 >/sys/block/mmcblk0boot0/force_ro; }
    [ "$read_md5" = "$file_md5" ] && { zip_write_file $zip $file $part; }
    [ "$part" = "$FIP_PART" ] && { echo 1 >/sys/block/mmcblk0boot0/force_ro; }
}

#####
latest() {
    RUN_CASE=$1
    local file_result="$UPGRADE_FILES/$RUN_CASE"

    [ "$2" = "q" ] && { [ -f $file_result ] && { cat $file_result; } && exit 0; }
    [ -z "$2" ] && {
        echo "Usage: $0 $RUN_CASE <url> | [q]"
        exit 1
    }
    ps_mutex

    local url=$2
    log_step "Parse url: $url"
    local md5_url=$(get_upgrade_url "$url") || { exit_upgrade "parse $url"; }

    log_step "Download: $md5_url"
    local md5_path="$UPGRADE_FILES/$MD5_FILE"
    wget_file "$md5_url" "$md5_path"
    [ ! -s "$md5_path" ] && { exit_upgrade "download $md5_url"; }

    # RESULT
    echo "$(parse_md5 os "$md5_path") $(parse_md5 version "$md5_path")" >$file_result
    local upgrade_url=$(echo ${md5_url%/*})
    echo "$upgrade_url" >"$UPGRADE_FILES/$URL_FILE"

    # EXIT
    exit_upgrade
}

download() {
    RUN_CASE=$1
    local file_result="$UPGRADE_FILES/$RUN_CASE"

    [ "$2" = "q" ] && {
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
    [ ! -z "$2" ] && {
        echo "Usage: $0 $RUN_CASE [q]"
        exit 1
    }
    ps_mutex

    log_step "Mount partition"
    mount_recovery

    local tmpdir="$UPGRADE_TMP"
    local md5_path_tmp="$tmpdir/$MD5_FILE"
    local url_path_tmp="$tmpdir/$URL_FILE"

    log_step "Check files"
    local md5_path_latest="$UPGRADE_FILES/$MD5_FILE"
    local md5_path_now="$MountPath/$MD5_FILE"

    [ -s $md5_path_latest ] && {
        [ -s "$md5_path_now" ] && {
            [ -z "$(diff "$md5_path_latest" "$md5_path_now")" ] &&
                {
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

    ([ -s "$md5_path_tmp" ] && [ -s "$url_path_tmp" ]) || { exit_upgrade "run 'upgrade.sh latest' first"; }

    local filename=$(parse_md5 name "$md5_path_tmp")
    local url=$(cat "$url_path_tmp")/$filename

    log_step "Get $filename size"
    local size=$(wget_size "$url")
    echo "size: $((size))"
    echo "$((size))" >$file_result.size

    local dir_size=$(df -B1 "$tmpdir" | grep "$USERDATA" | awk '{print $4}')
    [ $((size)) -gt $((dir_size)) ] && { exit_upgrade "$USERDATA space is not enough"; }

    log_step "Download: $url"
    echo "$tmpdir/$filename" >$file_result.file
    wget_file "$url" "$tmpdir/$filename"

    log_step "Check $filename md5sum"
    check_zip "$tmpdir"

    log_step "sync files"
    rm -rf $MountPath/*
    cp -f $tmpdir/*.zip $MountPath/
    cp -f $tmpdir/*.txt $MountPath/
    rm -rf $tmpdir

    # EXIT
    exit_upgrade
}

start() {
    RUN_CASE=$1
    local file_result="$UPGRADE_FILES/$RUN_CASE"

    [ "$2" = "x" ] && {
        exit 0
    }
    [ "$2" = "q" ] && { [ -f "$file_result" ] && { cat "$file_result"; } && exit 0; }
    [ -n "$2" ] && {
        echo "Usage: $0 $RUN_CASE [q] | [x]"
        exit 1
    }
    ps_mutex

    log_step "Mount partition"
    mount_recovery

    log_step "Check files"
    local file="$2"
    [ ! -s "$file" ] && { file="$MountPath/$(parse_md5 name "$MountPath/$MD5_FILE")"; }
    [ ! -s "$file" ] && { exit_upgrade "not found ota.zip"; }

    >"$file_result"
    log_step "Write $FIP_PART"
    ota_boot "$file" "$FIP_PART" "fip.bin"

    log_step "Write $BOOT_PART"
    ota_boot "$file" "$BOOT_PART" "boot.emmc"

    is_use_partition_b
    local target=$([ $? -eq 1 ] && echo "$ROOTFS" || echo "$ROOTFS_B")
    log_step "Write rootfs $target"
    zip_write_file "$file" "$target" "rootfs_ext4.emmc"

    log_step "Check rootfs $target"
    local read_md5=$(zip_read_md5 "$file" "rootfs_ext4.emmc") || { exit_upgrade "read md5 fail"; }
    local size=$(zip_filesize "$file" "rootfs_ext4.emmc") || { exit_upgrade "read size fail"; }
    local md5=$(calc_md5 $target $size)
    echo md5=$?
    [ "$md5" != "$read_md5" ] && { exit_upgrade "md5 mismatch $target"; }

    log_step "Write upgrade flag"
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
if [ "$(type $1)" = "$1 is a function" ]; then
    $1 $@
else
    echo "Not found"
fi
