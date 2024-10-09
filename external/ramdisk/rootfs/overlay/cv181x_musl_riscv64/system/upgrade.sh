#!/bin/sh

MD5_FILE=sg2002_recamera_emmc_md5sum.txt
URL_FILE=url.txt
ZIP_FILE=zip.txt

RECV_PARTITION=/dev/mmcblk0p5
ROOTFS=/dev/mmcblk0p3
ROOTFS_B=/dev/mmcblk0p4
ROOTFS_FILE=rootfs_ext4.emmc

PERCENTAGE=0
PERCENTAGE_FILE=/tmp/upgrade.percentage
CTRL_FILE=/tmp/upgrade.ctrl
VERSION_FILE=version.txt

function clean_up() {
    if [ ! -z $MOUNTPATH ]; then
        umount $MOUNTPATH
        rm -rf $MOUNTPATH
    fi
    rm -rf $CTRL_FILE
}

function write_percent() {
    echo $PERCENTAGE > $PERCENTAGE_FILE

    if [ -f $CTRL_FILE ]; then
        stop_it=$(cat $CTRL_FILE)
        if [ "$stop_it" = "stop" ]; then
            echo "Stop upgrade."
            clean_up
            exit 2
        fi
    fi
}

function exit_upgrade() {
    write_percent
    clean_up
    exit $1
}

function write_upgrade_flag() {
    fw_setenv use_part_b $1
    fw_setenv boot_cnt 0
    fw_setenv boot_failed_limits 5
    fw_setenv boot_rollback
}

function get_upgrade_url() {
    local url=$1
    local full_url=$url

    if [[ $url =~ .*\.txt$ ]]; then
        full_url=$url
    else
        url=$(curl -skLi $url | grep -i '^location:' | awk '{print $2}' | sed 's/^"//;s/"$//')
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

function check_version() {
    issue=""
    if [ -f "/etc/issue" ]; then
        issue=$(cat /etc/issue)
    fi
    if [ -z "$issue" ]; then
        echo "10,No issue file."
        return
    fi

    os_name=$(echo $issue | awk '{print $1}')
    os_version=$(echo $issue | awk '{print $2}')

    if [ $os_name != $1 ]; then
        echo "11,OS name is not match(current:$os_name != $1)."
        return
    else
        if [ $os_version != $2 ]; then
            echo "12,OS version is not match(current:$os_version != $2). "
            return
        else
            echo "0,OS version is match."
        fi
    fi
}

function mount_recovery() {
    fs_type=$(blkid -o value -s TYPE $RECV_PARTITION)
    if [ "$fs_type" != "ext4" ]; then
        mkfs.ext4 $RECV_PARTITION

        # check again
        fs_type=$(blkid -o value -s TYPE $RECV_PARTITION)
        if [ "$fs_type" != "ext4" ]; then
            echo "Recovery partition is not ext4!"
            return 1
        fi
    fi

    mount $RECV_PARTITION $MOUNTPATH
    if mount | grep -q "$RECV_PARTITION on $MOUNTPATH type"; then
        echo "Mount $RECV_PARTITION on $MOUNTPATH ok."
        return 0
    else
        echo "Mount $RECV_PARTITION on $MOUNTPATH failed."
        return 2
    fi
}

function wget_file() {
    wget -T 10 -t 3 -q --no-check-certificate $1 -O $2
}

function is_use_partition_b() {
    root_dev=$(mountpoint -n /)
    root_dev=${root_dev%% *}

    if [ "$root_dev" = "$(realpath $ROOTFS_B)" ]; then
        return 1
    else
        return 0
    fi
}

trap `rm -rf $CTRL_FILE` SIGINT

case $1 in
latest)
    if [ -z "$2" ]; then echo "Usage: $0 latest <url>"; exit 1; fi
    if [ -f $CTRL_FILE ]; then echo "Upgrade is running."; exit 1; fi
    echo "" > $CTRL_FILE

    step=0
    PERCENTAGE=0
    write_percent

    let step+=1
    echo "Step$step: Get upgrade url"
    url_md5=$(get_upgrade_url $2)
    if [ -z $url_md5 ]; then
        echo "Step$step: failed."
        PERCENTAGE="1,Unkown url."
        exit_upgrade 1
    fi

    let step+=1
    echo "Step$step: Mount partition"
    MOUNTPATH=$(mktemp -d)
    result=$(mount_recovery)
    if [ $? -ne 0 ]; then
        echo "Step$step: failed."
        PERCENTAGE=2,"$result"
        exit_upgrade 1
    fi

    let step+=1
    echo "Step$step: wget: $url_md5"
    md5_txt=$MOUNTPATH/$MD5_FILE
    wget_file $url_md5 $md5_txt

    zip_txt=$MOUNTPATH/$ZIP_FILE
    echo $(cat $md5_txt | grep ".*ota.*\.zip") > $zip_txt
    rm -rf $md5_txt

    zip=$(cat $zip_txt | awk '{print $2}')
    if [ -z "$zip" ]; then
        echo "Step$step: failed."
        rm -rfv $zip_txt
        PERCENTAGE="3,Get file list failed."
        exit_upgrade 1
    fi

    os_name=$(echo "$zip" | awk -F'_' '{print $2}')
    os_version=$(echo "$zip" | awk -F'_' '{print $3}')
    let step+=1
    echo "Step$step: the latest $os_name $os_version"
    if [ -z "$os_name" ] || [ -z "$os_version" ]; then
        echo "Step$step: failed."
        rm -rfv $zip_txt
        PERCENTAGE="4,Unknown file name $zip."
        exit_upgrade 1
    fi

    echo "$os_name $os_version" > $MOUNTPATH/$VERSION_FILE
    result=$(check_version $os_name $os_version)
    PERCENTAGE=$result
    echo "check_version: $result"

    echo "${url_md5%/*}/$zip" > $MOUNTPATH/$URL_FILE
    exit_upgrade 0
    ;;

