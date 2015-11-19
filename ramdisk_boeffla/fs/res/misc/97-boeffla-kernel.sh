#!/sbin/sh
# 
# /system/addon.d/97-boeffla-kernel.sh
#
# Backup/Restore script for Boeffla-Kernel to survive a CM rom flash (Bacon)
#
# 08.01.2014, andip71 (aka Lord Boeffla)

# block devices (Bacon)
BOOT_DEVICE="/dev/block/mmcblk0p7"

# pathes
LIB_PATH="/system/lib/modules"
BACKUP_PATH="/tmp/backup/boeffla"

# remove boot device node to prevent CM overwriting boot image
rm -f $BOOT_DEVICE

case "$1" in
  backup)
	# create backup folder, make sure it is clean
	mkdir -p $BACKUP_PATH
	rm $BACKUP_PATH/*

	# backup kernel modules
	cp $LIB_PATH/* $BACKUP_PATH
  ;;
  restore)
	# restore kernel modules and set user rights
	mkdir -p $LIB_PATH
	chmod 755 $LIB_PATH
	cp $BACKUP_PATH/* $LIB_PATH
	chmod 644 $LIB_PATH/*
	
	# remove backup folder
	rm -r -f $BACKUP_PATH
  ;;
  pre-backup)
    # Nothing to do
  ;;
  post-backup)
    # Nothing to do
  ;;
  pre-restore)
    # Nothing to do
  ;;
  post-restore)
    # Nothing to do
  ;;
esac
