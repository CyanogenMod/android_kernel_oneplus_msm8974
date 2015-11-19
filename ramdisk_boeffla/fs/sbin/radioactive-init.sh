#!/system/bin/sh

# *****************************
# Bacon Cyanogenmod 12 version
#
# V0.1 acuicultor
# *****************************

# define basic kernel configuration
	# path to internal sd memory
	SD_PATH="/data/media/0"

	# block devices
	SYSTEM_DEVICE="/dev/block/platform/msm_sdcc.1/by-name/system"
	CACHE_DEVICE="/dev/block/platform/msm_sdcc.1/by-name/cache"
	DATA_DEVICE="/dev/block/platform/msm_sdcc.1/by-name/userdata"

# define file paths
	RADIOACTIVE_DATA_PATH="$SD_PATH/Radioactive-kernel-data"
	RADIOACTIVE_LOGFILE="$RADIOACTIVE_DATA_PATH/-kernel.log"
	RADIOACTIVE_STARTCONFIG="/data/.radioactive/startconfig"
	RADIOACTIVE_STARTCONFIG_EARLY="/data/.radioactive/startconfig_early"
	RADIOACTIVE_STARTCONFIG_DONE="/data/.radioactive/startconfig_done"
	CWM_RESET_ZIP="radioactive-config-reset-v4.zip"
	INITD_ENABLER="/data/.radioactive/enable-initd"
	BUSYBOX_ENABLER="/data/.radioactive/enable-busybox"
	FRANDOM_ENABLER="/data/.radioactive/enable-frandom"
	PERMISSIVE_ENABLER="/data/.radioactive/enable-permissive"

# If not yet existing, create a radioactive-kernel-data folder on sdcard 
# which is used for many purposes,
# always set permissions and owners correctly for pathes and files
	if [ ! -d "$RADIOACTIVE_DATA_PATH" ] ; then
		/sbin/busybox mkdir $RADIOACTIVE_DATA_PATH
	fi

	/sbin/busybox chmod 775 $SD_PATH
	/sbin/busybox chown 1023:1023 $SD_PATH

	/sbin/busybox chmod -R 775 $RADIOACTIVE_DATA_PATH
	/sbin/busybox chown -R 1023:1023 $RADIOACTIVE_DATA_PATH

# maintain log file history
	rm $RADIOACTIVE_LOGFILE.3
	mv $RADIOACTIVE_LOGFILE.2 $RADIOACTIVE_LOGFILE.3
	mv $RADIOACTIVE_LOGFILE.1 $RADIOACTIVE_LOGFILE.2
	mv $RADIOACTIVE_LOGFILE $RADIOACTIVE_LOGFILE.1

# Initialize the log file (chmod to make it readable also via /sdcard link)
	echo $(date) Radioactive-Kernel initialisation started > $RADIOACTIVE_LOGFILE
	/sbin/busybox chmod 666 $RADIOACTIVE_LOGFILE
	/sbin/busybox cat /proc/version >> $RADIOACTIVE_LOGFILE
	echo "=========================" >> $RADIOACTIVE_LOGFILE
	/sbin/busybox grep ro.build.version /system/build.prop >> $RADIOACTIVE_LOGFILE
	echo "=========================" >> $RADIOACTIVE_LOGFILE

