# Boeffla-Config controller interface
#
# *******************************
# Bacon Cyanogenmod 12s version
#
# V0.1
# *******************************

# ********************************
# Kernel specific initialisation
# ********************************

# kernel specification (hardware; type; target; url)
KERNEL_SPECS="bacon;cm;cm12.0s1;http://boeffla.df-kunde.de/bacon/boeffla-kernel-cms/"

# kernel features 
# (1=enable-busybox,2=enable-frandom,3=wipe-cache,4=disable-zram-control)
# (5=enable-default-zram-control,6=enable-selinux-switch, 7=enable-selinux-control)
KERNEL_FEATURES="-3-4-6-7-"

# path to kernel libraries
LIBPATH="/system/lib/modules"

# block devices
SYSTEM_DEVICE="/dev/block/platform/msm_sdcc.1/by-name/system"
CACHE_DEVICE="/dev/block/platform/msm_sdcc.1/by-name/cache"
DATA_DEVICE="/dev/block/platform/msm_sdcc.1/by-name/userdata"
BOOT_DEVICE="/dev/block/platform/msm_sdcc.1/by-name/boot"
RADIO_DEVICE="/dev/block/platform/msm_sdcc.1/by-name/modem"
RECOVERY_DEVICE="/dev/block/platform/msm_sdcc.1/by-name/recovery"


# *******************
# List of values
# *******************

if [ "lov_gov_profiles" == "$1" ]; then
	echo "interactive - battery;interactive - battery extreme;interactive - performance;zzmoove - optimal;zzmoove - battery;zzmoove - battery plus;zzmoove - battery yank;zzmoove - battery extreme yank;zzmoove - performance;zzmoove - insane;zzmoove - moderate;zzmoove - game;zzmoove - relax"
	exit 0
fi

if [ "lov_cpu_hotplug_profiles" == "$1" ]; then
	echo "Default;Optimized;1 core max;2 cores max;3 cores max;2 cores min;3 cores min;4 cores min;2 cores exact;3 cores exact;zzmoove native default;zzmoove native 1 core max;zzmoove native 2 cores max;zzmoove native 3 cores max;zzmoove native 2 cores min;zzmoove native 3 cores min;zzmoove native 4 cores min"
	exit 0
fi

if [ "lov_cpu_volt_profiles" == "$1" ]; then
	echo "No undervolting;undervolt -25mV;undervolt -50mV;undervolt -75mV;undervolt -100mV;undervolt light;undervolt medium;undervolt heavy"
	exit 0
fi

if [ "lov_gpu_freq_profiles" == "$1" ]; then
	#echo "54 only;160 only;160/266;266/350;54/108/160/200/266;108/160/200/266/350;160/266/350/440/533 (default);266/350/440/533/600;350/440/533/600/640;440/533/600/640/700"
	exit 0
fi

if [ "lov_gpu_volt_profiles" == "$1" ]; then
	#echo "No undervolting;undervolt -25mV;undervolt -50mV;undervolt -75mV;undervolt -100mV;undervolt light;undervolt medium;undervolt heavy;overvolt +25mV;overvolt +50mV;overvolt +75mV;overvolt +100mV"
	exit 0
fi

if [ "lov_gpu_freq" == "$1" ]; then
	#echo "54;108;160;200;266;300;350;400;440;500;533;600;640;700"
	exit 0
fi

if [ "lov_eq_gain_profiles" == "$1" ]; then
	#echo "Archis audiophile;Baseland;Bass extreme;Bass treble;Classic;Dance;Eargasm;Metal/Rock;Pleasant;Treble"
	exit 0
fi

if [ "lov_system_tweaks" == "$1" ]; then
	echo "Off;Boeffla tweaks;Speedmod tweaks;Mattiadj tweaks"
	exit 0
fi

