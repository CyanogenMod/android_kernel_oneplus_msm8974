#!/bin/bash

# Bash Color
green='\033[01;32m'
red='\033[01;31m'
blink_red='\033[05;31m'
restore='\033[0m'

clear

# Resources
THREAD="-j$(grep -c ^processor /proc/cpuinfo)"
KERNEL="zImage"
DTBIMAGE="dtb"
DEFCONFIG="cyanogenmod_bacon_defconfig"

# Kernel Details
BASE_AK_VER="0.1"
VER=".CM12.1"
AK_VER="$BASE_AK_VER$VER"

# Vars

export CROSS_COMPILE=${HOME}/new/arm-eabi-4.9a15/bin/arm-eabi-
export ARCH=arm
export SUBARCH=arm
export KBUILD_BUILD_USER=jgcaap
export KBUILD_BUILD_HOST=kernel

# Paths
KERNEL_DIR=`pwd`
REPACK_DIR="${HOME}/new/anykernel"
PATCH_DIR="${HOME}/new/anykernel"
MODULES_DIR="${HOME}/new/modules"
ZIP_MOVE="${HOME}/new/out"
ZIMAGE_DIR="${HOME}/new/kernel/arch/arm/boot"

# Functions
function clean_all {
		rm -rf $MODULES_DIR/*
		cd $REPACK_DIR
		rm -rf $KERNEL
		rm -rf $DTBIMAGE
		cd $KERNEL_DIR
		echo
		make clean && make mrproper
}

function make_kernel {
		echo
		make $DEFCONFIG
		make $THREAD
		cp -vr $ZIMAGE_DIR/$KERNEL $REPACK_DIR
}

function make_modules {
		rm `echo $MODULES_DIR"/*"`
		find $KERNEL_DIR -name '*.ko' -exec cp -v {} $MODULES_DIR \;
}

function make_dtb {
		/home/jorge/new/anykernel/tools/dtbToolCM -2 -o /home/jorge/new/anykernel/dtb -s 2048 -p /home/jorge/new/kernel/scripts/dtc/ /home/jorge/new/kernel/arch/arm/boot/
}

function make_zip {
		cd $REPACK_DIR
		zip -r9 newKernel-CM12-"$VARIANT".zip *
		mv newKernel-CM12-"$VARIANT".zip $ZIP_MOVE
		cd $KERNEL_DIR
}


DATE_START=$(date +"%s")

echo -e "${green}"
echo "New Kernel Creation Script:"
echo -e "${restore}"

while read -p "Do you want to clean stuffs (y/n)? " cchoice
do
case "$cchoice" in
	y|Y )
		clean_all
		echo
		echo "All Cleaned now."
		break
		;;
	n|N )
		break
		;;
	* )
		echo
		echo "Invalid try again!"
		echo
		;;
esac
done

echo

while read -p "Do you want to build kernel (y/n)? " dchoice
do
case "$dchoice" in
	y|Y)
		make_kernel
		make_dtb
		make_modules
		make_zip
		break
		;;
	n|N )
		break
		;;
	* )
		echo
		echo "Invalid try again!"
		echo
		;;
esac
done

echo -e "${green}"
echo "-------------------"
echo "Build Completed in:"
oecho "-------------------"
echo -e "${restore}"

DATE_END=$(date +"%s")
DIFF=$(($DATE_END - $DATE_START))
echo "Time: $(($DIFF / 60)) minute(s) and $(($DIFF % 60)) seconds."
echo
mv ~/new/out/newKernel-CM12-.zip ~/files/oneplusone/kernel/newKernel-CM13.0-3.60.zip
#/etc/script/md5.sh