# Correct /sbin and /res directory and file permissions
	mount -o remount,rw rootfs /

	# change permissions of /sbin folder and scripts in /res/bc
	/sbin/busybox chmod -R 755 /sbin
	/sbin/busybox chmod 755 /res/bc/*

	/sbin/busybox sync
	mount -o remount,ro rootfs /

# remove any obsolete RADIOACTIVE-Config V2 startconfig done file
	/sbin/busybox rm -f $RADIOACTIVE_STARTCONFIG_DONE

# remove not used configuration files for frandom and busybox
	/sbin/busybox rm -f $BUSYBOX_ENABLER
	/sbin/busybox rm -f $FRANDOM_ENABLER
	
# Apply RADIOACTIVE--Kernel default settings

	# Sdcard buffer tweaks default to 1024 kb
	echo 1024 > /sys/block/mmcblk0/bdi/read_ahead_kb
	/sbin/busybox sync

	# Ext4 tweaks default to on
	/sbin/busybox sync
	mount -o remount,commit=20,noatime $CACHE_DEVICE /cache
	/sbin/busybox sync
	mount -o remount,commit=20,noatime $DATA_DEVICE /data
	/sbin/busybox sync

	echo $(date) RADIOACTIVE--Kernel default settings applied >> $RADIOACTIVE_LOGFILE

# Execute early startconfig placed by RADIOACTIVE--Config V2 app, if there is one
	if [ -f $RADIOACTIVE_STARTCONFIG_EARLY ]; then
		. $RADIOACTIVE_STARTCONFIG_EARLY
		echo $(date) "Early startup configuration found"  >> $RADIOACTIVE_LOGFILE
		echo $(date) Early startup configuration applied  >> $RADIOACTIVE_LOGFILE
	else
		echo $(date) "No early startup configuration found"  >> $RADIOACTIVE_LOGFILE
	fi

# init.d support (enabler only to be considered for CM based roms)
# (zipalign scripts will not be executed as only exception)
	if [ -f $INITD_ENABLER ] ; then
		echo $(date) Execute init.d scripts start >> $RADIOACTIVE_LOGFILE
		if cd /system/etc/init.d >/dev/null 2>&1 ; then
			for file in * ; do
				if ! cat "$file" >/dev/null 2>&1 ; then continue ; fi
				if [[ "$file" == *zipalign* ]]; then continue ; fi
				echo $(date) init.d file $file started >> $RADIOACTIVE_LOGFILE
				/system/bin/sh "$file"
				echo $(date) init.d file $file executed >> $RADIOACTIVE_LOGFILE
			done
		fi
		echo $(date) Finished executing init.d scripts >> $RADIOACTIVE_LOGFILE
	else
		echo $(date) init.d script handling by kernel disabled >> $RADIOACTIVE_LOGFILE
	fi

# Now wait for the rom to finish booting up
# (by checking for the android acore process)
	echo $(date) Checking for Rom boot trigger... >> $RADIOACTIVE_LOGFILE
	while ! /sbin/busybox pgrep com.android.systemui ; do
	  /sbin/busybox sleep 1
	done
	echo $(date) Rom boot trigger detected, waiting a few more seconds... >> $RADIOACTIVE_LOGFILE
	/sbin/busybox sleep 20

# Interaction with RADIOACTIVE--Config app V2
	# save original stock values for selected parameters
	cat /sys/devices/system/cpu/cpu0/cpufreq/UV_mV_table > /dev/bk_orig_cpu_voltage
	cat /sys/kernel/charge_levels/charge_level_ac > /dev/bk_orig_charge_level_ac
	cat /sys/kernel/charge_levels/charge_level_usb > /dev/bk_orig_charge_level_usb
	cat /sys/module/lowmemorykiller/parameters/minfree > /dev/bk_orig_minfree
	/sbin/busybox lsmod > /dev/bk_orig_modules
	cat /sys/class/kgsl/kgsl-3d0/devfreq/governor > /dev/bk_orig_gpu_governor
	cat /sys/class/kgsl/kgsl-3d0/min_pwrlevel > /dev/bk_orig_min_pwrlevel
	cat /sys/class/kgsl/kgsl-3d0/max_pwrlevel > /dev/bk_orig_max_pwrlevel

	# if there is a startconfig placed by RADIOACTIVE--Config V2 app, execute it;
	if [ -f $RADIOACTIVE_STARTCONFIG ]; then
		echo $(date) "Startup configuration found"  >> $RADIOACTIVE_LOGFILE
		. $RADIOACTIVE_STARTCONFIG
		echo $(date) Startup configuration applied  >> $RADIOACTIVE_LOGFILE
	else
		# dynamic fsync to on
		echo 1 > /sys/kernel/dyn_fsync/Dyn_fsync_active
		/sbin/busybox sync

		echo $(date) "No startup configuration found, enable all default settings"  >> $RADIOACTIVE_LOGFILE
	fi

# Turn off debugging for certain modules
	echo 0 > /sys/module/kernel/parameters/initcall_debug
	echo 0 > /sys/module/lowmemorykiller/parameters/debug_level
	echo 0 > /sys/module/alarm/parameters/debug_mask
	echo 0 > /sys/module/alarm_dev/parameters/debug_mask
	echo 0 > /sys/module/binder/parameters/debug_mask
	echo 0 > /sys/module/xt_qtaguid/parameters/debug_mask

# Auto root support
	if [ -f $SD_PATH/autoroot ]; then

		echo $(date) Auto root is enabled >> $RADIOACTIVE_LOGFILE

		mount -o remount,rw -t ext4 $SYSTEM_DEVICE /system

		/sbin/busybox mkdir /system/bin/.ext
		/sbin/busybox cp /res/misc/su /system/xbin/su
		/sbin/busybox cp /res/misc/su /system/xbin/daemonsu
		/sbin/busybox cp /res/misc/su /system/bin/.ext/.su
		/sbin/busybox cp /res/misc/install-recovery.sh /system/etc/install-recovery.sh
		/sbin/busybox echo /system/etc/.installed_su_daemon
		
		/sbin/busybox chown 0.0 /system/bin/.ext
		/sbin/busybox chmod 0777 /system/bin/.ext
		/sbin/busybox chown 0.0 /system/xbin/su
		/sbin/busybox chmod 6755 /system/xbin/su
		/sbin/busybox chown 0.0 /system/xbin/daemonsu
		/sbin/busybox chmod 6755 /system/xbin/daemonsu
		/sbin/busybox chown 0.0 /system/bin/.ext/.su
		/sbin/busybox chmod 6755 /system/bin/.ext/.su
		/sbin/busybox chown 0.0 /system/etc/install-recovery.sh
		/sbin/busybox chmod 0755 /system/etc/install-recovery.sh
		/sbin/busybox chown 0.0 /system/etc/.installed_su_daemon
		/sbin/busybox chmod 0644 /system/etc/.installed_su_daemon

		/system/bin/sh /system/etc/install-recovery.sh

		/sbin/busybox sync
		
		mount -o remount,ro -t ext4 $SYSTEM_DEVICE /system
		echo $(date) Auto root: su installed >> $RADIOACTIVE_LOGFILE

		rm $SD_PATH/autoroot
	fi

# EFS backup
	EFS_BACKUP_INT="$RADIOACTIVE_DATA_PATH/efs.tar.gz"

	if [ ! -f $EFS_BACKUP_INT ]; then

		dd if=/dev/block/mmcblk0p10 of=$RADIOACTIVE_DATA_PATH/modemst1.bin bs=512
		dd if=/dev/block/mmcblk0p11 of=$RADIOACTIVE_DATA_PATH/modemst2.bin bs=512

		cd $RADIOACTIVE_DATA_PATH
		/sbin/busybox tar cvz -f $EFS_BACKUP_INT modemst*
		/sbin/busybox chmod 666 $EFS_BACKUP_INT

		rm $RADIOACTIVE_DATA_PATH/modemst*
		
		echo $(date) EFS Backup: Not found, now created one >> $RADIOACTIVE_LOGFILE
	fi

# Copy reset recovery zip in RADIOACTIVE-kernel-data folder, delete older versions first
	CWM_RESET_ZIP_SOURCE="/res/misc/$CWM_RESET_ZIP"
	CWM_RESET_ZIP_TARGET="$RADIOACTIVE_DATA_PATH/$CWM_RESET_ZIP"

	if [ ! -f $CWM_RESET_ZIP_TARGET ]; then

		/sbin/busybox rm $RADIOACTIVE_DATA_PATH/RADIOACTIVE-config-reset*
		/sbin/busybox cp $CWM_RESET_ZIP_SOURCE $CWM_RESET_ZIP_TARGET
		/sbin/busybox chmod 666 $CWM_RESET_ZIP_TARGET

		echo $(date) Recovery reset zip copied >> $RADIOACTIVE_LOGFILE
	fi

# If not explicitely configured to permissive, set SELinux to enforcing and restart mpdecision
	if [ ! -f $PERMISSIVE_ENABLER ]; then
		echo "1" > /sys/fs/selinux/permissive

		stop mpdecision
		/sbin/busybox sleep 0.5
		start mpdecision

		echo $(date) "SELinux: enforcing" >> $RADIOACTIVE_LOGFILE
	else
		echo $(date) "SELinux: permissive" >> $RADIOACTIVE_LOGFILE
	fi

# Finished
	echo $(date) RADIOACTIVE--Kernel initialisation completed >> $RADIOACTIVE_LOGFILE
	echo $(date) "Loaded early startconfig was:" >> $RADIOACTIVE_LOGFILE
	cat $RADIOACTIVE-_STARTCONFIG_EARLY >> $RADIOACTIVE_LOGFILE
	echo $(date) "Loaded startconfig was:" >> $RADIOACTIVE_LOGFILE
	cat $RADIOACTIVE_STARTCONFIG >> $RADIOACTIVE_LOGFILE
	echo $(date) End of kernel startup logfile >> $RADIOACTIVE_LOGFILE
