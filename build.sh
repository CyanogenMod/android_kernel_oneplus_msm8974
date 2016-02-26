#!/bin/bash
# simple bash script for executing build

RDIR=$(pwd)

TOOLCHAIN=/home/jc/build/toolchain/arm-cortex_a15-linux-gnueabihf-linaro_4.9.4-2015.06

THREADS=5

[ -z $VERSION ] && \
# version number
VERSION=$(cat $RDIR/VERSION)

export ARCH=arm
export CROSS_COMPILE=$TOOLCHAIN/bin/arm-cortex_a15-linux-gnueabihf-
export LOCALVERSION=$VERSION

cd $RDIR

[ -z "$1" ] && {
	DEFCONFIG=nethunter_defconfig
} || {
	DEFCONFIG=${1}_defconfig
}

FILE=arch/arm/configs/$DEFCONFIG

[ -e "$FILE" ] || {
	echo "Defconfig not found: $FILE"
	exit -1
}

BUILD_DTB_IMG()
{
	echo "Generating dtb.img..."
	$RDIR/scripts/dtbTool/dtbTool -o build/arch/arm/boot/dtb.img build/arch/arm/boot/ -s 2048
}

echo "Cleaning build..."
rm -rf build
mkdir build

make -C $RDIR O=build $DEFCONFIG
echo "Starting build..."
make -C $RDIR O=build -j"$THREADS" CONFIG_NO_ERROR_ON_MISMATCH=y && BUILD_DTB_IMG

echo "Done."