start)
    if [ -f $CTRL_FILE ]; then echo "Upgrade is running."; exit 1; fi
    echo "" > $CTRL_FILE

    step=0
    PERCENTAGE=0
    write_percent

    let step+=1
    echo "Step$step: Mount recovery partition"
    MOUNTPATH=$(mktemp -d)
    result=$(mount_recovery)
    if [ $? -eq 0 ]; then
        PERCENTAGE=10
    else
        echo "Step$step: failed."
        PERCENTAGE=10,"$result"
        exit_upgrade 1
    fi
    write_percent

    if [ -z $2 ]; then
        let step+=1
        echo "Step$step: Get upgrade url"
        PERCENTAGE=20
        url_txt=$MOUNTPATH/$URL_FILE
        if [ ! -f $url_txt ]; then
            echo "Step$step: failed."
            PERCENTAGE="20,Url.txt not exist."
            exit_upgrade 1
        fi
        full_url=$(cat $url_txt)
        if [ -z "$full_url" ]; then
            echo "Step$step: failed: $URL_FILE is empty."
            PERCENTAGE="20,Url.txt file is empty."
            exit_upgrade 1
        fi
        echo "url: $full_url"

        let step+=1
        echo "Step$step: Read $ZIP_FILE"
        zip_txt=$MOUNTPATH/$ZIP_FILE
        if [ ! -f $zip_txt ]; then
            echo "Step$step: $ZIP_FILE file not exist."
            PERCENTAGE="30,Zip.txt not exist."
            exit_upgrade 1
        fi
        md5=$(cat $zip_txt | awk '{print $1}')
        zip=$(cat $zip_txt | awk '{print $2}')
        echo "zip=$zip"
        echo "md5=$md5"
        if [ -z "$md5" ] || [ -z "$zip" ]; then
            echo "Step$step: failed."
            PERCENTAGE="30,Zip.txt file is empty."
            exit_upgrade 1
        fi
        write_percent

        let step+=1
        echo "Step$step: Download $zip"
        full_path=$MOUNTPATH/$zip
        rm -fv $MOUNTPATH/*.zip
        wget_file $full_url $full_path
        if [ -f $full_path ]; then
            PERCENTAGE=40
        else
            echo "Step$step: failed."
            PERCENTAGE=40,"Download failed."
            exit_upgrade 1
        fi
        write_percent

        zip_md5=$(md5sum $full_path | awk '{print $1}')
        let step+=1
        echo "Step$step: Check $zip md5: $zip_md5"
        if [ "$md5" != "$zip_md5" ]; then
            echo "Step$step: failed."
            PERCENTAGE=50,"Package md5 check failed."
            exit_upgrade 1
        fi
        write_percent
    else
        if [ $2 = "." ]; then
            full_path=$(ls -t $MOUNTPATH | grep -E '.*\.zip' | head -n 1)
            full_path=$MOUNTPATH/$full_path
        else
            full_path=$2
        fi
        full_path=$(realpath $full_path)

        let step+=1
        echo "Step$step: Use local: $full_path"
    fi

    read_md5=$(unzip -p $full_path md5sum.txt | grep "$ROOTFS_FILE" | awk '{print $1}')
    let step+=1
    echo "Step$step: Read $ROOTFS_FILE md5: $read_md5"
    PERCENTAGE=60
    write_percent

    is_use_partition_b
    if [ $? -eq 1 ]; then
        target=$ROOTFS
    else
        target=$ROOTFS_B
    fi
    let step+=1
    file_size=$(unzip -l "$full_path" | grep "$ROOTFS_FILE" | awk '{print $1}')
    let file_size/=1024*1024
    echo "Step$step: Writing rootfs $target size=${file_size}MB"
    if [ $file_size -eq 0 ]; then
        echo "Step$step: failed."
        PERCENTAGE=70,"Read file size is 0."
        exit_upgrade 1
    fi
    unzip -p $full_path $ROOTFS_FILE | dd of=$target bs=1M
    PERCENTAGE=70
    write_percent

    let step+=1
    echo "Step$step: Calc $target md5"
    partition_md5=$(dd if=$target bs=1M count=$file_size 2>/dev/null | md5sum | awk '{print $1}')
    echo "$target md5: $partition_md5"
    PERCENTAGE=80
    write_percent

    let step+=1
    echo "Step$step: Check $target md5"
    if [ "$partition_md5" = "$read_md5" ]; then
        PERCENTAGE=90
        if [ "$target" = "$ROOTFS" ]; then
            write_upgrade_flag 0
            echo "Step$step: change to rootfs A"
        elif [ "$target" = "$ROOTFS_B" ]; then
            write_upgrade_flag 1
            echo "Step$step: change to rootfs B"
        fi
    else
        echo "Step$step: failed: md5 not match."
        PERCENTAGE=90,"Partition md5 check failed."
        exit_upgrade 1
    fi
    sync
    sleep 1

    PERCENTAGE=100
    echo "Finished!"
    exit_upgrade 0
    ;;

rollback)
    is_use_partition_b
    if [ $? -eq 1 ]; then
        write_upgrade_flag 0
        echo "Finished: rollback to rootfs A."
    else
        write_upgrade_flag 1
        echo "Finished: rollback to rootfs B."
    fi
    echo "Restart to valid."
    ;;

stop)
    echo "stop" > $CTRL_FILE
    ;;

query)
    if [ -f $PERCENTAGE_FILE ]; then
        cat $PERCENTAGE_FILE
    else
        echo "0"
    fi
    ;;

recovery)
    echo "Set recovery flag ok."
    fw_setenv factory_reset 1
    ;;

*)
    echo "Usage: $0 {latest|start|rollback|stop|query|recovery}"
    exit 1
    ;;

esac
