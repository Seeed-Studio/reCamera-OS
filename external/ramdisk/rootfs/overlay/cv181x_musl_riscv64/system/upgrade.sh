#!/bin/bash

OFFICIAL_URL="https://github.com/Seeed-Studio/reCamera-OS/releases/latest"
OFFICIAL_URL2="https://files.seeedstudio.com/reCamera"

MD5_FILE=sg2002_recamera_emmc_md5sum.txt
URL_FILE=url.txt

FIP_PART=/dev/mmcblk0boot0
BOOT_PART=/dev/mmcblk0p1
RECV_PART=/dev/mmcblk0p5
ROOTFS=/dev/mmcblk0p3
ROOTFS_B=/dev/mmcblk0p4

UPGRADE_TMP=/userdata/.upgrade
UPGRADE_FILES=/tmp/upgrade
mkdir -p "$UPGRADE_FILES" "$UPGRADE_TMP"

# global variable
RunCase=""
ResultFile=""
MountPath=""
Step=0
step_log() {
    let Step++
    echo "Step$Step: $1"
}

step_result() { echo "Result: $1"; }

# exit
cleanup() {
    sync
    [ -n "$MountPath" ] && [ -d "$MountPath" ] && {
        umount "$MountPath" 2>/dev/null
        ! mount | grep -qw "$MountPath" && rm -rf "$MountPath"
    }
    rm -f "$ResultFile.mutex" 2>/dev/null
}
trap cleanup SIGINT SIGTERM

exit_upgrade() {
    [ -z "$1" ] && {
        [ -f "$ResultFile" ] || echo "Success" >"$ResultFile"
        echo "Success"
        cleanup
        exit 0
    }
    echo "Failed: $1" | tee "$ResultFile"
    cleanup
    exit 1
}

# ps
ps_mutex() {
    [ -f "$ResultFile.mutex" ] && {
        echo "./upgrade.sh $RunCase is running."
        exit 1
    }
    Step=0
    rm -f ${ResultFile}*
    echo $$ >"$ResultFile.mutex"
}

kill_ps() {
    local pid=$(ps | grep "$1" | grep -v grep | awk '{print $1}')
    [ -n "$pid" ] && kill -9 "$pid"
}

ps_stop() {
    kill_ps "dd"
    kill_ps "unzip -p"
    kill_ps "wget -T 10 -t 3 --no-check-certificate"
    killall $(basename "$0")
    echo "stopped"
    exit 0
}

# utils
parse_md5() { # sg2002_recamera_emmc_md5sum.txt
    local info=$(grep ".*ota.zip" "$2" 2>/dev/null)
    [ -z "$info" ] && return 1
    case "$1" in
    name) echo "$info" | awk '{print $2}' ;;
    md5) echo "$info" | awk '{print $1}' ;;
    os) echo "$info" | awk '{print $2}' | cut -d'_' -f2 ;;
    version) echo "$info" | awk '{print $2}' | cut -d'_' -f3 ;;
    *) return 1 ;;
    esac
}

zip_get_size() { # zip=$1 file=$2
    local size=$(unzip -l "$1" 2>/dev/null | grep "$2" | awk '{print $1}')
    [ -z "$size" ] && return 1
    echo $((size))
}

zip_read_md5() { # md5sum.txt
    zip=$1 file=$2
    md5=$(unzip -p "$zip" md5sum.txt 2>/dev/null | grep "$file" | awk '{print $1}')
    [ -z "$md5" ] && return 1
    echo $md5
}

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

is_rootfs_b() {
    local root_dev=$(mountpoint -n / | awk '{print $1}') || exit_upgrade "get rootfs"
    [ "$root_dev" = "$(realpath "$ROOTFS_B")" ] && return 1 || return 0
}

set_bootenv() {
    fw_setenv "$1" "$2" || exit_upgrade "set $1=$2"
}

switch_partition() {
    step_log "Switch rootfs partition"
    is_rootfs_b
    local part_b=$?
    part_b=$((1 - part_b))
    set_bootenv use_part_b $part_b
    set_bootenv boot_cnt 0
    set_bootenv boot_failed_limits 5
    set_bootenv boot_rollback
    step_log "Please reboot to take effect."
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
}

wget_file() {
    local url="$1" file="$2" size
    step_log "Download $url"
    local tmpfile=$(mktemp)
    wget -T 10 -t 3 --no-check-certificate --spider "$url" -o "$tmpfile"
    size="$(grep -i 'Length' "$tmpfile" | awk '{print $2}')"
    rm -f "$tmpfile"
    [ -z "$size" ] && exit_upgrade "get size $(basename $file)"
    echo "$file" >"$ResultFile.file"
    echo "$size" >"$ResultFile.size"

    wget -T 10 -t 3 --no-check-certificate -q -c --show-progress "$url" -O "$file" -o /dev/null || exit_upgrade "download $url"
    [ ! -s "$file" ] && exit_upgrade "download $(basename $file) is empty"
}

