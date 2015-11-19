#!/bin/bash

# RADIOACTIVE Kernel Universal Build Script
#
# Version 1.1, 19.03.2015
#
# 

#######################################
# Parameters to be configured manually
#######################################

RADIOACTIVE_VERSION="_V2.26"
EXTENDED_CMDLINE="androidboot.selinux=permissive"

TOOLCHAIN="/opt/toolchains/arm-eabi-4.8/bin/arm-eabi-"

COMPILE_DTB="y"
MODULES_IN_SYSTEM="y"
KERNEL_SAMSUNG="n"
OUTPUT_FOLDER=""

DEFCONFIG="radioactive_defconfig"
DEFCONFIG_VARIANT=""

MKBOOTIMG_CMDLINE="console=ttyHSL0,115200,n8 androidboot.hardware=bacon user_debug=31 msm_rtb.filter=0x3F ehci-hcd.park=3"
MKBOOTIMG_BASE="0x00000000"
MKBOOTIMG_PAGESIZE="2048"
MKBOOTIMG_RAMDISK_OFFSET="0x02000000"
MKBOOTIMG_TAGS_OFFSET="0x01e00000"

BOOT_PARTITION="/dev/block/platform/msm_sdcc.1/by-name/boot"
SYSTEM_PARTITION="/dev/block/platform/msm_sdcc.1/by-name/system"

ASSERT_1="bacon"
ASSERT_2="A0001"
ASSERT_3=""
ASSERT_4=""
ASSERT_5=""
ASSERT_6=""
ASSERT_7=""
ASSERT_8=""
ASSERT_9=""
ASSERT_10=""
ASSERT_11=""
ASSERT_12=""

FINISH_MAIL_TO=""

SMB_SHARE_KERNEL=""
SMB_FOLDER_KERNEL=""
SMB_AUTH_KERNEL=""

SMB_SHARE_BACKUP=""
SMB_FOLDER_BACKUP=""
SMB_AUTH_BACKUP=""

NUM_CPUS="5"	# number of cpu cores used for build


#######################################
# automatic parameters, do not touch !
#######################################

COLOR_RED="\033[0;31m"
COLOR_GREEN="\033[1;32m"
COLOR_NEUTRAL="\033[0m"

SOURCE_PATH=$PWD
cd ..
ROOT_PATH=$PWD
ROOT_DIR_NAME=`basename "$PWD"`
cd $SOURCE_PATH

BUILD_PATH="$ROOT_PATH/build"
REPACK_PATH="$ROOT_PATH/repack"

TOOLCHAIN_COMPILE=`grep "^CROSS_COMPILE" $SOURCE_PATH/Makefile`

RADIOACTIVE_DATE=$(date +%Y%m%d)
GIT_BRANCH=`git branch | grep --color=none "*"`


# overwrite settings with custom file
. $ROOT_PATH/x-settings.sh

RADIOACTIVE_FILENAME="Radioactive-Kernel-$RADIOACTIVE_VERSION"

if [ "y" == "$MODULES_IN_SYSTEM" ]; then
	MODULE_PATH="system/lib/modules"
else
	MODULE_PATH="ramdisk/lib/modules"
fi


#####################
# internal functions
#####################

