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
DEFCONFIG="radioactive_defconfig"

# Kernel Details
BASE_RADIOACTIVE_VER="RADIOACTIVE_REBORN"
VER="_V2.00"
RADIOACTIVE_VER="$BASE_RADIOACTIVE_VER$VER"

# Vars
export LOCALVERSION=~`echo $RADIOACTIVE_VER`
export CROSS_COMPILE=${HOME}/Android/toolchains/uber5.2/bin/arm-eabi-
export ARCH=arm
export SUBARCH=arm
export KBUILD_BUILD_USER=R.Cuenca
export KBUILD_BUILD_HOST=acuicultor

# Paths
KERNEL_DIR=`pwd`
REPACK_DIR="${HOME}/Android/AK-OnePone-AnyKernel2"
PATCH_DIR="${HOME}/Android/AK-OnePone-AnyKernel2/patch"
MODULES_DIR="${HOME}/Android/AK-OnePone-AnyKernel2/modules"
ZIP_MOVE="${HOME}/Android/releases"
ZIMAGE_DIR="${HOME}/Android/AK-OnePone-AnyKernel2"

# Functions
function clean_all {
		rm -rf $MODULES_DIR/*
		cd $REPACK_DIR
		rm -rf $KERNEL
		rm -rf $DTBIMAGE
		git reset --hard > /dev/null 2>&1
		git clean -f -d > /dev/null 2>&1
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
		$REPACK_DIR/tools/dtbToolCM -2 -o $REPACK_DIR/$DTBIMAGE -s 2048 -p scripts/dtc/ arch/arm/boot/
}

function make_zip {
		cd $REPACK_DIR
		zip -r9 `echo $RADIOACTIVE_VER`.zip *
		mv  `echo $RADIOACTIVE_VER`.zip $ZIP_MOVE
		cd $KERNEL_DIR
}


DATE_START=$(date +"%s")

echo -e "${green}"
echo "RADIOACTIVE Kernel Creation Script:"
echo "                                   "
echo "           TEAM                    "
echo "                NUCLEAR            "
echo "                                   "
echo "        RADIOACTIVE KERNEL         "
echo "                                   "
echo

echo "---------------"
echo "Kernel Version:"
echo "---------------"

echo -e "${red}"; echo -e "${blink_red}"; echo "$RADIOACTIVE_VER"; echo -e "${restore}";

echo -e "${green}"
echo "-----------------"
echo "Making RADIOACTIVE Kernel:"
echo "-----------------"
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
echo "-------------------"
echo -e "${restore}"

DATE_END=$(date +"%s")
DIFF=$(($DATE_END - $DATE_START))
echo "Time: $(($DIFF / 60)) minute(s) and $(($DIFF % 60)) seconds."
echo