zip_write_part() {
    local zip=$1 part=$2 file=$3
    local size read_md5 calc_md5
    size=$(zip_get_size "$zip" "$file") || exit_upgrade "get size $zip $file"
    read_md5=$(zip_read_md5 "$zip" "$file") || exit_upgrade "read md5 $zip $file"
    step_log "Write $part with $file size: $size"
    echo "$size" >"$ResultFile.$file.size"

    local tmpfile=$(mktemp)
    unzip -p "$zip" "$file" 2>/dev/null | tee >(md5sum >"$tmpfile") | dd of="$part" bs=1M status=progress 2>&1 | tee -a "$ResultFile.$file.prog"
    local ret=$?
    calc_md5=$(cat $tmpfile | awk '{print $1}')
    rm -rf $tmpfile
    [ $ret -ne 0 ] && exit_upgrade "write $part with $file"

    step_log "Check md5sum $part"
    [ "$calc_md5" = "$read_md5" ] || exit_upgrade "check md5sum $part"
}

dd_calc_md5() {
    DD_Calc_MD5=""
    local file=$1 size=$2
    [ -e "$file" ] || exit_upgrade "file $file not exist"
    [ -z "$size" ] && size=$(stat -c %s "$file" 2>/dev/null) || size=$2
    [ $((size)) -eq 0 ] && exit_upgrade "unknown size $file"
    local tmpfile=$(mktemp)
    dd if="$file" bs=1M status=progress | head -c $size | md5sum >"$tmpfile"
    local ret=$?
    DD_Calc_MD5=$(cat $tmpfile | awk '{print $1}')
    rm -f $tmpfile
    [ $ret -ne 0 ] && exit_upgrade "calc md5sum $file"
}

ota_boot() { # fip.bin boot.emmc
    local zip=$1 part=$2 file=$3
    local size read_md5 calc_md5
    step_log "Check $part and $file"
    size=$(zip_get_size $zip $file)
    read_md5=$(zip_read_md5 $zip $file)
    ([ -z "$size" ] || [ -z "$read_md5" ]) && {
        step_result "skip with no valid $file"
        return 0
    }

    dd_calc_md5 $part $size
    calc_md5=$DD_Calc_MD5
    [ "$read_md5" = "$calc_md5" ] && {
        step_result "skip with md5 matched"
        return 0
    }

    calc_md5=$(unzip -p $zip $file 2>/dev/null | md5sum | awk '{print $1}')
    [ "$calc_md5" != "$read_md5" ] && {
        step_result "skip with $file damaged"
        return 0
    }

    [ "$part" = "$FIP_PART" ] && echo 0 >"/sys/block/mmcblk0boot0/force_ro" 2>/dev/null
    zip_write_part $zip $part $file
    [ "$part" = "$FIP_PART" ] && echo 1 >"/sys/block/mmcblk0boot0/force_ro" 2>/dev/null
}

#####
latest_cmd() {
    local cmd=$2
    [ "$cmd" = "x" ] && ps_stop
    [ "$cmd" = "q" ] && {
        [ -f $ResultFile ] && {
            cat $ResultFile
            exit 0
        }
        [ ! -f "$ResultFile.mutex" ] && {
            echo "Stopped"
            exit 0
        }
        exit 0
    }
}

latest() {
    latest_cmd $@
    ps_mutex
    local url="$2" md5_url=""
    if [ -z "$url" ]; then
        url="$OFFICIAL_URL"
        step_log "Parse $url"
        md5_url=$(get_upgrade_url "$url")
        [ -z "$md5_url" ] && {
            step_result "Failed parse $url"
            url="$OFFICIAL_URL2/latest"
            step_log "Parse $url"
            local ver=$(curl -sk "$url" --connect-timeout 30 --max-time 60)
            [ -z "$ver" ] && exit_upgrade "parse $url"
            md5_url="$OFFICIAL_URL2/"$ver"/$MD5_FILE"
        }
        step_result "$md5_url"
    else
        step_log "Parse $url"
        local md5_url md5_path
        md5_url=$(get_upgrade_url "$url") || exit_upgrade "parse $url"
        step_result "$md5_url"
    fi

    md5_path="$UPGRADE_FILES/$MD5_FILE"
    rm -f "$md5_path"
    wget_file "$md5_url" "$md5_path"

    # RESULT
    local os_name version
    os_name=$(parse_md5 os "$md5_path") || exit_upgrade "parse os $md5_path"
    version=$(parse_md5 version "$md5_path") || exit_upgrade "parse version $md5_path"
    echo "Success: $os_name $version" >$ResultFile
    echo "$(echo ${md5_url%/*})" >"$UPGRADE_FILES/$URL_FILE"
    step_result "$os_name@$version"
    exit_upgrade
}