if [ "lov_modules" == "$1" ]; then
	ls $LIBPATH/*
	exit 0
fi

if [ "lov_presets" == "$1" ]; then
	# Note, the ^ sign will be translated into newline for this setting
	echo "Power extreme~"
	echo "Gov: zzmoove / performance"
	echo "^Sched: row"
	echo "^CPU: 2880 / no uv"
	echo "^GPU: 330-657;"
	
	echo "Power~"
	echo "Gov: ondemand / standard"
	echo "^Sched: row"
	echo "^CPU: 2726 / no uv"
	echo "^GPU: 200-657;"
	
	echo "Standard~"
	echo "Gov: interactive / standard"
	echo "^Sched: row"
	echo "^CPU: 2457 / no uv"
	echo "^GPU: 27-578;"
	
	echo "Battery friendly~"
	echo "Gov: interactive / standard"
	echo "^Sched: zen"
	echo "^CPU: 1728 / -25mV"
	echo "^GPU: 27-330;"
	
	echo "Battery saving~"
	echo "Gov: zzmoove / battery yank"
	echo "^Sched: zen"
	echo "^CPU: 1497 / light uv"
	echo "^GPU: 27 only;"
	
	exit 0
fi


# ************************************
# Configuration values (for profiles)
# ************************************

if [ "conf_presets" == "$1" ]; then
	if [ "Power extreme" ==  "$2" ]; then
		# gov, gov prof, sched int, sched ext, cpu max, cpu uv, gpu freq, gpu uv
		echo "zzmoove;zzmoove - performance;"
		echo "row;row;"
		echo "2880000;None;"
		echo "4,0;None"
	fi
	if [ "Power" ==  "$2" ]; then
		# gov, gov prof, sched int, sched ext, cpu max, cpu uv, gpu freq, gpu uv
		echo "ondemand;ondemand - standard;"
		echo "row;row;"
		echo "2726400;None;"
		echo "5,0;None"
	fi
	if [ "Standard" ==  "$2" ]; then
		# gov, gov prof, sched int, sched ext, cpu max, cpu uv, gpu freq, gpu uv
		echo "interactive;standard;"
		echo "row;row;"
		echo "2457600;None;"
		echo "6,1;None"
	fi
	if [ "Battery friendly" ==  "$2" ]; then
		# gov, gov prof, sched int, sched ext, cpu max, cpu uv, gpu freq, gpu uv
		echo "interactive;standard;"
		echo "zen;zen;"
		echo "1728000;undervolt -25mV;"
		echo "6,4;None"
	fi
	if [ "Battery saving" ==  "$2" ]; then
		# gov, gov prof, sched int, sched ext, cpu max, cpu uv, gpu freq, gpu uv
		echo "zzmoove;zzmoove - battery yank;"
		echo "zen;zen;"
		echo "1497600;undervolt light;"
		echo "6,6;None"
	fi
	exit 0
fi


if [ "conf_gpu_freq" == "$1" ]; then
	if [ "54 only" == "$2" ]; then
		echo "54;54;54;54;54"
	fi
	if [ "160 only" == "$2" ]; then
		echo "160;160;160;160;160"
	fi
	if [ "160/266" == "$2" ]; then
		echo "160;160;160;266;266"
	fi
	if [ "266/350" == "$2" ]; then
		echo "266;266;266;350;350"
	fi
	if [ "54/108/160/200/266" == "$2" ]; then
		echo "54;108;160;200;266"
	fi
	if [ "108/160/200/266/350" == "$2" ]; then
		echo "108;160;200;266;350"
	fi
	if [ "160/266/350/440/533 (default)" == "$2" ]; then
		echo "160;266;350;440;533"
	fi
	if [ "266/350/440/533/600" == "$2" ]; then
		echo "266;350;440;533;600"
	fi
	if [ "350/440/533/600/640" == "$2" ]; then
		echo "350;440;533;600;640"
	fi
	if [ "440/533/600/640/700" == "$2" ]; then
		echo "440;533;600;640;700"
	fi
	exit 0
fi


if [ "conf_gpu_volt" == "$1" ]; then
	if [ "No undervolting" == "$2" ]; then
		echo "0;0;0;0;0"
	fi
	if [ "undervolt -25mV" == "$2" ]; then
		echo "-25000;-25000;-25000;-25000;-25000"
	fi
	if [ "undervolt -50mV" == "$2" ]; then
		echo "-50000;-50000;-50000;-50000;-50000"
	fi
	if [ "undervolt -75mV" == "$2" ]; then
		echo "-75000;-75000;-75000;-75000;-75000"
	fi
	if [ "undervolt -100mV" == "$2" ]; then
		echo "-100000;-100000;-100000;-100000;-100000"
	fi
	if [ "undervolt light" == "$2" ]; then
		echo "-25000;-25000;-25000;-50000;-50000"
	fi
	if [ "undervolt medium" == "$2" ]; then
		echo "-50000;-50000;-50000;-75000;-75000"
	fi
	if [ "undervolt heavy" == "$2" ]; then
		echo "-75000;-75000;-75000;-100000;-100000"
	fi
	if [ "overvolt +25mV" == "$2" ]; then
		echo "25000;25000;25000;25000;25000"
	fi
	if [ "overvolt +50mV" == "$2" ]; then
		echo "50000;50000;50000;50000;50000"
	fi
	if [ "overvolt +75mV" == "$2" ]; then
		echo "75000;75000;75000;75000;75000"
	fi
	if [ "overvolt +100mV" == "$2" ]; then
		echo "100000;100000;100000;100000;100000"
	fi
	exit 0
fi

if [ "conf_cpu_volt" == "$1" ]; then
	if [ "No undervolting" == "$2" ]; then
		echo "0;0;0;0;0;0;0;0;0;0;0;0;0;0;0;0;0;0"
	fi
	if [ "undervolt -25mV" == "$2" ]; then
		echo "-25;-25;-25;-25;-25;-25;-25;-25;-25;-25;-25;-25;-25;-25;-25;-25;-25;-25"
	fi
	if [ "undervolt -50mV" == "$2" ]; then
		echo "-50;-50;-50;-50;-50;-50;-50;-50;-50;-50;-50;-50;-50;-50;-50;-50;-50;-50"
	fi
	if [ "undervolt -75mV" == "$2" ]; then
		echo "-75;-75;-75;-75;-75;-75;-75;-75;-75;-75;-75;-75;-75;-75;-75;-75;-75;-75"
	fi
	if [ "undervolt -100mV" == "$2" ]; then
		echo "-100;-100;-100;-100;-100;-100;-100;-100;-100;-100;-100;-100;-100;-100;-100;-100;-100;-100"
	fi
	if [ "undervolt light" == "$2" ]; then
		echo "-50;-50;-50;-50;-50;-25;-25;-25;-25;-25;-25;-25;0;0;0;0;0;0"
	fi
	if [ "undervolt medium" == "$2" ]; then
		echo "-75;-75;-75;-75;-75;-75;-50;-50;-50;-50;-50;-25;-25;-25;-25;-25;-25;-25"
	fi
	if [ "undervolt heavy" == "$2" ]; then
		echo "-100;-100;-100;-100;-100;-100;-100;-75;-75;-75;-75;-75;-50;-50;-50;-50;-50;-50"
	fi
	exit 0
fi

if [ "conf_eq_gains" == "$1" ]; then
	if [ "Flat" ==  "$2" ]; then
		echo "0;0;0;0;0"
	fi
	if [ "Archis audiophile" ==  "$2" ]; then
		echo "8;4;4;2;6"
	fi
	if [ "Eargasm" ==  "$2" ]; then
		echo "12;8;4;2;3"
	fi
	if [ "Pleasant" ==  "$2" ]; then
		echo "4;3;2;2;3"
	fi
	if [ "Classic" ==  "$2" ]; then
		echo "0;0;0;-3;-5"
	fi
	if [ "Bass treble" ==  "$2" ]; then
		echo "10;7;0;2;5"
	fi
	if [ "Bass extreme" ==  "$2" ]; then
		echo "12;8;3;-1;1"
	fi
	if [ "Treble" ==  "$2" ]; then
		echo "-5;1;0;4;3"
	fi
	if [ "Baseland" ==  "$2" ]; then
		echo "8;7;4;3;3"
	fi
	if [ "Dance" ==  "$2" ]; then
		echo "4;0;-6;0;3"
	fi
	if [ "Metal/Rock" ==  "$2" ]; then
		echo "4;3;0;-4;3"
	fi
	if [ "Googy" ==  "$2" ]; then
		echo "10;2;-1;2;10"
	fi
	exit 0
fi

# *******************
# Parameters
# *******************

if [ "param_readahead" == "$1" ]; then
	# Internal sd (min/max/steps)
	echo "128;3072;128;"
	# External sd (min/max/steps)
	echo "128;3072;128"
	exit 0
fi

if [ "param_boeffla_sound" == "$1" ]; then
	# Headphone min/max, Speaker min/max
	echo "-30;30;-30;30;"
	# Equalizer min/max
	echo "-12;12;"
	# Microphone gain min/max
	echo "-30;30;"
	# Stereo expansion min/max
	echo "0;31"
	exit 0
fi

if [ "param_cpu_uv" == "$1" ]; then
	# CPU UV min/max/steps
	echo "600;1500;25"
	exit 0
fi

if [ "param_gpu_uv" == "$1" ]; then
	# GPU UV min/max/steps
	echo "500000;1200000;25000"
	exit 0
fi

if [ "param_led" == "$1" ]; then
	# LED speed min/max/steps
	echo "0;12;1;"
	# LED brightness min/max/steps
	echo "0;100;5"
	exit 0
fi

if [ "param_touchwake" == "$1" ]; then
	# Touchwake min/max/steps
	echo "0;600000;5000;"
	# Knockon min/max/steps
	echo "100;2000;100"
	exit 0
fi

if [ "param_early_suspend_delay" == "$1" ]; then
	# Early suspend delay min/max/steps
	echo "0;700;25"
	exit 0
fi

if [ "param_zram" == "$1" ]; then
	# zRam size min/max/steps
	echo "104857600;838860800;20971520"
	exit 0
fi

if [ "param_charge_rates" == "$1" ]; then
	# AC charge min/max/steps
	echo "0;2200;50;"
	# USB charge min/max/steps
	echo "0;1600;50;"
	# Wireless charge min/max/steps
	# echo "100;1000;25"
	exit 0
fi

if [ "param_lmk" == "$1" ]; then
	# LMK size min/max/steps
	echo "5;300;1"
	exit 0
fi


# *******************
# Get settings
# *******************

if [ "get_ums" == "$1" ]; then
#	if [ "`busybox grep 179 /sys/devices/platform/s3c-usbgadget/gadget/lun0/file`" ]; then
#		echo "1"
#	else
#		echo "0"
#	fi
	echo ""
	exit 0
fi


if [ "get_tunables" == "$1" ]; then
	if [ -d /sys/devices/system/cpu/cpufreq/$2 ]; then
		cd /sys/devices/system/cpu/cpufreq/$2
		for file in *
		do
			content="`busybox cat $file`"
			busybox echo -ne "$file~$content;"
		done
	fi
fi


if [ "get_kernel_version2" == "$1" ]; then
	busybox cat /proc/version
	exit 0
fi


if [ "get_kernel_specs" == "$1" ]; then
	echo $KERNEL_SPECS
	exit 0
fi

if [ "get_kernel_features" == "$1" ]; then
	echo $KERNEL_FEATURES
	exit 0
fi


# *******************
# Applying settings
# *******************

if [ "apply_cpu_hotplug_profile" == "$1" ]; then


	if [ `busybox cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor | busybox grep zzmoove` ]; then
		echo "0" > /sys/devices/system/cpu/cpufreq/zzmoove/hotplug_max_limit
		echo "0" > /sys/devices/system/cpu/cpufreq/zzmoove/hotplug_min_limit
		echo "2" > /sys/devices/system/cpu/cpufreq/zzmoove/disable_hotplug
		echo "2" > /sys/devices/system/cpu/cpufreq/zzmoove/disable_hotplug_sleep
	fi

	if [ "Default" == "$2" ]; then
		echo "0" >/sys/devices/system/cpu/cpu0/online_control
		echo "0" >/sys/devices/system/cpu/cpu1/online_control
		echo "0" >/sys/devices/system/cpu/cpu2/online_control
		echo "0" >/sys/devices/system/cpu/cpu3/online_control
		exit 0
	fi

	if [ "1 core max" == "$2" ]; then
		echo "0" >/sys/devices/system/cpu/cpu0/online_control
		echo "2" >/sys/devices/system/cpu/cpu1/online_control
		echo "2" >/sys/devices/system/cpu/cpu2/online_control
		echo "2" >/sys/devices/system/cpu/cpu3/online_control
		exit 0
	fi

	if [ "2 cores max" == "$2" ]; then
		echo "0" >/sys/devices/system/cpu/cpu0/online_control
		echo "0" >/sys/devices/system/cpu/cpu1/online_control
		echo "2" >/sys/devices/system/cpu/cpu2/online_control
		echo "2" >/sys/devices/system/cpu/cpu3/online_control
		exit 0
	fi

	if [ "3 cores max" == "$2" ]; then
		echo "0" >/sys/devices/system/cpu/cpu0/online_control
		echo "0" >/sys/devices/system/cpu/cpu1/online_control
		echo "0" >/sys/devices/system/cpu/cpu2/online_control
		echo "2" >/sys/devices/system/cpu/cpu3/online_control
		exit 0
	fi

	if [ "2 cores min" == "$2" ]; then
		echo "1" >/sys/devices/system/cpu/cpu0/online_control
		echo "0" >/sys/devices/system/cpu/cpu1/online_control
		echo "0" >/sys/devices/system/cpu/cpu2/online_control
		echo "1" >/sys/devices/system/cpu/cpu3/online_control
		exit 0
	fi

	if [ "3 cores min" == "$2" ]; then
		echo "1" >/sys/devices/system/cpu/cpu0/online_control
		echo "0" >/sys/devices/system/cpu/cpu1/online_control
		echo "1" >/sys/devices/system/cpu/cpu2/online_control
		echo "1" >/sys/devices/system/cpu/cpu3/online_control
		exit 0
	fi

	if [ "4 cores min" == "$2" ]; then
		echo "1" >/sys/devices/system/cpu/cpu0/online_control
		echo "1" >/sys/devices/system/cpu/cpu1/online_control
		echo "1" >/sys/devices/system/cpu/cpu2/online_control
		echo "1" >/sys/devices/system/cpu/cpu3/online_control
		exit 0
	fi

	if [ "Optimized" == "$2" ]; then
		echo "0" >/sys/devices/system/cpu/cpu0/online_control
		echo "0" >/sys/devices/system/cpu/cpu1/online_control
		echo "0" >/sys/devices/system/cpu/cpu2/online_control
		echo "3" >/sys/devices/system/cpu/cpu3/online_control
		exit 0
	fi

	if [ "2 cores exact" == "$2" ]; then
		echo "1" >/sys/devices/system/cpu/cpu0/online_control
		echo "2" >/sys/devices/system/cpu/cpu1/online_control
		echo "2" >/sys/devices/system/cpu/cpu2/online_control
		echo "1" >/sys/devices/system/cpu/cpu3/online_control
		exit 0
	fi

	if [ "3 cores exact" == "$2" ]; then
		echo "1" >/sys/devices/system/cpu/cpu0/online_control
		echo "1" >/sys/devices/system/cpu/cpu1/online_control
		echo "2" >/sys/devices/system/cpu/cpu2/online_control
		echo "1" >/sys/devices/system/cpu/cpu3/online_control
		exit 0
	fi

	if [ "zzmoove native default" == "$2" ]; then
		echo "1" >/sys/devices/system/cpu/cpu0/online_control
		echo "1" >/sys/devices/system/cpu/cpu1/online_control
		echo "1" >/sys/devices/system/cpu/cpu2/online_control
		echo "1" >/sys/devices/system/cpu/cpu3/online_control

		if [ `busybox cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor | busybox grep zzmoove` ]; then
			echo "0" > /sys/devices/system/cpu/cpufreq/zzmoove/disable_hotplug
			echo "0" > /sys/devices/system/cpu/cpufreq/zzmoove/disable_hotplug_sleep

			if [ `busybox cat /sys/devices/system/cpu/cpufreq/zzmoove/profile_number | busybox grep 11` ]; then
				echo "2" > /sys/devices/system/cpu/cpufreq/zzmoove/hotplug_min_limit
			fi
		else
			echo "0" >/sys/devices/system/cpu/cpu0/online_control
			echo "0" >/sys/devices/system/cpu/cpu1/online_control
			echo "0" >/sys/devices/system/cpu/cpu2/online_control
			echo "0" >/sys/devices/system/cpu/cpu3/online_control
		fi
		exit 0
	fi

	if [ "zzmoove native 1 core max" == "$2" ]; then
		echo "1" >/sys/devices/system/cpu/cpu0/online_control
		echo "1" >/sys/devices/system/cpu/cpu1/online_control
		echo "1" >/sys/devices/system/cpu/cpu2/online_control
		echo "1" >/sys/devices/system/cpu/cpu3/online_control

		if [ `busybox cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor | busybox grep zzmoove` ]; then
			echo "0" > /sys/devices/system/cpu/cpufreq/zzmoove/disable_hotplug
			echo "0" > /sys/devices/system/cpu/cpufreq/zzmoove/disable_hotplug_sleep
			echo "1" > /sys/devices/system/cpu/cpufreq/zzmoove/hotplug_max_limit
		else
			echo "0" >/sys/devices/system/cpu/cpu0/online_control
			echo "0" >/sys/devices/system/cpu/cpu1/online_control
			echo "0" >/sys/devices/system/cpu/cpu2/online_control
			echo "0" >/sys/devices/system/cpu/cpu3/online_control
		fi
		exit 0
	fi

	if [ "zzmoove native 2 cores max" == "$2" ]; then
		echo "1" >/sys/devices/system/cpu/cpu0/online_control
		echo "1" >/sys/devices/system/cpu/cpu1/online_control
		echo "1" >/sys/devices/system/cpu/cpu2/online_control
		echo "1" >/sys/devices/system/cpu/cpu3/online_control

		if [ `busybox cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor | busybox grep zzmoove` ]; then
			echo "0" > /sys/devices/system/cpu/cpufreq/zzmoove/disable_hotplug
			echo "0" > /sys/devices/system/cpu/cpufreq/zzmoove/disable_hotplug_sleep
			echo "2" > /sys/devices/system/cpu/cpufreq/zzmoove/hotplug_max_limit
		else
			echo "0" >/sys/devices/system/cpu/cpu0/online_control
			echo "0" >/sys/devices/system/cpu/cpu1/online_control
			echo "0" >/sys/devices/system/cpu/cpu2/online_control
			echo "0" >/sys/devices/system/cpu/cpu3/online_control
		fi
		exit 0
	fi

	if [ "zzmoove native 3 cores max" == "$2" ]; then
		echo "1" >/sys/devices/system/cpu/cpu0/online_control
		echo "1" >/sys/devices/system/cpu/cpu1/online_control
		echo "1" >/sys/devices/system/cpu/cpu2/online_control
		echo "1" >/sys/devices/system/cpu/cpu3/online_control

		if [ `busybox cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor | busybox grep zzmoove` ]; then
			echo "0" > /sys/devices/system/cpu/cpufreq/zzmoove/disable_hotplug
			echo "0" > /sys/devices/system/cpu/cpufreq/zzmoove/disable_hotplug_sleep
			echo "3" > /sys/devices/system/cpu/cpufreq/zzmoove/hotplug_max_limit
		else
			echo "0" >/sys/devices/system/cpu/cpu0/online_control
			echo "0" >/sys/devices/system/cpu/cpu1/online_control
			echo "0" >/sys/devices/system/cpu/cpu2/online_control
			echo "0" >/sys/devices/system/cpu/cpu3/online_control
		fi
		exit 0
	fi

	if [ "zzmoove native 2 cores min" == "$2" ]; then
		echo "1" >/sys/devices/system/cpu/cpu0/online_control
		echo "1" >/sys/devices/system/cpu/cpu1/online_control
		echo "1" >/sys/devices/system/cpu/cpu2/online_control
		echo "1" >/sys/devices/system/cpu/cpu3/online_control

		if [ `busybox cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor | busybox grep zzmoove` ]; then
			echo "0" > /sys/devices/system/cpu/cpufreq/zzmoove/disable_hotplug
			echo "0" > /sys/devices/system/cpu/cpufreq/zzmoove/disable_hotplug_sleep
			echo "2" > /sys/devices/system/cpu/cpufreq/zzmoove/hotplug_min_limit
		else
			echo "0" >/sys/devices/system/cpu/cpu0/online_control
			echo "0" >/sys/devices/system/cpu/cpu1/online_control
			echo "0" >/sys/devices/system/cpu/cpu2/online_control
			echo "0" >/sys/devices/system/cpu/cpu3/online_control
		fi
		exit 0
	fi

	if [ "zzmoove native 3 cores min" == "$2" ]; then
		echo "1" >/sys/devices/system/cpu/cpu0/online_control
		echo "1" >/sys/devices/system/cpu/cpu1/online_control
		echo "1" >/sys/devices/system/cpu/cpu2/online_control
		echo "1" >/sys/devices/system/cpu/cpu3/online_control

		if [ `busybox cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor | busybox grep zzmoove` ]; then
			echo "0" > /sys/devices/system/cpu/cpufreq/zzmoove/disable_hotplug
			echo "0" > /sys/devices/system/cpu/cpufreq/zzmoove/disable_hotplug_sleep
			echo "3" > /sys/devices/system/cpu/cpufreq/zzmoove/hotplug_min_limit
		else
			echo "0" >/sys/devices/system/cpu/cpu0/online_control
			echo "0" >/sys/devices/system/cpu/cpu1/online_control
			echo "0" >/sys/devices/system/cpu/cpu2/online_control
			echo "0" >/sys/devices/system/cpu/cpu3/online_control
		fi
		exit 0
	fi

	if [ "zzmoove native 4 cores min" == "$2" ]; then
		echo "1" >/sys/devices/system/cpu/cpu0/online_control
		echo "1" >/sys/devices/system/cpu/cpu1/online_control
		echo "1" >/sys/devices/system/cpu/cpu2/online_control
		echo "1" >/sys/devices/system/cpu/cpu3/online_control

		if [ `busybox cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor | busybox grep zzmoove` ]; then
			echo "0" > /sys/devices/system/cpu/cpufreq/zzmoove/hotplug_max_limit
			echo "0" > /sys/devices/system/cpu/cpufreq/zzmoove/hotplug_min_limit
			echo "1" > /sys/devices/system/cpu/cpufreq/zzmoove/disable_hotplug
			echo "1" > /sys/devices/system/cpu/cpufreq/zzmoove/disable_hotplug_sleep
		else
			echo "0" >/sys/devices/system/cpu/cpu0/online_control
			echo "0" >/sys/devices/system/cpu/cpu1/online_control
			echo "0" >/sys/devices/system/cpu/cpu2/online_control
			echo "0" >/sys/devices/system/cpu/cpu3/online_control
		fi
		exit 0
	fi
	
	exit 0;
fi

if [ "apply_governor_profile" == "$1" ]; then

	if [ "ondemand - standard" == "$2" ]; then
		echo "3" >/sys/devices/system/cpu/cpufreq/ondemand/down_differential
		echo "3" >/sys/devices/system/cpu/cpufreq/ondemand/down_differential_multi_core
		echo "0" >/sys/devices/system/cpu/cpufreq/ondemand/ignore_nice_load
		echo "0" >/sys/devices/system/cpu/cpufreq/ondemand/input_boost
		echo "0" >/sys/devices/system/cpu/cpufreq/ondemand/io_is_busy
		echo "300000" >/sys/devices/system/cpu/cpufreq/ondemand/optimal_freq
		echo "0" >/sys/devices/system/cpu/cpufreq/ondemand/powersave_bias
		echo "1" >/sys/devices/system/cpu/cpufreq/ondemand/sampling_down_factor
		echo "100000" >/sys/devices/system/cpu/cpufreq/ondemand/sampling_rate
		echo "10000" >/sys/devices/system/cpu/cpufreq/ondemand/sampling_rate_min
		echo "300000" >/sys/devices/system/cpu/cpufreq/ondemand/sync_freq
		echo "95" >/sys/devices/system/cpu/cpufreq/ondemand/up_threshold
		echo "80" >/sys/devices/system/cpu/cpufreq/ondemand/up_threshold_any_cpu_load
		echo "80" >/sys/devices/system/cpu/cpufreq/ondemand/up_threshold_multi_core

		busybox sleep 0.5s
		busybox sync
	fi

	if [ "conservative - standard" == "$2" ]; then
		echo "20" >/sys/devices/system/cpu/cpufreq/conservative/down_threshold
		echo "5" >/sys/devices/system/cpu/cpufreq/conservative/freq_step
		echo "0" >/sys/devices/system/cpu/cpufreq/conservative/ignore_nice_load
		echo "1" >/sys/devices/system/cpu/cpufreq/conservative/sampling_down_factor
		echo "200000" >/sys/devices/system/cpu/cpufreq/conservative/sampling_rate
		echo "200000" >/sys/devices/system/cpu/cpufreq/conservative/sampling_rate_min
		echo "80" >/sys/devices/system/cpu/cpufreq/conservative/up_threshold

		busybox sleep 0.5s
		busybox sync
	fi

	if [ "intelliactive - standard" == "$2" ]; then
		echo "20000" > /sys/devices/system/cpu/cpufreq/intelliactive/above_hispeed_delay 
		echo "0" > /sys/devices/system/cpu/cpufreq/intelliactive/boost
		echo "" > /sys/devices/system/cpu/cpufreq/intelliactive/boostpulse
		echo "80000" > /sys/devices/system/cpu/cpufreq/intelliactive/boostpulse_duration
		echo "99" > /sys/devices/system/cpu/cpufreq/intelliactive/go_hispeed_load
		echo "1400000" > /sys/devices/system/cpu/cpufreq/intelliactive/hispeed_freq
		echo "1" > /sys/devices/system/cpu/cpufreq/intelliactive/io_is_busy
		echo "80000" > /sys/devices/system/cpu/cpufreq/intelliactive/min_sample_time
		echo "0" > /sys/devices/system/cpu/cpufreq/intelliactive/sampling_down_factor
		echo "729600" > /sys/devices/system/cpu/cpufreq/intelliactive/sync_freq
		echo "90" > /sys/devices/system/cpu/cpufreq/intelliactive/target_loads
		echo "20000" > /sys/devices/system/cpu/cpufreq/intelliactive/timer_rate
		echo "80000" > /sys/devices/system/cpu/cpufreq/intelliactive/timer_slack
		echo "1728000,1728000,1728000,1728000" > /sys/devices/system/cpu/cpufreq/intelliactive/two_phase_freq
		echo "960000" > /sys/devices/system/cpu/cpufreq/intelliactive/up_threshold_any_cpu_freq
		echo "95" > /sys/devices/system/cpu/cpufreq/intelliactive/up_threshold_any_cpu_load

		busybox sleep 0.5s
		busybox sync
	fi

	if [ "intellidemand - standard" == "$2" ]; then
		echo "3" > /sys/devices/system/cpu/cpufreq/intellidemand/down_differential 
		echo "0" > /sys/devices/system/cpu/cpufreq/intellidemand/ignore_nice_load 
		echo "1" > /sys/devices/system/cpu/cpufreq/intellidemand/io_is_busy 
		echo "0" > /sys/devices/system/cpu/cpufreq/intellidemand/powersave_bias 
		echo "15" > /sys/devices/system/cpu/cpufreq/intellidemand/sampling_down_factor 
		echo "10000" > /sys/devices/system/cpu/cpufreq/intellidemand/sampling_rate 
		echo "4294967295" > /sys/devices/system/cpu/cpufreq/intellidemand/sampling_rate_max 
		echo "10000" > /sys/devices/system/cpu/cpufreq/intellidemand/sampling_rate_min 
		echo "85" > /sys/devices/system/cpu/cpufreq/intellidemand/up_threshold 

		busybox sleep 0.5s
		busybox sync
	fi

	if [ "interactive - standard" == "$2" ]; then
		echo "20000 1400000:40000 1700000:20000" > /sys/devices/system/cpu/cpufreq/interactive/above_hispeed_delay 
		echo "0" > /sys/devices/system/cpu/cpufreq/interactive/boost 
		echo "" > /sys/devices/system/cpu/cpufreq/interactive/boostpulse 
		echo "80000" > /sys/devices/system/cpu/cpufreq/interactive/boostpulse_duration 
		echo "90" > /sys/devices/system/cpu/cpufreq/interactive/go_hispeed_load 
		echo "1190400" > /sys/devices/system/cpu/cpufreq/interactive/hispeed_freq 
		echo "1" > /sys/devices/system/cpu/cpufreq/interactive/io_is_busy 
		echo "40000" > /sys/devices/system/cpu/cpufreq/interactive/min_sample_time 
		echo "100000" > /sys/devices/system/cpu/cpufreq/interactive/sampling_down_factor 
		echo "1036800" > /sys/devices/system/cpu/cpufreq/interactive/sync_freq 
		echo "85 1500000:90 1800000:70" > /sys/devices/system/cpu/cpufreq/interactive/target_loads 
		echo "30000" > /sys/devices/system/cpu/cpufreq/interactive/timer_rate 
		echo "20000" > /sys/devices/system/cpu/cpufreq/interactive/timer_slack 
		echo "1190400" > /sys/devices/system/cpu/cpufreq/interactive/up_threshold_any_cpu_freq 
		echo "50" > /sys/devices/system/cpu/cpufreq/interactive/up_threshold_any_cpu_load 

		busybox sleep 0.5s
		busybox sync
	fi
	
	if [ "interactive - battery" == "$2" ]; then
		echo "20000 1400000:40000 1700000:20000" > /sys/devices/system/cpu/cpufreq/interactive/above_hispeed_delay 
		echo "0" > /sys/devices/system/cpu/cpufreq/interactive/boost 
		echo "" > /sys/devices/system/cpu/cpufreq/interactive/boostpulse 
		echo "80000" > /sys/devices/system/cpu/cpufreq/interactive/boostpulse_duration 
		echo "95" > /sys/devices/system/cpu/cpufreq/interactive/go_hispeed_load 
		echo "833200" > /sys/devices/system/cpu/cpufreq/interactive/hispeed_freq 
		echo "1" > /sys/devices/system/cpu/cpufreq/interactive/io_is_busy 
		echo "10000" > /sys/devices/system/cpu/cpufreq/interactive/min_sample_time 
		echo "100000" > /sys/devices/system/cpu/cpufreq/interactive/sampling_down_factor 
		echo "1036800" > /sys/devices/system/cpu/cpufreq/interactive/sync_freq 
		echo "85 1200000:90 1500000:70" > /sys/devices/system/cpu/cpufreq/interactive/target_loads 
		echo "50000" > /sys/devices/system/cpu/cpufreq/interactive/timer_rate 
		echo "20000" > /sys/devices/system/cpu/cpufreq/interactive/timer_slack 
		echo "1190400" > /sys/devices/system/cpu/cpufreq/interactive/up_threshold_any_cpu_freq 
		echo "50" > /sys/devices/system/cpu/cpufreq/interactive/up_threshold_any_cpu_load 

		busybox sleep 0.5s
		busybox sync
	fi

	if [ "interactive - battery extreme" == "$2" ]; then
		echo "20000 1400000:40000 1700000:20000" > /sys/devices/system/cpu/cpufreq/interactive/above_hispeed_delay 
		echo "0" > /sys/devices/system/cpu/cpufreq/interactive/boost 
		echo "" > /sys/devices/system/cpu/cpufreq/interactive/boostpulse 
		echo "80000" > /sys/devices/system/cpu/cpufreq/interactive/boostpulse_duration 
		echo "100" > /sys/devices/system/cpu/cpufreq/interactive/go_hispeed_load 
		echo "300000" > /sys/devices/system/cpu/cpufreq/interactive/hispeed_freq 
		echo "1" > /sys/devices/system/cpu/cpufreq/interactive/io_is_busy 
		echo "5000" > /sys/devices/system/cpu/cpufreq/interactive/min_sample_time 
		echo "100000" > /sys/devices/system/cpu/cpufreq/interactive/sampling_down_factor 
		echo "1036800" > /sys/devices/system/cpu/cpufreq/interactive/sync_freq 
		echo "85 900000:90 1200000:70" > /sys/devices/system/cpu/cpufreq/interactive/target_loads 
		echo "100000" > /sys/devices/system/cpu/cpufreq/interactive/timer_rate 
		echo "20000" > /sys/devices/system/cpu/cpufreq/interactive/timer_slack 
		echo "1190400" > /sys/devices/system/cpu/cpufreq/interactive/up_threshold_any_cpu_freq 
		echo "50" > /sys/devices/system/cpu/cpufreq/interactive/up_threshold_any_cpu_load 

		busybox sleep 0.5s
		busybox sync
	fi

	if [ "interactive - performance" == "$2" ]; then
		echo "20000 1400000:40000 1700000:20000" > /sys/devices/system/cpu/cpufreq/interactive/above_hispeed_delay 
		echo "0" > /sys/devices/system/cpu/cpufreq/interactive/boost 
		echo "" > /sys/devices/system/cpu/cpufreq/interactive/boostpulse 
		echo "80000" > /sys/devices/system/cpu/cpufreq/interactive/boostpulse_duration 
		echo "80" > /sys/devices/system/cpu/cpufreq/interactive/go_hispeed_load 
		echo "1958400" > /sys/devices/system/cpu/cpufreq/interactive/hispeed_freq 
		echo "1" > /sys/devices/system/cpu/cpufreq/interactive/io_is_busy 
		echo "60000" > /sys/devices/system/cpu/cpufreq/interactive/min_sample_time 
		echo "100000" > /sys/devices/system/cpu/cpufreq/interactive/sampling_down_factor 
		echo "1036800" > /sys/devices/system/cpu/cpufreq/interactive/sync_freq 
		echo "85 1800000:90 2100000:70" > /sys/devices/system/cpu/cpufreq/interactive/target_loads 
		echo "20000" > /sys/devices/system/cpu/cpufreq/interactive/timer_rate 
		echo "20000" > /sys/devices/system/cpu/cpufreq/interactive/timer_slack 
		echo "1190400" > /sys/devices/system/cpu/cpufreq/interactive/up_threshold_any_cpu_freq 
		echo "50" > /sys/devices/system/cpu/cpufreq/interactive/up_threshold_any_cpu_load 

		busybox sleep 0.5s
		busybox sync
	fi

	if [ "wheatley - standard" == "$2" ]; then
		echo "5" > /sys/devices/system/cpu/cpufreq/wheatley/allowed_misses
		echo "0" > /sys/devices/system/cpu/cpufreq/wheatley/ignore_nice_load
		echo "0" > /sys/devices/system/cpu/cpufreq/wheatley/io_is_busy
		echo "0" > /sys/devices/system/cpu/cpufreq/wheatley/powersave_bias
		echo "1" > /sys/devices/system/cpu/cpufreq/wheatley/sampling_down_factor
		echo "10000" > /sys/devices/system/cpu/cpufreq/wheatley/sampling_rate
		echo "10000" > /sys/devices/system/cpu/cpufreq/wheatley/sampling_rate_min
		echo "10000" > /sys/devices/system/cpu/cpufreq/wheatley/target_residency
		echo "95" > /sys/devices/system/cpu/cpufreq/wheatley/up_threshold

		busybox sleep 0.5s
		busybox sync
	fi	

	if [ "smartmax - standard" == "$2" ]; then
		echo "1036800" > /sys/devices/system/cpu/cpufreq/smartmax/awake_ideal_freq
		echo "1" > /sys/devices/system/cpu/cpufreq/smartmax/boost_duration
		echo "1497600" > /sys/devices/system/cpu/cpufreq/smartmax/boost_freq
		echo "0" > /sys/devices/system/cpu/cpufreq/smartmax/debug_mask
		echo "60000" > /sys/devices/system/cpu/cpufreq/smartmax/down_rate
		echo "1" > /sys/devices/system/cpu/cpufreq/smartmax/ignore_nice
		echo "150000" > /sys/devices/system/cpu/cpufreq/smartmax/input_boost_duration
		echo "0" > /sys/devices/system/cpu/cpufreq/smartmax/io_is_busy
		echo "55" > /sys/devices/system/cpu/cpufreq/smartmax/max_cpu_load
		echo "5" > /sys/devices/system/cpu/cpufreq/smartmax/min_cpu_load
		echo "10000" > /sys/devices/system/cpu/cpufreq/smartmax/min_sampling_rate
		echo "200000" > /sys/devices/system/cpu/cpufreq/smartmax/ramp_down_step
		echo "1" > /sys/devices/system/cpu/cpufreq/smartmax/ramp_up_during_boost
		echo "200000" > /sys/devices/system/cpu/cpufreq/smartmax/ramp_up_step
		echo "30000" > /sys/devices/system/cpu/cpufreq/smartmax/sampling_rate
		echo "652800" > /sys/devices/system/cpu/cpufreq/smartmax/suspend_ideal_freq
		echo "1497600" > /sys/devices/system/cpu/cpufreq/smartmax/touch_poke_freq
		echo "30000" > /sys/devices/system/cpu/cpufreq/smartmax/up_rate

		busybox sleep 0.5s
		busybox sync
	fi

	if [ "smartmax_eps - standard" == "$2" ]; then
		echo "652800" > /sys/devices/system/cpu/cpufreq/smartmax/awake_ideal_freq
		echo "0" > /sys/devices/system/cpu/cpufreq/smartmax/boost_duration
		echo "1497600" > /sys/devices/system/cpu/cpufreq/smartmax/boost_freq
		echo "0" > /sys/devices/system/cpu/cpufreq/smartmax/debug_mask
		echo "60000" > /sys/devices/system/cpu/cpufreq/smartmax/down_rate
		echo "1" > /sys/devices/system/cpu/cpufreq/smartmax/ignore_nice
		echo "90000" > /sys/devices/system/cpu/cpufreq/smartmax/input_boost_duration
		echo "0" > /sys/devices/system/cpu/cpufreq/smartmax/io_is_busy
		echo "70" > /sys/devices/system/cpu/cpufreq/smartmax/max_cpu_load
		echo "40" > /sys/devices/system/cpu/cpufreq/smartmax/min_cpu_load
		echo "10000" > /sys/devices/system/cpu/cpufreq/smartmax/min_sampling_rate
		echo "200000" > /sys/devices/system/cpu/cpufreq/smartmax/ramp_down_step
		echo "1" > /sys/devices/system/cpu/cpufreq/smartmax/ramp_up_during_boost
		echo "200000" > /sys/devices/system/cpu/cpufreq/smartmax/ramp_up_step
		echo "30000" > /sys/devices/system/cpu/cpufreq/smartmax/sampling_rate
		echo "300000" > /sys/devices/system/cpu/cpufreq/smartmax/suspend_ideal_freq
		echo "1036800" > /sys/devices/system/cpu/cpufreq/smartmax/touch_poke_freq
		echo "30000" > /sys/devices/system/cpu/cpufreq/smartmax/up_rate

		busybox sleep 0.5s
		busybox sync
	fi

	if [ "slim - standard" == "$2" ]; then
		echo "3" > /sys/devices/system/cpu/cpufreq/slim/down_differential
		echo "1" > /sys/devices/system/cpu/cpufreq/slim/gboost
		echo "0" > /sys/devices/system/cpu/cpufreq/slim/ignore_nice_load
		echo "1728000,1267200,1267200,1267200" > /sys/devices/system/cpu/cpufreq/slim/input_event_min_freq
		echo "500" > /sys/devices/system/cpu/cpufreq/slim/input_event_timeout
		echo "300000" > /sys/devices/system/cpu/cpufreq/slim/optimal_freq
		echo "0" > /sys/devices/system/cpu/cpufreq/slim/powersave_bias
		echo "1" > /sys/devices/system/cpu/cpufreq/slim/sampling_down_factor
		echo "30000" > /sys/devices/system/cpu/cpufreq/slim/sampling_rate
		echo "10000" > /sys/devices/system/cpu/cpufreq/slim/sampling_rate_min
		echo "300000" > /sys/devices/system/cpu/cpufreq/slim/sync_freq
		echo "1728000,1728000,1728000,1728000" > /sys/devices/system/cpu/cpufreq/slim/two_phase_freq
		echo "15000" > /sys/devices/system/cpu/cpufreq/slim/ui_sampling_rate
		echo "95" > /sys/devices/system/cpu/cpufreq/slim/up_threshold
		echo "90" > /sys/devices/system/cpu/cpufreq/slim/up_threshold_any_cpu_load
		echo "90" > /sys/devices/system/cpu/cpufreq/slim/up_threshold_multi_core

		busybox sleep 0.5s
		busybox sync
	fi

	if [ "intellimm - standard" == "$2" ]; then
		echo "3" > /sys/devices/system/cpu/cpufreq/intellimm/down_differential
		echo "3" > /sys/devices/system/cpu/cpufreq/intellimm/down_differential_multi_c
		echo "652800" > /sys/devices/system/cpu/cpufreq/intellimm/freq_down_step
		echo "1190400" > /sys/devices/system/cpu/cpufreq/intellimm/freq_down_step_barrier
		echo "0" > /sys/devices/system/cpu/cpufreq/intellimm/ignore_nice_load
		echo "1574400,1574400,1574400,1574400" > /sys/devices/system/cpu/cpufreq/intellimm/input_event_min_freq
		echo "0" > /sys/devices/system/cpu/cpufreq/intellimm/io_is_busy
		echo "1728000" > /sys/devices/system/cpu/cpufreq/intellimm/optimal_freq
		echo "0" > /sys/devices/system/cpu/cpufreq/intellimm/powersave_bias
		echo "1" > /sys/devices/system/cpu/cpufreq/intellimm/sampling_down_factor
		echo "50000" > /sys/devices/system/cpu/cpufreq/intellimm/sampling_rate
		echo "10000" > /sys/devices/system/cpu/cpufreq/intellimm/sampling_rate_min
		echo "0" > /sys/devices/system/cpu/cpufreq/intellimm/shortcut
		echo "1728000" > /sys/devices/system/cpu/cpufreq/intellimm/two_phase_freq
		echo "95" > /sys/devices/system/cpu/cpufreq/intellimm/up_threshold
		echo "80" > /sys/devices/system/cpu/cpufreq/intellimm/up_threshold_any_cpu_load
		echo "80" > /sys/devices/system/cpu/cpufreq/intellimm/up_threshold_multi_core

		busybox sleep 0.5s
		busybox sync
	fi

	if [ "zzmoove - standard" == "$2" ]; then
		echo "1" > /sys/devices/system/cpu/cpufreq/zzmoove/profile_number

		busybox sleep 0.5s
		busybox sync
	fi

	if [ "zzmoove - battery" == "$2" ]; then
		echo "4" > /sys/devices/system/cpu/cpufreq/zzmoove/profile_number

		busybox sleep 0.5s
		busybox sync
	fi

	if [ "zzmoove - optimal" == "$2" ]; then
		echo "6" > /sys/devices/system/cpu/cpufreq/zzmoove/profile_number

		busybox sleep 0.5s
		busybox sync
	fi

	if [ "zzmoove - performance" == "$2" ]; then
		echo "8" > /sys/devices/system/cpu/cpufreq/zzmoove/profile_number

		busybox sleep 0.5s
		busybox sync
	fi

	if [ "zzmoove - battery extreme yank" == "$2" ]; then
		echo "3" > /sys/devices/system/cpu/cpufreq/zzmoove/profile_number

		busybox sleep 0.5s
		busybox sync
	fi

	if [ "zzmoove - battery yank" == "$2" ]; then
		echo "2" > /sys/devices/system/cpu/cpufreq/zzmoove/profile_number

		busybox sleep 0.5s
		busybox sync
	fi

	if [ "zzmoove - insane" == "$2" ]; then
		echo "9" > /sys/devices/system/cpu/cpufreq/zzmoove/profile_number

		busybox sleep 0.5s
		busybox sync
	fi

	if [ "zzmoove - battery plus" == "$2" ]; then
		echo "5" > /sys/devices/system/cpu/cpufreq/zzmoove/profile_number

		busybox sleep 0.5s
		busybox sync
	fi

	if [ "zzmoove - moderate" == "$2" ]; then
		echo "7" > /sys/devices/system/cpu/cpufreq/zzmoove/profile_number

		busybox sleep 0.5s
		busybox sync
	fi

	if [ "zzmoove - game" == "$2" ]; then
		echo "10" > /sys/devices/system/cpu/cpufreq/zzmoove/profile_number

		busybox sleep 0.5s
		busybox sync
	fi

	if [ "zzmoove - relax" == "$2" ]; then
		echo "11" > /sys/devices/system/cpu/cpufreq/zzmoove/profile_number

		busybox sleep 0.5s
		busybox sync
	fi

	if [ "pegasusq - standard" == "$2" ]; then
		echo "5" > /sys/devices/system/cpu/cpufreq/pegasusq/down_differential
		echo "2265600" > /sys/devices/system/cpu/cpufreq/pegasusq/freq_for_responsiveness
		echo "37" > /sys/devices/system/cpu/cpufreq/pegasusq/freq_step
		echo "0" > /sys/devices/system/cpu/cpufreq/pegasusq/ignore_nice_load
		echo "0" > /sys/devices/system/cpu/cpufreq/pegasusq/io_is_busy
		echo "2" > /sys/devices/system/cpu/cpufreq/pegasusq/sampling_down_factor
		echo "50000" > /sys/devices/system/cpu/cpufreq/pegasusq/sampling_rate
		echo "10000" > /sys/devices/system/cpu/cpufreq/pegasusq/sampling_rate_min
		echo "85" > /sys/devices/system/cpu/cpufreq/pegasusq/up_threshold
		echo "40" > /sys/devices/system/cpu/cpufreq/pegasusq/up_threshold_at_min_freq

		busybox sleep 0.5s
		busybox sync
	fi
	
	if [ "lionheart - standard" == "$2" ]; then
		echo "30" > /sys/devices/system/cpu/cpufreq/lionheart/down_threshold
		echo "5" > /sys/devices/system/cpu/cpufreq/lionheart/freq_step
		echo "0" > /sys/devices/system/cpu/cpufreq/lionheart/ignore_nice_load
		echo "1" > /sys/devices/system/cpu/cpufreq/lionheart/sampling_down_factor
		echo "10000" > /sys/devices/system/cpu/cpufreq/lionheart/sampling_rate
		echo "10000" > /sys/devices/system/cpu/cpufreq/lionheart/sampling_rate_min
		echo "65" > /sys/devices/system/cpu/cpufreq/lionheart/up_threshold

		busybox sleep 0.5s
		busybox sync
	fi

	if [ "nightmare - standard" == "$2" ]; then
		echo "50" > /sys/devices/system/cpu/cpufreq/nightmare/dec_cpu_load
		echo "540000" > /sys/devices/system/cpu/cpufreq/nightmare/freq_for_responsiveness
		echo "1890000" > /sys/devices/system/cpu/cpufreq/nightmare/freq_for_responsiveness_max
		echo "20" > /sys/devices/system/cpu/cpufreq/nightmare/freq_step
		echo "20" > /sys/devices/system/cpu/cpufreq/nightmare/freq_step_at_min_freq
		echo "10" > /sys/devices/system/cpu/cpufreq/nightmare/freq_step_dec
		echo "10" > /sys/devices/system/cpu/cpufreq/nightmare/freq_step_dec_at_max_freq
		echo "30" > /sys/devices/system/cpu/cpufreq/nightmare/freq_up_brake
		echo "30" > /sys/devices/system/cpu/cpufreq/nightmare/freq_up_brake_at_min_freq
		echo "70" > /sys/devices/system/cpu/cpufreq/nightmare/inc_cpu_load
		echo "60" > /sys/devices/system/cpu/cpufreq/nightmare/inc_cpu_load_at_min_freq
		echo "60000" > /sys/devices/system/cpu/cpufreq/nightmare/sampling_rate
		
		busybox sleep 0.5s
		busybox sync
	fi

	if [ "impulse - standard" == "$2" ]; then
		echo "20000" > /sys/devices/system/cpu/cpufreq/impulse/above_hispeed_delay
		echo "1" > /sys/devices/system/cpu/cpufreq/impulse/align_windows
		echo "0" > /sys/devices/system/cpu/cpufreq/impulse/boost
		echo "0" > /sys/devices/system/cpu/cpufreq/impulse/boostpulse
		echo "80000" > /sys/devices/system/cpu/cpufreq/impulse/boostpulse_duration
		echo "99" > /sys/devices/system/cpu/cpufreq/impulse/go_hispeed_load
		echo "5" > /sys/devices/system/cpu/cpufreq/impulse/go_lowspeed_load
		echo "1958400" > /sys/devices/system/cpu/cpufreq/impulse/hispeed_freq
		echo "0" > /sys/devices/system/cpu/cpufreq/impulse/max_freq_hysteresis
		echo "80000" > /sys/devices/system/cpu/cpufreq/impulse/min_sample_time
		echo "0" > /sys/devices/system/cpu/cpufreq/impulse/powersave_bias
		echo "90" > /sys/devices/system/cpu/cpufreq/impulse/target_loads
		echo "20000" > /sys/devices/system/cpu/cpufreq/impulse/timer_rate
		echo "80000" > /sys/devices/system/cpu/cpufreq/impulse/timer_slack

		busybox sleep 0.5s
		busybox sync
	fi

	if [ "ondemandplus - standard" == "$2" ]; then
		echo "20" >/sys/devices/system/cpu/cpufreq/ondemandplus/down_differential
		echo "1728000" >/sys/devices/system/cpu/cpufreq/ondemandplus/inter_hifreq
		echo "300000" >/sys/devices/system/cpu/cpufreq/ondemandplus/inter_lofreq
		echo "2" >/sys/devices/system/cpu/cpufreq/ondemandplus/inter_staycycles
		echo "0" >/sys/devices/system/cpu/cpufreq/ondemandplus/io_is_busy
		echo "652800" >/sys/devices/system/cpu/cpufreq/ondemandplus/staycycles_resetfreq
		echo "20000" >/sys/devices/system/cpu/cpufreq/ondemandplus/timer_rate
		echo "70" >/sys/devices/system/cpu/cpufreq/ondemandplus/up_threshold

		busybox sleep 0.5s
		busybox sync
	fi

	if [ "yankactive - standard" == "$2" ]; then
		echo "80000" >/sys/devices/system/cpu/cpufreq/yankactive/above_hispeed_delay
		echo "0" >/sys/devices/system/cpu/cpufreq/yankactive/boost
		echo "20000" >/sys/devices/system/cpu/cpufreq/yankactive/boostpulse_duration
		echo "99" >/sys/devices/system/cpu/cpufreq/yankactive/go_hispeed_load
		echo "1728000" >/sys/devices/system/cpu/cpufreq/yankactive/hispeed_freq
		echo "0" >/sys/devices/system/cpu/cpufreq/yankactive/io_is_busy
		echo "20000" >/sys/devices/system/cpu/cpufreq/yankactive/min_sample_time
		echo "0" >/sys/devices/system/cpu/cpufreq/yankactive/sampling_down_factor
		echo "0" >/sys/devices/system/cpu/cpufreq/yankactive/sync_freq
		echo "95" >/sys/devices/system/cpu/cpufreq/yankactive/target_loads
		echo "20000" >/sys/devices/system/cpu/cpufreq/yankactive/timer_rate
		echo "80000" >/sys/devices/system/cpu/cpufreq/yankactive/timer_slack
		echo "0" >/sys/devices/system/cpu/cpufreq/yankactive/up_threshold_any_cpu_freq
		echo "0" >/sys/devices/system/cpu/cpufreq/yankactive/up_threshold_any_cpu_load

		busybox sleep 0.5s
		busybox sync
	fi
		
	exit 0
fi

if [ "apply_system_tweaks" == "$1" ]; then

	if [ "Off" == "$2" ]; then
		echo "5" > /proc/sys/vm/dirty_background_ratio
		echo "200" > /proc/sys/vm/dirty_expire_centisecs
		echo "20" > /proc/sys/vm/dirty_ratio
		echo "500" > /proc/sys/vm/dirty_writeback_centisecs
		echo "3207" > /proc/sys/vm/min_free_kbytes
		echo "60" > /proc/sys/vm/swappiness
		echo "100" > /proc/sys/vm/vfs_cache_pressure
		echo "0" > /proc/sys/vm/drop_caches
		busybox sleep 0.5s
		busybox sync
	fi

	if [ "Boeffla tweaks" == "$2" ]; then
		echo "70" > /proc/sys/vm/dirty_background_ratio
		echo "250" > /proc/sys/vm/dirty_expire_centisecs
		echo "90" > /proc/sys/vm/dirty_ratio
		echo "500" > /proc/sys/vm/dirty_writeback_centisecs
		echo "4096" > /proc/sys/vm/min_free_kbytes
		echo "60" > /proc/sys/vm/swappiness
		echo "10" > /proc/sys/vm/vfs_cache_pressure
		echo "3" > /proc/sys/vm/drop_caches
		busybox sleep 0.5s
		busybox sync
	fi

	if [ "Speedmod tweaks" == "$2" ]; then
		echo "5" > /proc/sys/vm/dirty_background_ratio
		echo "200" > /proc/sys/vm/dirty_expire_centisecs
		echo "20" > /proc/sys/vm/dirty_ratio
		echo "1500" > /proc/sys/vm/dirty_writeback_centisecs
		echo "12288" > /proc/sys/vm/min_free_kbytes
		echo "0" > /proc/sys/vm/swappiness
		echo "100" > /proc/sys/vm/vfs_cache_pressure
		echo "0" > /proc/sys/vm/drop_caches
		busybox sleep 0.5s
		busybox sync
	fi

	if [ "Mattiadj tweaks" == "$2" ]; then
		echo "10" > /proc/sys/vm/dirty_background_ratio
		echo "500" > /proc/sys/vm/dirty_expire_centisecs
		echo "10" > /proc/sys/vm/dirty_ratio
		echo "100" > /proc/sys/vm/dirty_writeback_centisecs
		echo "8192" > /proc/sys/vm/min_free_kbytes
		echo "70" > /proc/sys/vm/swappiness
		echo "500" > /proc/sys/vm/vfs_cache_pressure
		echo "0" > /proc/sys/vm/drop_caches
		busybox sleep 0.5s
		busybox sync
	fi
	exit 0
fi

if [ "apply_eq_bands" == "$1" ]; then
#	echo "1 4027 1031 0 276" > /sys/class/misc/boeffla_sound/eq_bands
#	echo "2 8076 61555 456 456" > /sys/class/misc/boeffla_sound/eq_bands
#	echo "3 7256 62323 2644 1368" > /sys/class/misc/boeffla_sound/eq_bands
#	echo "4 5774 63529 1965 4355" > /sys/class/misc/boeffla_sound/eq_bands
#	echo "5 1380 1369 0 16384" > /sys/class/misc/boeffla_sound/eq_bands
	exit 0
fi

if [ "apply_ext4_tweaks" == "$1" ]; then
	if [ "1" == "$2" ]; then
		busybox sync
		mount -o remount,commit=20,noatime $CACHE_DEVICE /cache
		busybox sync
		mount -o remount,commit=20,noatime $DATA_DEVICE /data
		busybox sync
	fi

	if [ "0" == "$2" ]; then
		busybox sync
		mount -o remount,commit=0,noatime $CACHE_DEVICE /cache
		busybox sync
		mount -o remount,commit=0,noatime $DATA_DEVICE /data
		busybox sync
	fi
	exit 0
fi

if [ "apply_survival_script" == "$1" ]; then
	if [ "1" == "$2" ]; then
		mount -o remount,rw -t ext4 $SYSTEM_DEVICE /system
		busybox mkdir -p /system/addon.d
		busybox cp /res/misc/97-boeffla-kernel.sh /system/addon.d
		busybox chmod 755 /system/addon.d/97-boeffla-kernel.sh
		busybox sync
		mount -o remount,ro -t ext4 $SYSTEM_DEVICE /system
	fi

	if [ "0" == "$2" ]; then
		mount -o remount,rw -t ext4 $SYSTEM_DEVICE /system
		busybox rm /system/addon.d/97-boeffla-kernel.sh
		busybox sync
		mount -o remount,ro -t ext4 $SYSTEM_DEVICE /system
	fi
	exit 0
fi

#if [ "apply_zram" == "$1" ]; then
#	if [ "1" == "$2" ]; then
#		if [ "1" == "$3" ]; then
#			busybox swapoff /dev/block/zram0
#			busybox swapoff /dev/block/zram1
#			busybox swapoff /dev/block/zram2
#			busybox swapoff /dev/block/zram3
#			echo "1" > /sys/block/zram0/reset
#			echo "1" > /sys/block/zram1/reset
#			echo "1" > /sys/block/zram2/reset
#			echo "1" > /sys/block/zram3/reset
#			echo "$4" > /sys/block/zram0/disksize
#			echo "0" > /sys/block/zram1/disksize
#			echo "0" > /sys/block/zram2/disksize
#			echo "0" > /sys/block/zram3/disksize
#			busybox mkswap /dev/block/zram0
#			busybox swapon -p 2 /dev/block/zram0
#			busybox sleep 0.5s
#			busybox sync
#		fi
#		if [ "2" == "$3" ]; then
#			busybox swapoff /dev/block/zram0
#			busybox swapoff /dev/block/zram1
#			busybox swapoff /dev/block/zram2
#			busybox swapoff /dev/block/zram3
#			echo "1" > /sys/block/zram0/reset
#			echo "1" > /sys/block/zram1/reset
#			echo "1" > /sys/block/zram2/reset
#			echo "1" > /sys/block/zram3/reset
#			echo "$4" > /sys/block/zram0/disksize
#			echo "$4" > /sys/block/zram1/disksize
#			echo "0" > /sys/block/zram2/disksize
#			echo "0" > /sys/block/zram3/disksize
#			busybox mkswap /dev/block/zram0
#			busybox mkswap /dev/block/zram1
#			busybox swapon -p 2 /dev/block/zram0
#			busybox swapon -p 2 /dev/block/zram1
#			busybox sleep 0.5s
#			busybox sync
#		fi
#		if [ "4" == "$3" ]; then
#			busybox swapoff /dev/block/zram0
#			busybox swapoff /dev/block/zram1
#			busybox swapoff /dev/block/zram2
#			busybox swapoff /dev/block/zram3
#			echo "1" > /sys/block/zram0/reset
#			echo "1" > /sys/block/zram1/reset
#			echo "1" > /sys/block/zram2/reset
#			echo "1" > /sys/block/zram3/reset
#			echo "$4" > /sys/block/zram0/disksize
#			echo "$4" > /sys/block/zram1/disksize
#			echo "$4" > /sys/block/zram2/disksize
#			echo "$4" > /sys/block/zram3/disksize
#			busybox mkswap /dev/block/zram0
#			busybox mkswap /dev/block/zram1
#			busybox mkswap /dev/block/zram2
#			busybox mkswap /dev/block/zram3
#			busybox swapon -p 2 /dev/block/zram0
#			busybox swapon -p 2 /dev/block/zram1
#			busybox swapon -p 2 /dev/block/zram2
#			busybox swapon -p 2 /dev/block/zram3
#			busybox sleep 0.5s
#			busybox sync
#		fi
#		echo "80" > /proc/sys/vm/swappiness
#	fi
#
#	if [ "0" == "$2" ]; then
#		busybox swapoff /dev/block/zram0
#		busybox swapoff /dev/block/zram1
#		busybox swapoff /dev/block/zram2
#		busybox swapoff /dev/block/zram3
#		echo "1" > /sys/block/zram0/reset
#		echo "1" > /sys/block/zram1/reset
#		echo "1" > /sys/block/zram2/reset
#		echo "1" > /sys/block/zram3/reset
#		busybox sleep 0.5s
#		busybox sync
#	fi
#	exit 0
#fi

if [ "apply_cifs" == "$1" ]; then
	if [ "1" == "$2" ]; then
		insmod $LIBPATH/cifs.ko
	fi

	if [ "0" == "$2" ]; then
		rmmod $LIBPATH/cifs.ko	
	fi
	exit 0
fi

if [ "apply_nfs" == "$1" ]; then
	if [ "1" == "$2" ]; then
		insmod $LIBPATH/sunrpc.ko
		insmod $LIBPATH/auth_rpcgss.ko
		insmod $LIBPATH/lockd.ko
		insmod $LIBPATH/nfs.ko
	fi

	if [ "0" == "$2" ]; then
		rmmod $LIBPATH/nfs.ko
		rmmod $LIBPATH/lockd.ko
		rmmod $LIBPATH/auth_rpcgss.ko
		rmmod $LIBPATH/sunrpc.ko
	fi
	exit 0
fi

if [ "apply_xbox" == "$1" ]; then
	if [ "1" == "$2" ]; then
		insmod $LIBPATH/xpad.ko
	fi

	if [ "0" == "$2" ]; then
		rmmod $LIBPATH/xpad.ko
	fi
	exit 0
fi

if [ "apply_exfat" == "$1" ]; then
	if [ "1" == "$2" ]; then
		insmod $LIBPATH/exfat_core.ko
		insmod $LIBPATH/exfat_fs.ko
	fi

	if [ "0" == "$2" ]; then
		rmmod $LIBPATH/exfat_fs.ko
		rmmod $LIBPATH/exfat_core.ko
	fi
	exit 0
fi

if [ "apply_usb_ethernet" == "$1" ]; then
	if [ "1" == "$2" ]; then
		insmod $LIBPATH/asix.ko
		netcfg eth0 up
		dhcpcd eth0
		DNS=`getprop net.eth0.dns1`
		ndc resolver setifdns eth0 "" $DNS  8.8.8.8
		ndc resolver setdefaultif eth0		
	fi

	if [ "0" == "$2" ]; then
		rmmod $LIBPATH/asix.ko
		netcfg eth0 down
	fi
	exit 0
fi

if [ "apply_ntfs" == "$1" ]; then
	if [ "1" == "$2" ]; then
		insmod $LIBPATH/ntfs.ko
	fi

	if [ "0" == "$2" ]; then
		rmmod $LIBPATH/ntfs.ko
	fi
	exit 0
fi

if [ "apply_ums" == "$1" ]; then
#	if [ "1" == "$2" ]; then
#		busybox umount -l /mnt/extSdCard
#		busybox umount -l /storage/sdcard1
#		busybox umount -l /mnt/media_rw/sdcard1
#		busybox umount -l /mnt/secure/asec
#		/system/bin/setprop persist.sys.usb.config mass_storage,adb
#		echo /dev/block/vold/179:49 > /sys/devices/platform/s3c-usbgadget/gadget/lun0/file
#	fi
#
#	if [ "0" == "$2" ]; then
#		echo "" > /sys/devices/platform/s3c-usbgadget/gadget/lun0/file
#		/system/bin/vold
#		/system/bin/setprop persist.sys.usb.config mtp,adb
#	fi
	exit 0
fi


# *******************
# Actions
# *******************

if [ "action_debug_info_file" == "$1" ]; then
	echo $(date) Full debug log file start > $2
	echo -e "\n============================================\n" >> $2

	echo -e "\n**** Boeffla-Kernel version\n" >> $2
	cat /proc/version >> $2

	echo -e "\n**** Firmware information\n" >> $2
	busybox grep ro.build.version /system/build.prop >> $2

	echo -e "\n============================================\n" >> $2

	echo -e "\n**** Boeffla-Kernel log\n" >> $2
	cat /sdcard/boeffla-kernel-data/boeffla-kernel.log >> $2

	echo -e "\n**** Boeffla-Kernel log 1\n" >> $2
	cat /sdcard/boeffla-kernel-data/boeffla-kernel.log.1 >> $2

	echo -e "\n**** Boeffla-Kernel log 2\n" >> $2
	cat /sdcard/boeffla-kernel-data/boeffla-kernel.log.2 >> $2

	echo -e "\n**** Boeffla-Kernel log 3\n" >> $2
	cat /sdcard/boeffla-kernel-data/boeffla-kernel.log.3 >> $2

	echo -e "\n============================================\n" >> $2

	echo -e "\n**** Boeffla-Config app log\n" >> $2
	cat /sdcard/boeffla-kernel-data/bc.log >> $2

	echo -e "\n**** Boeffla-Config app log 1\n" >> $2
	cat /sdcard/boeffla-kernel-data/bc.log.1 >> $2

	echo -e "\n**** Boeffla-Config app log 2\n" >> $2
	cat /sdcard/boeffla-kernel-data/bc.log.2 >> $2

	echo -e "\n**** Boeffla-Config app log 3\n" >> $2
	cat /sdcard/boeffla-kernel-data/bc.log.3 >> $2

	echo -e "\n**** Boeffla-Config crash log\n" >> $2
	cat /sdcard/boeffla-kernel-data/bc.crashlog >> $2

	echo -e "\n============================================\n" >> $2

	echo -e "\n**** boeffla_sound\n" >> $2
	cd /sys/class/misc/boeffla_sound
	busybox find * -print -maxdepth 0 -type f -exec tail -v -n +1 {} + >> $2

	echo "\n============================================\n" >> $2

	echo -e "\n**** SELinux:\n" >> $2
	getenforce >> $2

	echo -e "\n**** Loaded modules:\n" >> $2
	lsmod >> $2

	echo -e "\n**** CPU information:\n" >> $2
	cd /sys/devices/system/cpu/cpu0/cpufreq
	busybox find * -print -maxdepth 0 -type f -exec tail -v -n +1 {} + >> $2
	cd /sys/devices/system/cpu/cpu1/cpufreq
	busybox find * -print -maxdepth 0 -type f -exec tail -v -n +1 {} + >> $2
	cd /sys/devices/system/cpu/cpu2/cpufreq
	busybox find * -print -maxdepth 0 -type f -exec tail -v -n +1 {} + >> $2
	cd /sys/devices/system/cpu/cpu3/cpufreq
	busybox find * -print -maxdepth 0 -type f -exec tail -v -n +1 {} + >> $2

	echo -e "\n**** GPU information:\n" >> $2

	echo -e "\n**** Root:\n" >> $2
	ls /system/xbin/su >> $2
	ls /system/app/Superuser.apk >> $2

	echo -e "\n**** Busybox:\n" >> $2
	ls /sbin/busybox >> $2
	ls /system/bin/busybox >> $2
	ls /system/xbin/busybox >> $2

	echo -e "\n**** Mounts:\n" >> $2
	mount | busybox grep /data >> $2
	mount | busybox grep /cache >> $2

	echo -e "\n**** SD Card read ahead:\n" >> $2
	cat /sys/block/mmcblk0/bdi/read_ahead_kb >> $2

	echo -e "\n**** Various kernel settings by config app:\n" >> $2
	echo -e "\n(gov prof, cpu volt prof, gpu freq prof, gpu volt prof, eq prof, mdnie over, sys tweaks, swapp over)\n" >> $2
	cat /dev/bk_governor_profile >> $2
	cat /dev/bk_cpu_voltages_profile >> $2
	cat /dev/bk_gpu_frequencies_profile >> $2
	cat /dev/bk_gpu_voltages_profile >> $2
	cat /dev/bk_system_tweaks >> $2
	cat /dev/bk_swappiness_overwrite >> $2

	echo -e "\n**** Touch boost:\n" >> $2
	cd /sys/class/misc/touchboost_switch
	busybox find * -print -maxdepth 0 -type f -exec tail -v -n +1 {} + >> $2

	echo -e "\n**** Charging levels (ac/usb/wireless) and Charging instable power / ignore safety margin:\n" >> $2
	cd /sys/kernel/charge_levels
	busybox find * -print -maxdepth 0 -type f -exec tail -v -n +1 {} + >> $2

	echo -e "\n**** Governor:\n" >> $2
	cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor >> $2
	cat /sys/devices/system/cpu/cpu1/cpufreq/scaling_governor >> $2
	cat /sys/devices/system/cpu/cpu2/cpufreq/scaling_governor >> $2
	cat /sys/devices/system/cpu/cpu3/cpufreq/scaling_governor >> $2

	echo -e "\n**** Governor hard:\n" >> $2
	cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor_hard >> $2
	cat /sys/devices/system/cpu/cpu1/cpufreq/scaling_governor_hard >> $2
	cat /sys/devices/system/cpu/cpu2/cpufreq/scaling_governor_hard >> $2
	cat /sys/devices/system/cpu/cpu3/cpufreq/scaling_governor_hard >> $2

	echo -e "\n**** Scheduler:\n" >> $2
	cat /sys/block/mmcblk0/queue/scheduler >> $2

	echo -e "\n**** Scheduler hard:\n" >> $2
	cat /sys/block/mmcblk0/queue/scheduler_hard >> $2

	echo -e "\n**** Kernel Logger:\n" >> $2
	cat /sys/kernel/printk_mode/printk_mode >> $2

	echo -e "\n**** Android Logger:\n" >> $2
	cat /sys/kernel/logger_mode/logger_mode >> $2

	echo -e "\n**** LED information:\n" >> $2
	cd /sys/class/leds/red/device
	busybox find * -print -maxdepth 0 -type f -exec tail -v -n +1 {} + >> $2

	echo -e "\n**** Color control information:\n" >> $2
	cd /sys/devices/platform/kcal_ctrl.0
	busybox find * -print -maxdepth 0 -type f -exec tail -v -n +1 {} + >> $2
	
	echo -e "\n**** Swipe2wake information:\n" >> $2
	cd /proc/touchpanel
	busybox find * -print -maxdepth 0 -type f -exec tail -v -n +1 {} + >> $2

	echo -e "\n**** Swipe2sleep information:\n" >> $2
	cd /sys/android_touch
	busybox find * -print -maxdepth 0 -type f -exec tail -v -n +1 {} + >> $2

	echo -e "\n**** zRam information:\n" >> $2
	busybox find /sys/block/zram*/* -print -maxdepth 0 -type f -exec tail -v -n +1 {} + >> $2

	echo -e "\n**** Uptime:\n" >> $2
	cat /proc/uptime >> $2

	echo -e "\n**** Frequency usage table:\n" >> $2
	cat /sys/devices/system/cpu/cpu0/cpufreq/stats/time_in_state >> $2

	echo -e "\n**** Memory:\n" >> $2
	busybox free -m >> $2

	echo -e "\n**** Meminfo:\n" >> $2
	cat /proc/meminfo >> $2

	echo -e "\n**** Swap:\n" >> $2
	cat /proc/swaps >> $2

	echo -e "\n**** Low memory killer:\n" >> $2
	cat /sys/module/lowmemorykiller/parameters/minfree >> $2

	echo -e "\n**** Swappiness:\n" >> $2
	cat /proc/sys/vm/swappiness >> $2

	echo -e "\n**** Storage:\n" >> $2
	busybox df >> $2

	echo -e "\n**** Mounts:\n" >> $2
	mount >> $2

	echo -e "\n**** Governor tuneables\n" >> $2
	GOVERNOR=`cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor`
	cd /sys/devices/system/cpu/cpufreq/$GOVERNOR
	busybox find * -print -maxdepth 0 -type f -exec tail -v -n +1 {} + >> $2

	echo -e "\n============================================\n" >> $2

	echo -e "\n**** /data/app folder\n" >> $2
	ls -l /data/app >> $2

	echo -e "\n**** /system/app folder\n" >> $2
	ls -l /system/app >> $2

	echo -e "\n============================================\n" >> $2

	echo -e "\n**** /system/etc/init.d folder\n" >> $2
	ls -l /system/etc/init.d >> $2

	echo -e "\n**** /etc/init.d folder\n" >> $2
	ls -l /etc/init.d >> $2

	echo -e "\n**** /data/init.d folder\n" >> $2
	ls -l /data/init.d >> $2

	echo -e "\n============================================\n" >> $2

	echo -e "\n**** last_kmsg\n" >> $2
	cat /proc/last_kmsg  >> $2

	echo -e "\n============================================\n" >> $2

	echo -e "\n**** dmesg\n" >> $2
	dmesg  >> $2

	echo -e "\n============================================\n" >> $2
	echo $(date) Full debug log file end >> $2

	busybox chmod 666 $2
	exit 0
