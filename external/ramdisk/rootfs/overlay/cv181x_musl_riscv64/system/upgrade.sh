#!/bin/sh

MD5_FILE=sg2002_recamera_emmc_md5sum.txt
URL_FILE=url.txt
ISSUE_FILE=/etc/issue

RECV_PARTITION=/dev/mmcblk0p5
ROOTFS=/dev/mmcblk0p3
ROOTFS_B=/dev/mmcblk0p4
ROOTFS_FILE=rootfs_ext4.emmc

CTRL_FILE=/tmp/upgrade
RESULT_FILE=$CTRL_FILE.result

function write_upgrade_flag() {
    fw_setenv use_part_b $1
    fw_setenv boot_cnt 0
    fw_setenv boot_failed_limits 5
    fw_setenv boot_rollback
}

function cleanup() {
    sync
    if [ ! -z $MOUNTPATH ] && [ -d $MOUNTPATH ]; then
        umount $MOUNTPATH
        # Defense against false deletion
        mount_check=$(mount | grep -w $MOUNTPATH)
        if [ -z $mount_check ]; then
            rm -rf $MOUNTPATH
        fi
    fi

    # Remove case ctrl files
    rm -rf $CTRL_FILE.$RUN_CASE*
}
trap cleanup SIGINT SIGTERM

function exit_upgrade() {
    if [ "$1" != "0" ]; then
        ps_ctrl error
    fi

    cleanup
    exit $1
}

function get_upgrade_url() {
    local url=$1
    local full_url=$url

    if [[ $url =~ .*\.txt$ ]]; then
        full_url=$url
    else
        url=$(curl -skLi $url --connect-timeout 30 --max-time 60 | grep -i '^location:' | awk '{print $2}' | sed 's/^"//;s/"$//')
        if [ -z "$url" ]; then
            echo ""
            return 1
        fi

        url=$(echo "$url" | sed 's/tag/download/g')
        full_url=$url/$MD5_FILE
    fi

    echo $full_url
    return 0
}

function mount_recovery() {
    fs_type=$(blkid -o value -s TYPE $RECV_PARTITION)
    if [ "$fs_type" != "ext4" ]; then
        mkfs.ext4 $RECV_PARTITION

        # check again
        fs_type=$(blkid -o value -s TYPE $RECV_PARTITION)
        if [ "$fs_type" != "ext4" ]; then
            echo "Recovery partition is not ext4!"
            exit_upgrade 1
        fi
    fi

    MOUNTPATH=$(mktemp -d);
    mount $RECV_PARTITION $MOUNTPATH
    if mount | grep -q "$RECV_PARTITION on $MOUNTPATH type"; then
        echo "Mount $RECV_PARTITION on $MOUNTPATH ok."
        return 0
    else
        echo "Mount $RECV_PARTITION on $MOUNTPATH failed."
        exit_upgrade 1
    fi
}

function wget_file() {
    wget -q -c -T 10 -t 3 --no-check-certificate $1 -O $2
    if [ $? -ne 0 ]; then
        echo "Failed: unable to download $2."
        exit_upgrade 1
    fi
    if [ ! -f "$2" ]; then
        echo "Failed: $2 does not exist."
        exit_upgrade 1
    fi
}

function get_pack_info() {
    local file="$MOUNTPATH/$MD5_FILE"
    local file_latest="$file.latest"

    if [ -f "$file_latest" ]; then
        file=$file_latest
    fi
    if [ ! -f "$file" ]; then
        exit 1
    fi

    local info=$(grep ".*ota.zip" $file)

    case $1 in
    name)
        local name=$(echo $info | awk '{print $2}')
        echo $name
        ;;
    md5)
        local md5=$(echo $info | awk '{print $1}')
        echo $md5
        ;;
    os)
        local name=$(echo $info | awk '{print $2}')
        local os=$(echo $name | cut -d'_' -f2)
        echo $os
        ;;
    version)
        local name=$(echo $info | awk '{print $2}')
        local version=$(echo $name | cut -d'_' -f3)
        echo $version
        ;;
    *)
        exit 1
        ;;
    esac
}

function check_version() {
    local issue=$(cat $ISSUE_FILE 2>/dev/null)
    if [ -z "$issue" ]; then
        echo "Can't get $ISSUE_FILE."
        return 0
    fi

    local name=$(echo $issue | awk '{print $1}')
    local version=$(echo $issue | awk '{print $2}')

    if [ $name != $1 ]; then
        echo "The OS name does not match(current:$name != $1)."
        return 2
    else
        if [ $version != $2 ]; then
            echo "The OS version does not match(current:$version != $2)."
            return 1
        else
            echo "OS name and version match."
            return 0
        fi
    fi
}