download_cmd() {
    local cmd=$2
    [ "$cmd" = "x" ] && ps_stop
    [ "$cmd" = "q" ] && {
        [ -f $ResultFile ] && {
            cat $ResultFile
            exit 0
        }
        [ ! -f "$ResultFile.mutex" ] && {
            echo "Stopped"
            exit 0
        }

        local total=$(cat "$ResultFile.size" 2>/dev/null)
        local file=$(cat "$ResultFile.file" 2>/dev/null)
        local size=$(stat -c %s "$file" 2>/dev/null)
        echo "Progress: $((size)) $((total))"
        exit 0
    }
    [ ! -z "$cmd" ] && {
        echo "Usage: $0 $RunCase [q]"
        exit 1
    }
}

download() {
    download_cmd $@
    ps_mutex
    mount_recovery

    local tmpdir="$UPGRADE_TMP"
    local url_path_tmp="$tmpdir/$URL_FILE"
    local md5_path_tmp="$tmpdir/$MD5_FILE"
    local md5_path_latest="$UPGRADE_FILES/$MD5_FILE"

    step_log "Check files"
    [ -s $md5_path_latest ] && {
        local md5_path_now="$MountPath/$MD5_FILE"
        [ -s "$md5_path_now" ] && {
            [ -z "$(diff "$md5_path_latest" "$md5_path_now")" ] && {
                step_result "OTA is up to date."
                exit_upgrade
            }
        }
        ([ ! -s "$md5_path_tmp" ] || [ ! -z "$(diff "$md5_path_latest" "$md5_path_tmp")" ]) && {
            step_log "Copy latest files"
            rm -rf $tmpdir/*
            cp -f "$md5_path_latest" "$md5_path_tmp"
            cp -f "$UPGRADE_FILES/$URL_FILE" "$url_path_tmp"
        }
    }
    ([ -s "$md5_path_tmp" ] && [ -s "$url_path_tmp" ]) || {
        exit_upgrade "no latest files, please run 'upgrade.sh latest [url]' first"
    }

    local filename md5 url
    filename=$(parse_md5 name "$md5_path_tmp") || exit_upgrade "parse name $md5_path_tmp"
    md5=$(parse_md5 md5 "$md5_path_tmp") || exit_upgrade "parse md5 $md5_path_tmp"
    url=$(cat "$url_path_tmp")/$filename
    wget_file "$url" "$tmpdir/$filename"

    step_log "Check md5sum $filename"
    dd_calc_md5 "$tmpdir/$filename"
    [ "$md5" != "$DD_Calc_MD5" ] && exit_upgrade "md5 mismatch"

    step_log "Sync files"
    rm -rf $MountPath/*
    cp -f $tmpdir/*.zip $MountPath/ || exit_upgrade "copy $tmpdir/*.zip"
    cp -f $tmpdir/*.txt $MountPath/ || exit_upgrade "copy $tmpdir/*.txt"
    rm -rf $tmpdir

    exit_upgrade
}

start_cmd() {
    local cmd=$2
    [ "$cmd" = "x" ] && ps_stop
    [ "$cmd" = "q" ] && {
        [ -f "$ResultFile" ] && {
            cat "$ResultFile"
            exit 0
        }
        [ ! -f "$ResultFile.mutex" ] && {
            echo "Stopped"
            exit 0
        }

        local total=$(cat "$ResultFile.rootfs_ext4.emmc.size" 2>/dev/null)
        local size=$(tr '\r' '\n' <"$ResultFile.rootfs_ext4.emmc.prog" | awk 'END {print $1}')
        echo "Progress: $((size)) $((total))"
        exit 0
    }
}

start() {
    start_cmd $@
    ps_mutex
    mount_recovery

    local zip="$2"
    step_log "Check ota pack $zip"
    [ -z "$zip" ] && {
        file="$(parse_md5 name "$MountPath/$MD5_FILE")" || exit_upgrade "parse name $MountPath/$MD5_FILE"
        zip="$MountPath/$file"
    }
    [ -e "$zip" ] || { exit_upgrade "not found $zip"; }
    step_result "OTA will use $zip"

    ota_boot "$zip" "$FIP_PART" "fip.bin"
    ota_boot "$zip" "$BOOT_PART" "boot.emmc"

    local target="$ROOTFS_B"
    is_rootfs_b || target="$ROOTFS"
    zip_write_part "$zip" "$target" "rootfs_ext4.emmc"

    switch_partition
    exit_upgrade
}

recovery() {
    step_log "Set recovery flag"
    set_bootenv factory_reset 1
    step_log "Please reboot to take effect."
    exit_upgrade
}

clean() {
    rm -rf $UPGRADE_FILES
    rm -rf $UPGRADE_TMP/*
    echo "Success"
}

# call function
RunCase=$1
ResultFile="$UPGRADE_FILES/$RunCase"
FUNCS=(latest download start rollback recovery clean)
for func in ${FUNCS[@]}; do
    [ "$RunCase" = "$func" ] && {
        $func $@
        exit 0
    }
done
echo "Usage: $0 {clean|latest|download|start|rollback|recovery}"
