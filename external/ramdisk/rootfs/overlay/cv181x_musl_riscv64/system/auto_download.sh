#!/bin/bash

URL_DEFAULT="https://github.com/Seeed-Studio/reCamera-OS/releases/latest"
URL_BAKEUP1=""

URL_LIST=("$URL_DEFAULT" "$URL_BAKEUP1")

UPGRADE="/mnt/system/upgrade.sh"
function get_latest() {
    local url=$1
    $UPGRADE latest "$url" > /dev/null 2>&1
    $UPGRADE latest q
}

function get_download() {
    local url=$1
    $UPGRADE download > /dev/null 2>&1
    $UPGRADE download q
}

DEFAULT_SLEEP=60

# clean all
$UPGRADE clean > /dev/null 2>&1

while true; do
    # check custom url
    custom_url="$(cat /etc/upgrade 2>&1)"
    if [ ! -z "$custom_url" ]; then
        custom=$(echo "$custom_url" | awk -F',' '{print $1}')
        url=$(echo "$custom_url" | awk -F',' '{print $2}')
    fi
    if [ $custom -eq "1" ]; then
        result=$(get_latest "$url")
        ret=$(echo "$result" | awk '{print $1}')
        if [ "$ret" == "error" ]; then
            continue;
        fi
        need_download=$(echo "$ret" | awk -F',' '{print $1}')
    else
        # get latest
        for url in "${URL_LIST[@]}"; do
            if [ -z "$url" ]; then continue; fi

            result=$(get_latest "$url")
            ret=$(echo "$result" | awk '{print $1}')
            if [ "$ret" == "error" ]; then
                continue;
            fi

            need_download=$(echo "$ret" | awk -F',' '{print $1}')
            if [ ! -z $need_download ] && [ "$need_download" != "0" ]; then
                break;
            fi
        done
    fi
    if [ ! -z $need_download ] && [ "$need_download" != "0" ]; then
        break;
    fi

    sleep $DEFAULT_SLEEP
done

while true; do
    result=$(get_download)
    if [ "$result" = "ready" ]; then
        break;
    fi

    sleep $DEFAULT_SLEEP
done

exit 0