function is_use_partition_b() {
    local root_dev=$(mountpoint -n /)
    local root_dev=${root_dev%% *}

    if [ "$root_dev" = "$(realpath $ROOTFS_B)" ]; then
        return 1
    else
        return 0
    fi
}

function ps_mutex() {
    if [ -f "$CTRL_FILE.$RUN_CASE.mutex" ]; then
        echo "./upgrade.sh $RUN_CASE is running.";
        exit 1;
    fi
    echo $$ > $CTRL_FILE.$RUN_CASE.mutex
}

function ps_running() {
    if [ ! -f "$CTRL_FILE.$RUN_CASE.mutex" ]; then
        echo "./upgrade.sh $RUN_CASE is not running"
        exit 0
    fi
}

function ps_ctrl() {
    if [ -z $1 ]; then
        if [ -f $CTRL_FILE.$RUN_CASE ]; then
            cat $CTRL_FILE.$RUN_CASE
        fi
    else
        echo "$1" > $CTRL_FILE.$RUN_CASE
        echo "$1" > $RESULT_FILE.$RUN_CASE
    fi
}

function is_stopped() {
    if [ "$(ps_ctrl)" != "run" ]; then
        exit_upgrade 0
    fi
}

function kill_ps() {
    local search_string=$1
    local pid=$(ps | grep "$search_string" | grep -v grep | awk '{print $1}')
    if [ ! -z "$pid" ]; then
        kill $pid || kill -9 $pid
    fi
}

function write_boot() {
    local src=$1
    local dst=$2

    size_bytes=$(unzip -l "$full_path" | grep "$src" | awk '{print $1}')
    if [ ! -z "$size_bytes" ]; then
        size_mb=$(($size_bytes/(1024*1024)))
        let size_mb+=1

        let step+=1
        echo "Step$step: Write $src size=${size_bytes} bytes"
        read_md5=$(unzip -p $full_path md5sum.txt | grep "$src" | awk '{print $1}')
        calc_md5=$(unzip -p $full_path "$src" | md5sum | awk '{print $1}')
        part_md5=$(dd if=$dst bs=1M count=$size_mb 2>/dev/null | head -c $size_bytes | md5sum | awk '{print $1}')
        if [ "$read_md5" = "$calc_md5" ] && [ "$read_md5" != "$part_md5" ]; then
            echo "boot: $read_md5 $part_md5"
            echo 0 > /sys/block/mmcblk0boot0/force_ro
            $(unzip -p $full_path "$src" | dd of=$dst bs=1M status=progress)
            echo 1 > /sys/block/mmcblk0boot0/force_ro
        else
            echo "skip write $src"
        fi
    fi
}

case $1 in
clean)
    rm -rf $CTRL_FILE*
    echo "Success"
    ;;