step0_copy_code()
{
	echo -e $COLOR_GREEN"\n0 - copy code\n"$COLOR_NEUTRAL

	# remove old build folder and create empty one
	rm -r -f $BUILD_PATH
	mkdir $BUILD_PATH

	# copy code from source folder to build folder
	# (usage of * prevents .git folder to be copied)
	cp -r $SOURCE_PATH/* $BUILD_PATH

	# Replace version information in mkcompile_h with the one from x-settings.sh
	sed "s/\`echo \$LINUX_COMPILE_BY | \$UTS_TRUNCATE\`/Radioactive-Kernel$RADIOACTIVE_VERSION/g" -i $BUILD_PATH/scripts/mkcompile_h
	sed "s/\`echo \$LINUX_COMPILE_HOST | \$UTS_TRUNCATE\`/acuicultor/g" -i $BUILD_PATH/scripts/mkcompile_h
}

step1_make_clean()
{
	echo -e $COLOR_GREEN"\n1 - make clean\n"$COLOR_NEUTRAL
	
	# jump to build path and make clean
	cd $BUILD_PATH
	make clean
}

step2_make_config()
{
	echo -e $COLOR_GREEN"\n2 - make config\n"$COLOR_NEUTRAL
	echo
	export ARCH=arm
        export CROSS_COMPILE=~/Android/toolchains/uber5.2-cortex-a15/bin/arm-eabi-
	# build make string depending on if we need to compile to an output folder
	# and if we need to have a defconfig variant
	MAKESTRING="arch=arm $DEFCONFIG"
	
	if [ ! -z "$OUTPUT_FOLDER" ]; then
		rm -rf $BUILD_PATH/output
		mkdir $BUILD_PATH/output
		MAKESTRING="O=$OUTPUT_FOLDER $MAKESTRING"
	fi

	if [ ! -z "$DEFCONFIG_VARIANT" ]; then
		MAKESTRING="$MAKESTRING VARIANT_DEFCONFIG=$DEFCONFIG_VARIANT"
	fi
	
	# jump to build path and make config
	cd $BUILD_PATH
	echo "Makestring: $MAKESTRING"
	make $MAKESTRING
}

step3_compile()
{
	echo -e $COLOR_GREEN"\n3 - compile\n"$COLOR_NEUTRAL

	TIMESTAMP1=$(date +%s)
	
	# jump to build path
	cd $BUILD_PATH

	# compile source
	if [ -z "$OUTPUT_FOLDER" ]; then
		make -j$NUM_CPUS 2>&1 |tee ../compile.log
	else
		make -j$NUM_CPUS O=$OUTPUT_FOLDER 2>&1 |tee ../compile.log
	fi

	# compile dtb if required
	if [ "y" == "$COMPILE_DTB" ]; then
		echo -e ">>> compiling DTB\n"
		echo
		
		# Compile dtb (device tree blob) file
		chmod 777 tools_boeffla/dtbToolCM
		tools_boeffla/dtbToolCM -2 -o $BUILD_PATH/$OUTPUT_FOLDER/arch/arm/boot/dt.img -s 2048 -p $BUILD_PATH/$OUTPUT_FOLDER/scripts/dtc/ $BUILD_PATH/$OUTPUT_FOLDER/arch/arm/boot/
	fi

	TIMESTAMP2=$(date +%s)
	
	# Log compile time (screen output)
	echo "compile time:" $(($TIMESTAMP2 - $TIMESTAMP1)) "seconds"
	echo "zImage size (bytes):"
	stat -c%s $BUILD_PATH/$OUTPUT_FOLDER/arch/arm/boot/zImage

	# Log compile time and parameters (log file output)
	echo -e "\n***************************************************" >> ../compile.log
	echo -e "\ncompile time:" $(($TIMESTAMP2 - $TIMESTAMP1)) "seconds" >> ../compile.log
	echo "zImage size (bytes):" >> ../compile.log
	stat -c%s $BUILD_PATH/$OUTPUT_FOLDER/arch/arm/boot/zImage >> ../compile.log

	echo -e "\n***************************************************" >> ../compile.log
	echo -e "\nroot path:" $ROOT_PATH >> ../compile.log
	echo "toolchain compile:" >> ../compile.log
	grep "^CROSS_COMPILE" $BUILD_PATH/Makefile >> ../compile.log
	echo "toolchain stripping:" $TOOLCHAIN >> ../compile.log
	echo "extended cmdline:" $EXTENDED_CMDLINE >> ../compile.log
}

step4_unpack_ramdisk()
{
	echo -e $COLOR_GREEN"\n4 - unpack ramdisk\n"$COLOR_NEUTRAL
	
	# Cleanup folder if still existing
	echo -e ">>> cleanup repack folder\n"
	{
		rm -r -f $REPACK_PATH
		mkdir -p $REPACK_PATH
	} 2>/dev/null

	# Copy and Unpack original ramdisk
	echo -e ">>> unpack original ramdisk\n"

	cd $REPACK_PATH

	cp $BUILD_PATH/ramdisk_original/* .
	mkdir ramdisk

	cd $REPACK_PATH/ramdisk
	gunzip -c ../boot.img-ramdisk.gz | cpio -i
}

step5_patch_ramdisk()
{
	echo -e $COLOR_GREEN"\n5 - patch ramdisk\n"$COLOR_NEUTRAL
	
	# Copy compiled files (zImage, dtb and modules)
	echo -e ">>> copy zImage, dtb and modules\n"
	
	cp $BUILD_PATH/$OUTPUT_FOLDER/arch/arm/boot/zImage $REPACK_PATH/zImage
	{
		# copy dt.img
		cp $BUILD_PATH/$OUTPUT_FOLDER/arch/arm/boot/dt.img $REPACK_PATH/dt.img

		# copy modules and set permissions
		mkdir -p $REPACK_PATH/$MODULE_PATH
		
		cd $BUILD_PATH/$OUTPUT_FOLDER
		find -name '*.ko' -exec cp -av {} $REPACK_PATH/$MODULE_PATH/ \;
		chmod 644 $REPACK_PATH/$MODULE_PATH/*

		# strip modules
		echo -e ">>> strip modules\n"
		${TOOLCHAIN}strip --strip-unneeded $REPACK_PATH/$MODULE_PATH/*
	} 2>/dev/null


	# Apply boeffla kernel specific patches and copy additional files to ramdisk
	echo -e ">>> apply Boeffla kernel patches and copy files\n"

	cd $REPACK_PATH
	for PATCHFILE in $BUILD_PATH/ramdisk_boeffla/patch/*.patch
	do
		patch ramdisk/$(basename $PATCHFILE .patch) < $PATCHFILE
	done
		
	{
		# delete orig files, if patching created some
		rm ramdisk/*.orig

		cp -R $BUILD_PATH/ramdisk_boeffla/fs/* ramdisk
		chmod -R 755 ramdisk/*.rc
		chmod -R 755 ramdisk/sbin
		chmod -R 755 ramdisk/res/bc
		chmod -R 755 ramdisk/res/misc
	} 2>/dev/null
}

step6_repack_ramdisk()
{
	echo -e $COLOR_GREEN"\n6 - repack ramdisk\n"$COLOR_NEUTRAL
	
	echo -e ">>> repack new ramdisk\n"
	cd $REPACK_PATH/ramdisk
	find . | cpio -o -H newc | gzip > ../newramdisk.cpio.gz

	# Create new bootimage
	echo -e ">>> create boot image\n"
	
	cd $REPACK_PATH
	chmod 777 $BUILD_PATH/tools_boeffla/mkbootimg
	
	if [ "y" == "$COMPILE_DTB" ]; then
		$BUILD_PATH/tools_boeffla/mkbootimg --kernel zImage --ramdisk newramdisk.cpio.gz --cmdline "$MKBOOTIMG_CMDLINE $EXTENDED_CMDLINE" --base $MKBOOTIMG_BASE --pagesize $MKBOOTIMG_PAGESIZE --ramdisk_offset $MKBOOTIMG_RAMDISK_OFFSET --tags_offset $MKBOOTIMG_TAGS_OFFSET --dt dt.img -o boot.img
	else
		$BUILD_PATH/tools_boeffla/mkbootimg --kernel zImage --ramdisk newramdisk.cpio.gz --cmdline "$MKBOOTIMG_CMDLINE $EXTENDED_CMDLINE" --base $MKBOOTIMG_BASE --pagesize $MKBOOTIMG_PAGESIZE --ramdisk_offset $MKBOOTIMG_RAMDISK_OFFSET --tags_offset $MKBOOTIMG_TAGS_OFFSET -o boot.img
	fi
	
	# Creating recovery flashable zip
	echo -e ">>> create flashable zip\n"

	cd $REPACK_PATH
	mkdir -p META-INF/com/google/android
	cp $BUILD_PATH/tools_boeffla/update-binary META-INF/com/google/android

	# compose updater script
	if [ ! -z $ASSERT_1 ]; then
		echo "assert(getprop(\"ro.product.device\") == \"$ASSERT_1\" ||" >> META-INF/com/google/android/updater-script
		echo "getprop(\"ro.build.product\") == \"$ASSERT_1\" ||" >> META-INF/com/google/android/updater-script
	fi
	if [ ! -z $ASSERT_2 ]; then
		echo "getprop(\"ro.product.device\") == \"$ASSERT_2\" ||" >> META-INF/com/google/android/updater-script
		echo "getprop(\"ro.build.product\") == \"$ASSERT_2\" ||" >> META-INF/com/google/android/updater-script
	fi
	if [ ! -z $ASSERT_3 ]; then
		echo "getprop(\"ro.product.device\") == \"$ASSERT_3\" ||" >> META-INF/com/google/android/updater-script
		echo "getprop(\"ro.build.product\") == \"$ASSERT_3\" ||" >> META-INF/com/google/android/updater-script
	fi
	if [ ! -z $ASSERT_4 ]; then
		echo "getprop(\"ro.product.device\") == \"$ASSERT_4\" ||" >> META-INF/com/google/android/updater-script
		echo "getprop(\"ro.build.product\") == \"$ASSERT_4\" ||" >> META-INF/com/google/android/updater-script
	fi
	if [ ! -z $ASSERT_5 ]; then
		echo "getprop(\"ro.product.device\") == \"$ASSERT_5\" ||" >> META-INF/com/google/android/updater-script
		echo "getprop(\"ro.build.product\") == \"$ASSERT_5\" ||" >> META-INF/com/google/android/updater-script
	fi
	if [ ! -z $ASSERT_6 ]; then
		echo "getprop(\"ro.product.device\") == \"$ASSERT_6\" ||" >> META-INF/com/google/android/updater-script
		echo "getprop(\"ro.build.product\") == \"$ASSERT_6\" ||" >> META-INF/com/google/android/updater-script
	fi
	if [ ! -z $ASSERT_7 ]; then
		echo "getprop(\"ro.product.device\") == \"$ASSERT_7\" ||" >> META-INF/com/google/android/updater-script
		echo "getprop(\"ro.build.product\") == \"$ASSERT_7\" ||" >> META-INF/com/google/android/updater-script
	fi
	if [ ! -z $ASSERT_8 ]; then
		echo "getprop(\"ro.product.device\") == \"$ASSERT_8\" ||" >> META-INF/com/google/android/updater-script
		echo "getprop(\"ro.build.product\") == \"$ASSERT_8\" ||" >> META-INF/com/google/android/updater-script
	fi
	if [ ! -z $ASSERT_9 ]; then
		echo "getprop(\"ro.product.device\") == \"$ASSERT_9\" ||" >> META-INF/com/google/android/updater-script
		echo "getprop(\"ro.build.product\") == \"$ASSERT_9\" ||" >> META-INF/com/google/android/updater-script
	fi
	if [ ! -z $ASSERT_10 ]; then
		echo "getprop(\"ro.product.device\") == \"$ASSERT_10\" ||" >> META-INF/com/google/android/updater-script
		echo "getprop(\"ro.build.product\") == \"$ASSERT_10\" ||" >> META-INF/com/google/android/updater-script
	fi
	if [ ! -z $ASSERT_11 ]; then
		echo "getprop(\"ro.product.device\") == \"$ASSERT_11\" ||" >> META-INF/com/google/android/updater-script
		echo "getprop(\"ro.build.product\") == \"$ASSERT_11\" ||" >> META-INF/com/google/android/updater-script
	fi
	if [ ! -z $ASSERT_12 ]; then
		echo "getprop(\"ro.product.device\") == \"$ASSERT_12\" ||" >> META-INF/com/google/android/updater-script
		echo "getprop(\"ro.build.product\") == \"$ASSERT_12\" ||" >> META-INF/com/google/android/updater-script
	fi
	
	if [ ! -z $ASSERT_1 ]; then
		echo "abort(\"This package is for device: $ASSERT_1 $ASSERT_2 $ASSERT_3 $ASSERT_4 $ASSERT_5 $ASSERT_6; this device is \" + getprop(\"ro.product.device\") + \".\"););" >> META-INF/com/google/android/updater-script
	fi
	
	echo "ui_print(\"Flashing Radioactive-kernel $RADIOACTIVE_VERSION\");" >> META-INF/com/google/android/updater-script
	echo "package_extract_file(\"boot.img\", \"$BOOT_PARTITION\");" >> META-INF/com/google/android/updater-script
	
	if [ ! "y" == "$KERNEL_SAMSUNG" ]; then
		echo "mount(\"ext4\", \"EMMC\", \"$SYSTEM_PARTITION\", \"/system\");" >> META-INF/com/google/android/updater-script
		echo "delete_recursive(\"/$MODULE_PATH\");" >> META-INF/com/google/android/updater-script
		echo "package_extract_dir(\"$MODULE_PATH\", \"/$MODULE_PATH\");" >> META-INF/com/google/android/updater-script
		echo "unmount(\"/system\");" >> META-INF/com/google/android/updater-script
	fi
	
	echo "ui_print(\" \");" >> META-INF/com/google/android/updater-script
	echo "ui_print(\"(c) Ramon Cuenca (aka acuicultor), $(date +%Y.%m.%d-%H:%M:%S)\");" >> META-INF/com/google/android/updater-script
	echo "ui_print(\" \");" >> META-INF/com/google/android/updater-script
	echo "ui_print(\"Finished, please reboot.\");" >> META-INF/com/google/android/updater-script

	# add required files to new zip
	zip $RADIOACTIVE_FILENAME.recovery.zip boot.img
	zip $RADIOACTIVE_FILENAME.recovery.zip META-INF/com/google/android/updater-script
	zip $RADIOACTIVE_FILENAME.recovery.zip META-INF/com/google/android/update-binary

	if [ ! "y" == "$KERNEL_SAMSUNG" ]; then
		zip $RADIOACTIVE_FILENAME.recovery.zip $MODULE_PATH/*
	fi

	# sign recovery zip if there are keys available
	if [ -f "$BUILD_PATH/tools_boeffla/testkey.x509.pem" ]; then
		echo -e ">>> signing recovery zip\n"
		java -jar $BUILD_PATH/tools_boeffla/signapk.jar -w $BUILD_PATH/tools_boeffla/testkey.x509.pem $BUILD_PATH/tools_boeffla/testkey.pk8 $RADIOACTIVE_FILENAME.recovery.zip $RADIOACTIVE_FILENAME.recovery.zip_signed
		cp $RADIOACTIVE_FILENAME.recovery.zip_signed $RADIOACTIVE_FILENAME.recovery.zip
		rm $RADIOACTIVE_FILENAME.recovery.zip_signed
	fi

	md5sum $RADIOACTIVE_FILENAME.recovery.zip > $RADIOACTIVE_FILENAME.recovery.zip.md5

	# For Samsung kernels, create tar.md5 for odin
	if [ "y" == "$KERNEL_SAMSUNG" ]; then
		echo -e ">>> create Samsung files for Odin\n"
		cd $REPACK_PATH
		tar -cvf $RADIOACTIVE_FILENAME.tar boot.img
		md5sum $RADIOACTIVE_FILENAME.tar >> $RADIOACTIVE_FILENAME.tar
		mv $RADIOACTIVE_FILENAME.tar $RADIOACTIVE_FILENAME.tar.md5
	fi
	
	# Creating additional files for load&flash
	echo -e ">>> create load&flash files\n"

	if [ "y" == "$KERNEL_SAMSUNG" ]; then
		md5sum boot.img > checksum
	else
		cp $RADIOACTIVE_FILENAME.recovery.zip cm-kernel.zip
		md5sum cm-kernel.zip > checksum
	fi

	# Cleanup
	echo -e ">>> cleanup\n"
	rm -rf META-INF
}

step7_analyse_log()
{
	echo -e $COLOR_GREEN"\n7 - analyse log\n"$COLOR_NEUTRAL

	# Check compile result and patch file success
	echo -e "\n***************************************************"
	echo -e "Check for compile errors:"

	cd $ROOT_PATH
	echo -e $COLOR_RED
	grep " error" compile.log
	grep "forbidden warning" compile.log
	echo -e $COLOR_NEUTRAL

	echo -e "Check for patch file issues:"
	cd $REPACK_PATH/ramdisk
	echo -e $COLOR_RED
	find . -type f -name *.rej
	echo -e $COLOR_NEUTRAL

	echo -e "***************************************************"
}

step8_transfer_kernel()
{
	echo -e $COLOR_GREEN"\n8 - transfer kernel\n"$COLOR_NEUTRAL

	# exit function if no SMB share configured
	if [ -z "$SMB_SHARE_KERNEL" ]; then
		echo -e "No kernel smb share configured, not transfering files.\n"	
		return
	fi

	# copy the required files to a SMB network storage
	smbclient $SMB_SHARE_KERNEL -U $SMB_AUTH_KERNEL -c "mkdir $SMB_FOLDER_KERNEL\\$RADIOACTIVE_VERSION"
	smbclient $SMB_SHARE_KERNEL -U $SMB_AUTH_KERNEL -c "put $REPACK_PATH/$RADIOACTIVE_FILENAME.recovery.zip $SMB_FOLDER_KERNEL\\$RADIOACTIVE_VERSION\\$RADIOACTIVE_FILENAME.recovery.zip"
	smbclient $SMB_SHARE_KERNEL -U $SMB_AUTH_KERNEL -c "put $REPACK_PATH/$RADIOACTIVE_FILENAME.recovery.zip.md5 $SMB_FOLDER_KERNEL\\$RADIOACTIVE_VERSION\\$RADIOACTIVE_FILENAME.recovery.zip.md5"
	smbclient $SMB_SHARE_KERNEL -U $SMB_AUTH_KERNEL -c "put $REPACK_PATH/checksum $SMB_FOLDER_KERNEL\\$RADIOACTIVE_VERSION\\checksum"

	if [ "y" == "$KERNEL_SAMSUNG" ]; then
		smbclient $SMB_SHARE_KERNEL -U $SMB_AUTH_KERNEL -c "put $REPACK_PATH/$RADIOACTIVE_FILENAME.tar.md5 $SMB_FOLDER_KERNEL\\$RADIOACTIVE_VERSION\\$RADIOACTIVE_FILENAME.tar.md5"
		smbclient $SMB_SHARE_KERNEL -U $SMB_AUTH_KERNEL -c "put $REPACK_PATH/boot.img $SMB_FOLDER_KERNEL\\$RADIOACTIVE_VERSION\\boot.img"
	else
		smbclient $SMB_SHARE_KERNEL -U $SMB_AUTH_KERNEL -c "put $REPACK_PATH/cm-kernel.zip $SMB_FOLDER_KERNEL\\$RADIOACTIVE_VERSION\\cm-kernel.zip"
	fi
}

step9_send_finished_mail()
{
	echo -e $COLOR_GREEN"\n9 - send finish mail\n"$COLOR_NEUTRAL

	# send a mail to inform about finished compilation
	if [ -z "$FINISH_MAIL_TO" ]; then
		echo -e "No mail address configured, not sending mail.\n"	
	else
		cat $ROOT_PATH/compile.log | /usr/bin/mailx -s "Compilation for Radioactive-Kernel $RADIOACTIVE_VERSION finished!!!" $FINISH_MAIL_TO
	fi
}	

stepR_rewrite_config()
{
	echo -e $COLOR_GREEN"\nr - rewrite config\n"$COLOR_NEUTRAL
	
	# copy defconfig, run make oldconfig and copy it back
	cd $SOURCE_PATH
	cp arch/arm/configs/$DEFCONFIG .config
	make oldconfig
	cp .config arch/arm/configs/$DEFCONFIG
	make mrproper
	
	# commit change
	git add arch/arm/configs/$DEFCONFIG
	git commit
}

stepC_cleanup()
{
	echo -e $COLOR_GREEN"\nc - cleanup\n"$COLOR_NEUTRAL
	
	# remove old build and repack folders, remove any logs
	{
		rm -r -f $BUILD_PATH
		rm -r -f $REPACK_PATH
		rm $ROOT_PATH/*.log
	} 2>/dev/null
}

stepB_backup()
{
	echo -e $COLOR_GREEN"\nb - backup\n"$COLOR_NEUTRAL

	# Create a tar backup in parent folder, gzip it and copy to verlies
	BACKUP_FILE="$ROOT_DIR_NAME""_$(date +"%Y-%m-%d_%H-%M").tar.gz"

	cd $ROOT_PATH
	tar cvfz $BACKUP_FILE source x-settings.sh
	cd $SOURCE_PATH

	# transfer backup only if smbshare configured
	if [ -z "$SMB_SHARE_BACKUP" ]; then
		echo -e "No backup smb share configured, not transfering backup.\n"	
	else
		# copy backup to a SMB network storage and delete backup afterwards
		smbclient $SMB_SHARE_BACKUP -U $SMB_AUTH_BACKUP -c "put $ROOT_PATH/$BACKUP_FILE $SMB_FOLDER_BACKUP\\$BACKUP_FILE"
		rm $ROOT_PATH/$BACKUP_FILE
	fi
}


################
# main function
################

case "$1" in
	a)
		step0_copy_code
		step1_make_clean
		step2_make_config
		step3_compile
		step4_unpack_ramdisk
		step5_patch_ramdisk
		step6_repack_ramdisk
		step7_analyse_log
		step8_transfer_kernel
		step9_send_finished_mail
		exit
		;;
	u)
		step3_compile
		step4_unpack_ramdisk
		step5_patch_ramdisk
		step6_repack_ramdisk
		step7_analyse_log
		step8_transfer_kernel
		step9_send_finished_mail
		exit
		;;
	ur)
		step6_repack_ramdisk
		step7_analyse_log
		step8_transfer_kernel
		step9_send_finished_mail
		exit
		;;
	0)
		step0_copy_code
		exit
		;;
	1)
		step1_make_clean
		exit
		;;
	2)
		step2_make_config
		exit
		;;
	3)
		step3_compile
		exit
		;;
	4)
		step4_unpack_ramdisk
		exit
		;;
	5)
		step5_patch_ramdisk
		exit
		;;
	6)
		step6_repack_ramdisk
		exit
		;;
	7)
		step7_analyse_log
		exit
		;;
	8)
		step8_transfer_kernel
		exit
		;;
	9)
		step9_send_finished_mail
		exit
		;;
	b)
		stepB_backup
		exit
		;;
	c)
		stepC_cleanup
		exit
		;;
	r)
		stepR_rewrite_config
		exit
		;;
esac	

echo
echo
echo "Function menu"
echo "================================================"
echo
echo "a   = all, execute steps 0-9"
echo "u   = upd, execute steps 3-9"
echo "ur  = upd, execute steps 6-9"
echo
echo "0  = copy code"
echo "1  = make clean"
echo "2  = make config"
echo "3  = compile"
echo "4  = unpack ramdisk"
echo "5  = patch ramdisk"
echo "6  = repack ramdisk"
echo "7  = analyse log"
echo "8  = transfer kernel"
echo "9  = send finish mail"
echo
echo "r = rewrite config"
echo "c = cleanup"
echo "b = backup"
echo 
echo "================================================"
echo 
echo "Parameters:"
echo
echo "  Radioactive version:  $RADIOACTIVE_VERSION"
echo "  Extended cmdline: $EXTENDED_CMDLINE"
echo "  Radioactive date:     $RADIOACTIVE_DATE"
echo "  Git branch:       $GIT_BRANCH"
echo
echo "  Toolchain:    $TOOLCHAIN / $TOOLCHAIN_COMPILE"
echo "  Root path:    $ROOT_PATH"
echo "  Root dir:     $ROOT_DIR_NAME"
echo "  Source path:  $SOURCE_PATH"
echo "  Build path:   $BUILD_PATH"
echo "  Repack path:  $REPACK_PATH"
echo
echo "================================================"

exit
