## 0.0.8 (2024-10-22)

### sg2002_recamera_emmc

- New features:
    - auto swapon /userdata/.swapfile
    - supervior
        - add the operation that AP will automatically turn on or off according to the status of WiFi
        - split the wifi scan into two operations: scan wifi and get scan results
        - add a judgement that an upgrade is in progress

- Fix bugs:
    - solve some supervisor bug
    - solve some sscma-node bug

## 0.0.7 (2024-10-16)

### sg2002_recamera_emmc

- New features:
    - reduce ION_SIZE to 50M
    - kernel support swap
    - kernel support advise syscalls
    - update c-ares to 1.32.2
    - update libuv to 1.48.0
    - update nodejs to 22.8.0
    - update node-red to 4.0.0
    - sd supports hotplug
    - close swupdate auto start (`sudo /usr/lib/swupdate/swupdate.sh`)

- Fix bugs:
    - solve some supervisor bug
    - solve some sscma-node bug

## 0.0.6 (2024-10-12)

### sg2002_recamera_emmc

- New features:
    - sensor auto detection (ov5647/sc530ai)
    - supports restore to factory
    - get mac & sn from efuse
    - add sscma-node program
    - supervisor
        - support https service
        - add service status detection
        - add a feature for file management
        - add wifi password verification operation

- Fix bugs:
    - solve some compilation issues
    - solve some supervisor bugs

## 0.0.5 (2024-09-25)

### sg2002_recamera_emmc

- New features:
    - update node-red interface style
    - supervisor
        - add APIs for uploading models and getting model information
        - allow CORS

- Fix bugs:
    - fix the problem that some configuration files did not exist
    - node-red start failed when reset system
    - solve some supervisor bugs

- Docs:
    - add compilation notes

## 0.0.4 (2024-09-12)

### sg2002_recamera_emmc

- New features:
    - buildin supervisor
    - buildin npm@8.11.0 and node-red@v3.1.11
    - update ov5647 isp params
    - supports rootfs overlay (/bin /etc /lib /home /root /usr /var)
    - add recamera as default user
    - supports sudo
    - upgrade icu to 73-2

- Fix bugs:
    - upgrade.sh checksum failed
    - fixed ov5647 mirror

## 0.0.3 (2024-08-30)

### sg2002_recamera_emmc
- update sdk upstream (6cd7a5b)
- support more buildroot packages (mosquitto/avahi/opkg/live555)
- remove reCamera app
- change ota (not compatible with last version) and supports swupdate
- use ncm replace of rndis
- expand rootfs size to 512M, rootfs default readonly (rootfs_rw (on|off))
- auto mount /dev/mmcblk0p6 to /userdata
- update nodejs to 17.9.1

## 0.0.2 (2024-06-24)

### sg2002_recamera_emmc
- update sdk upstream
- support booting from sd (sg2002_reCamera_0.0.2_emmc_sd_compat.zip)
- support sd recovery (sg2002_reCamera_0.0.2_emmc_recovery.zip)

## 0.0.1 (2024-06-21)

### sg2002_recamera_emmc
- First beta release
- SDK: supported emmc/sdcard/leds/wifi/uart/ethernet/ov5647 sensor
- APP: complete basic functions(Overview/Security/Newwork/Terminal/Setting)
