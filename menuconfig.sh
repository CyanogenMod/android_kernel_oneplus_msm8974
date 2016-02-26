#!/bin/bash
# simple script for executing menuconfig

RDIR=$(pwd)

export ARCH=arm

cd $RDIR

[ -z "$1" ] && {
	DEFCONFIG=nethunter_defconfig
} || {
	DEFCONFIG=${1}_defconfig
}

FILE=arch/arm/configs/$DEFCONFIG

echo "Cleaning build..."
rm -rf build
mkdir build

make -s -i -C $RDIR O=build $DEFCONFIG menuconfig
echo "Showing differences between old config and new config"
echo "-----------------------------------------------------"
command -v colordiff >/dev/null 2>&1 && {
	diff -Bwu --label "old config" "$FILE" --label "new config" build/.config | colordiff
} || {
	diff -Bwu --label "old config" "$FILE" --label "new config" build/.config
	echo "-----------------------------------------------------"
	echo "Consider installing the colordiff package to make diffs easier to read"
}
echo "-----------------------------------------------------"
echo -n "Are you satisfied with these changes? Y/N: "
read option
case $option in
y|Y)
	cp build/.config "$FILE"
	echo "Copied new config to $FILE"
	;;
*)
	echo "That's unfortunate"
	;;
esac

echo "Cleaning build..."
rm -rf build

echo "Done."
