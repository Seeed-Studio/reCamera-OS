#!/bin/sh
${CVI_SHOPTS}
#
# Start to insert kernel modules
#
insmod /mnt/system/ko/cv181x_sys.ko
insmod /mnt/system/ko/cv181x_base.ko
insmod /mnt/system/ko/cv181x_rtos_cmdqu.ko
insmod /mnt/system/ko/cv181x_fast_image.ko
insmod /mnt/system/ko/cvi_mipi_rx.ko
insmod /mnt/system/ko/snsr_i2c.ko
insmod /mnt/system/ko/cv181x_vi.ko vi_log_lv=1
insmod /mnt/system/ko/cv181x_vpss.ko vpss_log_lv=1
insmod /mnt/system/ko/cv181x_dwa.ko
insmod /mnt/system/ko/cv181x_vo.ko vo_log_lv=1
insmod /mnt/system/ko/cv181x_mipi_tx.ko
insmod /mnt/system/ko/cv181x_rgn.ko

#insmod /mnt/system/ko/cv181x_wdt.ko
insmod /mnt/system/ko/cv181x_clock_cooling.ko

insmod /mnt/system/ko/cv181x_tpu.ko
insmod /mnt/system/ko/cv181x_vcodec.ko
insmod /mnt/system/ko/cv181x_jpeg.ko
insmod /mnt/system/ko/cvi_vc_driver.ko MaxVencChnNum=9 MaxVdecChnNum=9
#insmod /mnt/system/ko/cv181x_rtc.ko
insmod /mnt/system/ko/cv181x_ive.ko

insmod /mnt/system/ko/cfg80211.ko
insmod /mnt/system/ko/brcmutil.ko
insmod /mnt/system/ko/brcmfmac.ko

echo 3 > /proc/sys/vm/drop_caches
dmesg -n 4

#usb hub control
#/etc/uhubon.sh host

exit $?
