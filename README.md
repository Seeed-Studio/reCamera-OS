- [1. Official reference](#1-official-reference)
- [2. How to start](#2-how-to-start)
  - [2.1. Preparation](#21-preparation)
  - [2.2 Checkout code](#22-checkout-code)
  - [2.3 Build](#23-build)
  - [2.4 How to flash](#24-how-to-flash)

## 1. Official reference

- https://developer.sophgo.com/thread/471.html

## 2. How to start

### 2.1. Preparation

- Recommended OS: Ubuntu 20.04.6 LTS
- Install dependencies

    ```bash
    sudo apt-get update
    sudo apt-get install -y build-essential ninja-build automake autoconf libtool wget curl git gcc \
        libssl-dev bc slib squashfs-tools android-sdk-libsparse-utils android-sdk-ext4-utils jq \
        cmake python3-distutils tclsh scons parallel ssh-client tree python3-dev python3-pip \
        device-tree-compiler libssl-dev ssh cpio squashfs-tools fakeroot libncurses5 flex bison
    ```

### 2.2 Checkout code

- Checkout

    ```bash
    git clone https://github.com/mingzhangqun/reCamera.git -b sg200x-reCamera
    cd reCamera
    git submodule update --init --recursive
    ```

- Update submodules

    ```bash
    ./scripts/repo_clone.sh --gitpull external/subtree.xml
    ```

### 2.3 Build

- Where is project defconfig?
  
    ```bash
    ls external/configs/
    sg2002_recamera_emmc_defconfig  sg2002_recamera_sd_defconfig  sg2002_xiao_sd_defconfig
    ```

- Build
  
    ```bash
    make ${project}
    ```
    Such as:
    ```bash
    make sg2002_recamera_emmc
    ```

- Where is build targets?

    ```bash
    ls -l output/${project}/install/soc_${project}/${project}
    ```

    Such as:
    ```bash
    cd output/sg2002_recamera_emmc/install/soc_sg2002_recamera_emmc/
    ls -l *.zip
    sg2002_reCamera_0.0.1_emmc_ota.zip
    sg2002_reCamera_0.0.1_emmc_recovery.zip
    sg2002_reCamera_0.0.1_emmc_sd_compat.zip
    sg2002_reCamera_0.0.1_emmc.zip
    ```

### 2.4 How to flash

- Flash to emmc

    - 解压：[CviBurn_v2.0_cli_windows.zip](./external/tools/CviBurn_v2.0_cli_windows.zip)
    - 解压 [2.3](#23-build) 中生成的镜像，比如：
        > ./output/sg2002_recamera_emmc/install/soc_sg2002_recamera_emmc/sg2002_reCamera_0.0.1_emmc.zip
    - 在Windows命令行中执行命令，[-m xx:xx:xx:xx:xx:xx]以太网MAC地址（可选）：
        > usb_dl.exe -c cv181x -s linux -i ..\sg2002_reCamera_0.0.1_emmc [-m xx:xx:xx:xx:xx:xx]

- Flash to sdcard

    [Reference to](./build/README.md)