fi

if [ "action_reboot" == "$1" ]; then
	echo 0 > /sys/kernel/dyn_fsync/Dyn_fsync_active
	busybox sync
	busybox sleep 1s
	/system/bin/reboot
	exit 0
fi

if [ "action_reboot_cwm" == "$1" ]; then
	echo 0 > /sys/kernel/dyn_fsync/Dyn_fsync_active
	busybox sync
	busybox sleep 1s
	/system/bin/reboot recovery
	exit 0
fi

if [ "action_reboot_download" == "$1" ]; then
	echo 0 > /sys/kernel/dyn_fsync/Dyn_fsync_active
	busybox sync
	busybox sleep 1s
	/system/bin/reboot download
	exit 0
fi

if [ "action_wipe_caches_reboot" == "$1" ]; then
	echo 0 > /sys/kernel/dyn_fsync/Dyn_fsync_active
	busybox rm -rf /cache/*
	busybox rm -rf /data/dalvik-cache/*
	busybox sync
	busybox sleep 1s
	/system/bin/reboot
	exit 0
fi

if [ "action_wipe_cache" == "$1" ]; then
	busybox rm -rf /cache/*
	busybox sync
	busybox sleep 1s
	exit 0
fi

if [ "action_wipe_clipboard_cache" == "$1" ]; then
	busybox rm -rf /data/clipboard/*
	busybox sync
	exit 0
fi

if [ "action_clean_initd" == "$1" ]; then
	busybox tar cvz -f $2 /system/etc/init.d
	mount -o remount,rw -t ext4 $SYSTEM_DEVICE /system
	busybox rm /system/etc/init.d/*
	busybox sync
	mount -o remount,ro -t ext4 $SYSTEM_DEVICE /system
	exit 0
fi

if [ "action_fix_permissions" == "$1" ]; then
	mount -o remount,rw -t ext4 $SYSTEM_DEVICE /system

	# User apps
	busybox chmod 644 /data/app/*.apk
	busybox chown 1000:1000 /data/app/*.apk
	# System apps
	busybox chmod 644 /system/app/*.apk
	busybox chown 0:0 /system/app/*.apk
	# System framework
	busybox chmod 644 /system/framework/*.apk
	busybox chown 0:0 /system/framework/*.apk
	busybox chmod 644 /system/framework/*.jar
	busybox chown 0:0 /system/framework/*.jar

	mount -o remount,ro -t ext4 $SYSTEM_DEVICE /system
	busybox sync
	exit 0
fi

if [ "action_fstrim" == "$1" ]; then
	echo -e "Trim /data"
	/sbin/busybox fstrim -v /data
	echo -e ""
	echo -e "Trim /cache"
	/sbin/busybox fstrim -v /cache
	echo -e ""
	echo -e "Trim /system"
	/sbin/busybox fstrim -v /system
	echo -e ""
	busybox sync
	exit 0
fi


if [ "flash_kernel" == "$1" ]; then
	setenforce 0
	busybox dd if=$2 of=$BOOT_DEVICE
	exit 0
fi

if [ "archive_kernel" == "$1" ]; then
	IMGPATH=$2
	cd ${IMGPATH%/*}
	busybox rm $3.tar
	busybox rm $3.tar.md5
	busybox tar cvf $3.tar ${IMGPATH##*/}
	busybox md5sum $3.tar >> $3.tar
	busybox mv $3.tar $3.tar.md5
	busybox chmod 666 $3.tar.md5
	busybox rm $2
	busybox sync
	exit 0
