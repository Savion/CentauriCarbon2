#!/bin/bash

# ./mcu_build.sh -b bed_sensor -v 00.00.00.03 -f

# ./mcu_build.sh -b mcu_gd32 -v 00.00.00.02 -f
# ./mcu_build.sh -b toolhead_gd32 -v 00.00.00.02 -f
# ./mcu_build.sh -b bed_sensor -v 00.00.00.02 -f


# -b 指定板子配置,配置存储于mcu_config文件夹
# -v 指定版本号,格式必须为00.00.00.00
# -f 生成bootloader并同时合成factory.bin文件
# -m 指定bootloader.bin文件与app.bin合成factory.bin文件

workdir=$(cd $(dirname $0); pwd)
build_bootloader="false"
merge="false"
bootloader_bin_file=""
boot_compile_time=""
app_compile_time=""
board=""
version=""

# 1. 获取脚本参数
while getopts "b:fm:v:" opt; do
    case $opt in
        b) 
            board=$OPTARG
            if [ "$board" == "" ]; then
                echo 'must specify board: XXX'
                exit
            fi
            ;;
        f)
            build_bootloader="true"
            merge="true"
            ;;
        m)
            bootloader_bin_file=$OPTARG
            merge="true"
            ;;
        v)
            version=$OPTARG
            if [ "$version" == "" ]; then
                echo 'must specify version: AA.BB.CC'
                exit
            fi
            ;;
        ?)
            echo '-b config name'
            echo '-f build bootloader and factory bin'
            echo '-v version'
            echo '-m bootloader.bin'
            exit 1
            ;;
    esac
done

# 2. 检查配置文件是否存在

if [ -e "$workdir/mcu_config/$board.config" ]; then
    echo "select build $board"
    cfg="$workdir/mcu_config/"$board".config"
else
    echo "can't found configfile: "$workdir/mcu_config/$board.config""
    exit -1
fi

if [ "$build_bootloader" == "true" ]; then
    if [ -e "$workdir/mcu_config/"$board"_bootloader.config" ]; then
        echo "select build "$board"_bootloader"
        boot_cfg="$workdir/mcu_config/"$board"_bootloader.config"
    else
        echo "can't found configfile: "$workdir/mcu_config/"$board"_bootloader.config""
        exit -1
    fi
fi

echo "cfg: $cfg"
echo "boot_cfg: $boot_cfg"

# 3. 编译bootloader
if [ "$build_bootloader" == "true" ]; then
    if [ -e $workdir/.config ]; then
        mv $workdir/.config $workdir/.config.old
    fi
    cp $boot_cfg $workdir/.config
    mcu_version="$board"-"$version"-bootloader
    header_file=$workdir/src/version.h
    sed -i "s|#define MCU_VERSION \".*\"|#define MCU_VERSION \"$mcu_version\"|" "$header_file"
    make -j16
    if [ $? -ne 0 ]; then
        echo "build bootloader failed"
        exit
    fi
    boot_compile_time=$(python3 $workdir/scripts/firmware_dump_timestamp.py $workdir/out/klipper.dict)
    bootloader_bin_file=$workdir/out/"$board"-"$boot_compile_time"-"$version"-bootloader.bin
    cp $workdir/out/klipper.bin $bootloader_bin_file
fi



# 4. 编译app
if [ -e $workdir/.config ]; then
    mv $workdir/.config $workdir/.config.old
fi
cp $cfg $workdir/.config

mcu_version="$board"-"$version"-app
header_file=$workdir/src/version.h
sed -i "s|#define MCU_VERSION \".*\"|#define MCU_VERSION \"$mcu_version\"|" "$header_file"
make -j16
if [ $? -ne 0 ]; then
    echo "build app failed"
    exit
fi
app_compile_time=$(python3 $workdir/scripts/firmware_dump_timestamp.py $workdir/out/klipper.dict)
app_bin_file=$workdir/out/"$board"-"$app_compile_time"-"$version"-app.bin 
cp $workdir/out/klipper.bin $app_bin_file



# 5. 打包app
# python3 $workdir/scripts/firmware_package.py $board $app_compile_time $version $app_bin_file $workdir/out/"$board"_"$app_compile_time"_"$version"_release.bin

# 6. 合并bootloader+app=factory
if [ "$merge" == "true" ]; then
    # bootloader占用16K 剩余部分都给APP
    python3 $workdir/scripts/firmware_merge.py 0x4000 $board $app_compile_time $version $bootloader_bin_file $app_bin_file $workdir/out/"$board"-"$app_compile_time"-"$version"-factory.bin
fi