latest)
    RUN_CASE=$1
    file_download="$RESULT_FILE.download"
    file_result="$RESULT_FILE.$RUN_CASE"

    # Query
    if [ ! -z "$2" ] && [ "$2" = "q" ]; then
        issue=$(cat $ISSUE_FILE 2>/dev/null | sed 's/ /,/g')
        if [ -z $issue ]; then
            issue="null,null"
        fi
        if [ -f $file_result ]; then
            echo "$(cat $file_result) $issue"
            exit 0
        fi
        echo "0,null,null $issue"
        exit 0
    fi

    if [ -z "$2" ]; then echo "Usage: $0 $RUN_CASE <url>|[q]"; exit 1; fi
    ps_mutex

    # Clean
    rm -rf $file_result.*
    ps_ctrl run
    step=0

    # Get upgrade url
    let step+=1
    echo "Step$step: Parse upgrade url"
    md5_url=$(get_upgrade_url $2)
    if [ -z $md5_url ]; then
        echo "Failed: unknown url($2)."
        exit_upgrade 1
    fi

    # Mount recovery partition
    let step+=1
    echo "Step$step: Mount partition"
    mount_recovery

    # Download md5sum.txt
    let step+=1
    echo "Step$step: Run wget $md5_url"
    md5_txt_latest=$MOUNTPATH/$MD5_FILE.latest
    rm -f $md5_txt_latest
    wget_file $md5_url $md5_txt_latest

    # Get latest version
    let step+=1
    os_name=$(get_pack_info os)
    os_version=$(get_pack_info version)
    echo "Step$step: Get latest version: $os_name $os_version"
    if [ -z "$os_name" ] || [ -z "$os_version" ]; then
        echo "Failed: get version info."
        exit_upgrade 1
    fi

    # Save url
    echo ${md5_url%/*}/$(get_pack_info name) > $MOUNTPATH/$URL_FILE

    # Check version
    let step+=1
    result=$(check_version $os_name $os_version); ret=$?;
    ps_ctrl $(echo "$ret,$os_name,$os_version")
    echo "$result"

    rm -rf $file_download*
    exit_upgrade 0
    ;;

download)
    RUN_CASE=$1
    file_result="$RESULT_FILE.$RUN_CASE"

    # Query
    if [ ! -z "$2" ] && [ "$2" = "q" ]; then
        if [ -f $file_result ]; then
            cat $file_result
            exit 0
        fi

        echo "null"
        exit 0
    fi

    if [ ! -z "$2" ]; then echo "Usage: $0 $RUN_CASE [q]"; exit 1; fi

    ps_mutex

    # Clean
    rm -rf $file_result.*
    ps_ctrl run
    step=0

    # Mount recovery partition
    let step+=1
    echo "Step$step: Mount partition"
    mount_recovery

    # Download zip file
    let step+=1
    zip=$(get_pack_info name)
    if [ -z "$zip" ]; then
        echo "Failed: can't get zip filename."
        exit_upgrade 1
    fi
    full_path=$MOUNTPATH/$zip
    full_path_latest=$full_path.latest
    if [ -f $full_path ]; then
        echo "Step$step: File already exist ($full_path)"
    else
        zip_url=$(cat "$MOUNTPATH/$URL_FILE" 2>/dev/null)
        full_path=$full_path_latest
        echo "Step$step: Download $zip_url"
        wget_file $zip_url $full_path
        echo "Download success."
    fi

    # Check md5
    let step+=1
    echo "Step$step: Check md5sum"
    read_md5=$(get_pack_info md5)
    calc_md5=$(md5sum $full_path | awk '{print $1}')
    if [ -z "$read_md5" ] || [ -z "$calc_md5" ]; then
        echo "Failed: calc md5sum."
        rm -rfv $full_path
        exit_upgrade 1
    fi
    if [ "$read_md5" != "$calc_md5" ]; then
        echo "Failed: md5sum is mismatch($read_md5 != $calc_md5)."
        rm -rfv $full_path
        exit_upgrade 1
    else
        if [ "$full_path" = "$full_path_latest" ]; then
            rm -rfv $MOUNTPATH/*.zip
            mv -fv $full_path $MOUNTPATH/$zip
        fi
        cp -f $MOUNTPATH/$MD5_FILE.latest $MOUNTPATH/$MD5_FILE 2>/dev/null
        ps_ctrl ready
        echo "Success"
    fi
    exit_upgrade 0
    ;;

start)
    RUN_CASE=$1
    file_result="$RESULT_FILE.$RUN_CASE"
    file_size="$file_result.size"
    file_stage="$file_result.stage"
    file_dd="$file_result.dd"
    file_percent="$file_result.percent"

    # Kill
    if [ ! -z "$2" ] && [ "$2" = "x" ]; then
        ps_running
        ps_ctrl stop

        kill_ps "dd of=$ROOTFS bs=1M status=progress"
        kill_ps "dd of=$ROOTFS_B bs=1M status=progress"
        kill_ps "dd if=$ROOTFS bs=1M count="
        kill_ps "dd if=$ROOTFS_B bs=1M count="

        echo "Stoped"
        exit 0
    fi

    # Query
    if [ ! -z "$2" ] && [ "$2" = "q" ]; then
        if [ ! -f $file_size ] || [ ! -f $file_dd ]; then
            echo "0,null"
            exit 0
        fi
        total_size=$(cat $file_size 2>/dev/null)
        if [ -z "$total_size" ]; then
            echo "0,null"
            exit 0
        fi

        result=$(cat $file_result 2>/dev/null)
        if [ "$result" = "run" ]; then
            write_size=$(cat $file_dd 2>/dev/null | awk 'END {print $1}')
            echo "" > $file_dd
        else
            write_size=""
        fi
        if [ -z "$write_size" ]; then
            percent=$(cat $file_percent 2>/dev/null)
        else
            percent=$(($write_size*100/$total_size))
            echo $percent > $file_percent
        fi

        total=0
        if [ -f $file_stage ]; then
            total=$(cat $file_stage 2>/dev/null)
        fi
        let total+=$percent
        if [ $total -gt 100 ]; then
            total=100
        fi

        echo "$total,$result"
        exit 0
    fi

    # Enter
    ps_mutex

    # Clean
    rm -rf $file_result.*
    ps_ctrl run
    step=0

    zip=$2
    if [ -z $zip ]; then
        # Mount recovery partition
        mount_recovery
        full_path=$(find $MOUNTPATH -maxdepth 1 -name "*ota.zip")
    else
        full_path=$(realpath "$zip")
    fi
    let step+=1
    echo "Step$step: Get $full_path"
    if [ ! -f $full_path ]; then
        echo "Failed: file not exist $full_path"
        exit_upgrade 1
    fi

    write_boot "fip.bin" /dev/mmcblk0boot0
    write_boot "boot.emmc" /dev/mmcblk0p1

    if [[ "$zip" = "*boot_ota.zip" ]]; then
        echo "Success: only write boot partition."
        echo "Please reboot to valid."
        exit_upgrade 0
    fi

    # Read md5sum
    let step+=1
    read_md5=$(unzip -p $full_path md5sum.txt | grep "$ROOTFS_FILE" | awk '{print $1}')
    echo "Step$step: Read $ROOTFS_FILE md5sum($read_md5)"
    if [ -z "$read_md5" ]; then
        echo "Failed: can't read $ROOTFS_FILE md5sum."
        exit_upgrade 1
    fi
    is_stopped

    # Write rootfs
    let step+=1
    is_use_partition_b; if [ $? -eq 1 ]; then target=$ROOTFS; else target=$ROOTFS_B; fi

    size_bytes=$(unzip -l "$full_path" | grep "$ROOTFS_FILE" | awk '{print $1}')
    size_mb=$(($size_bytes/(1024*1024)))
    # size_bytes=write_size+calc_size
    let size_bytes+=size_bytes
    echo $size_bytes > $file_size
    echo "Step$step: Writing rootfs $target size=${size_mb}MB"
    if [ $size_mb -eq 0 ]; then
        echo "Failed: file size is 0."
        exit_upgrade 1
    fi

    echo "0" > $file_stage; echo "0" > $file_dd; echo "0" > $file_percent; 
    unzip -p $full_path $ROOTFS_FILE | dd of=$target bs=1M status=progress 2>$file_dd
    if [ $? -ne 0 ]; then
        is_stopped
        echo "Failed: can't write rootfs."
        exit_upgrade 1
    fi
    is_stopped
    echo "50" > $file_stage; echo "0" > $file_dd; echo "0" > $file_percent;

    # Check md5sum
    let step+=1
    echo "Step$step: Calc partition md5sum"
    partition_md5=$(dd if=$target bs=1M count=$size_mb status=progress 2>$file_dd | md5sum | awk '{print $1}')
    if [ $? -ne 0 ]; then
        is_stopped
        echo "Failed: calc md5sum."
        exit_upgrade 1
    fi
    is_stopped
    if [ "$partition_md5" = "$read_md5" ]; then
        if [ "$target" = "$ROOTFS" ]; then
            write_upgrade_flag 0
            echo "Success: change to rootfs_a"
        elif [ "$target" = "$ROOTFS_B" ]; then
            write_upgrade_flag 1
            echo "Success: change to rootfs_b"
        fi
        echo "Please reboot to valid."
        ps_ctrl ok
    else
        echo "Failed: md5sum is mismatch($partition_md5)."
        exit_upgrade 1
    fi
    echo "100" > $file_stage; echo "0" > $file_dd; echo "0" > $file_percent; 

    exit_upgrade 0
    ;;

rollback)
    is_use_partition_b
    if [ $? -eq 1 ]; then
        write_upgrade_flag 0
        echo "Finished: rollback to rootfs_a."
    else
        write_upgrade_flag 1
        echo "Finished: rollback to rootfs_b."
    fi
    echo "Restart to valid."

    exit_upgrade 0
    ;;

recovery)
    fw_setenv factory_reset 1
    echo "Set recovery flag ok, restart to valid."
    ;;

*)
    echo "Usage: $0 {clean|latest|download|start|rollback|recovery}"
    exit 1
    ;;

esac