fi

if [ "extract_kernel" == "$1" ]; then
	busybox tar -xvf $2 -C $3
	exit 0
fi

if [ "flash_recovery" == "$1" ]; then
	setenforce 0
	busybox dd if=$2 of=$RECOVERY_DEVICE
	exit 0
fi

if [ "extract_recovery" == "$1" ]; then
	busybox tar -xvf $2 -C $3
	exit 0
fi

if [ "flash_modem" == "$1" ]; then
	setenforce 0
	busybox dd if=$2 of=$RADIO_DEVICE
	exit 0
fi

if [ "extract_modem" == "$1" ]; then
	busybox tar -xvf $2 -C $3
	exit 0
fi

if [ "flash_cm_kernel" == "$1" ]; then
	setenforce 0
	busybox dd if=$2/boot.img of=$BOOT_DEVICE
	mount -o remount,rw -t ext4 $SYSTEM_DEVICE /system
	busybox mkdir -p $LIBPATH
	busybox chmod 755 $LIBPATH
	busybox rm -f $LIBPATH/*
	busybox cp $2$LIBPATH/* $LIBPATH
	busybox chmod 644 $LIBPATH/*
	busybox sync
	mount -o remount,ro -t ext4 $SYSTEM_DEVICE /system
	exit 0
fi

if [ "extract_cm_kernel" == "$1" ]; then
	busybox unzip $2 -d $3
	exit 0
fi
