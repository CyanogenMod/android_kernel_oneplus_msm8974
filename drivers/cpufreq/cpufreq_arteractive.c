/*
 * drivers/cpufreq/cpufreq_arteractive.c
 *
 * Copyright (C) 2010 Google, Inc.
 * Copyright (C) 2014 Samsung, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Author: Mike Chan (mike@android.com)
 *         Park Ju Hyung - bring-up from SM-G900F source drop (qkrwngud825@gmail.com)
 *
 * Version : 1.0 - Initial bring-up from SM-G900F_KK_Opensource_Update3.zip
 *                 Whitespace fix-ups
 *                 Apply upstream patches from https://github.com/android/kernel_common (branch android-3.4)
 *                 Remove unnecessary tracer
 *                 Bring-up Touchboost from intelliactive (by faux123)
 *                 Predefine some governor tunables
 *           1.1 - Predefine more governor tunables taken from stock klte firmware's init.qcom.post_boot.sh
 *           1.2 - Apply Samsung changes from SM-G900F_EUR_LL_Opensource.zip
 *           1.3 - Apply upstream patches from https://github.com/android/kernel_common (branch android-3.4)
 *                 (subsystem: CPU FREQUENCY DRIVERS- Set cpu_load calculation on current frequency)
 *           2.0 - Apply different go_hispeed_load when screen is off
 *                 (depends on powersuspend)
 *                 (inspired by https://github.com/CyanogenMod/android_device_lge_hammerhead/commit/e18d9f262ddad4d142be1fb14dd48cdc01e64dee )
 *                 Apply hispeed boost only on cpu0.
 *                     Boost in interactive is intended to ramp up the frequency on sudden workload burst,
 *                     and since most *sudden workload* is single-threaded, boosting up exclusively on cpu0 is quite enough.
 *                     Rest of the cores can slowly ramp up and match the actual workload.
 *                     This is not recommended on devices where reducing latency is very important.
 *                 Apply some several CAF(3.4 & 3.10) and Chromium(3.14) commits.
 *           2.1 - Disable mode change on suspend and optimize go_hispeed_load poll
 *
 */

#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/cpufreq.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/tick.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/kernel_stat.h>
#include <linux/powersuspend.h>
#include <asm/cputime.h>

#define CONFIG_MODE_AUTO_CHANGE
#define CONFIG_RETENTION_CHANGE

/*
 * Governor-side implemented Touchboost by Paul Reioux(faux123)
 *
 *    Setting this to 1 will activate Touchboost and use maximum frequency on screen touch input events.
 * Please set this to 0 if the source-tree has another Touchboost(e.g. TSP_BOOSTER) implemented.
 */
#define TOUCHBOOST 0

/*
 * Duration in usec to be used with Touchboost(interactive_input_event).
 * Default is 500000 usec(500 msec).
 */
#define TOUCHBOOST_DURATION 500000

#if TOUCHBOOST
#include <linux/input.h>
#endif

static int active_count;

struct cpufreq_interactive_cpuinfo {
	struct timer_list cpu_timer;
	struct timer_list cpu_slack_timer;
	spinlock_t load_lock; /* protects the next 4 fields */
	u64 time_in_idle;
	u64 time_in_idle_timestamp;
	u64 cputime_speedadj;
	u64 cputime_speedadj_timestamp;
	struct cpufreq_policy *policy;
	struct cpufreq_frequency_table *freq_table;
	spinlock_t target_freq_lock; /*protects target freq */
	unsigned int target_freq;
	unsigned int floor_freq;
	unsigned int max_freq;
	u64 floor_validate_time;
	u64 hispeed_validate_time;
	struct rw_semaphore enable_sem;
	int governor_enabled;
	int prev_load;
	int minfreq_boost;
};

static DEFINE_PER_CPU(struct cpufreq_interactive_cpuinfo, cpuinfo);

/* boolean for determining screen on/off state */
static bool suspended = false;

/* realtime thread handles frequency scaling */
static struct task_struct *speedchange_task;
static cpumask_t speedchange_cpumask;
static spinlock_t speedchange_cpumask_lock;
static struct mutex gov_lock;

/* Hi speed to bump to from lo speed when load burst (default max) */
static unsigned int hispeed_freq = 1190400;

/* Go to hi speed when CPU load at or above this value. */
#define DEFAULT_GO_HISPEED_LOAD 99
static unsigned long go_hispeed_load = DEFAULT_GO_HISPEED_LOAD;

/* Go to hi speed when CPU load at or above this value on screen-off state */
#define DEFAULT_GO_HISPEED_LOAD_SCREEN_OFF 110

/* Sampling down factor to be applied to min_sample_time at max freq */
static unsigned int sampling_down_factor = 100000;

/* Target load.  Lower values result in higher CPU speeds. */
#define DEFAULT_TARGET_LOAD 85
static unsigned int default_target_loads[] = {DEFAULT_TARGET_LOAD};
static spinlock_t target_loads_lock;
static unsigned int *target_loads = default_target_loads;
static int ntarget_loads = ARRAY_SIZE(default_target_loads);

/*
 * The minimum amount of time to spend at a frequency before we can ramp down.
 */
#define DEFAULT_MIN_SAMPLE_TIME 40000
static unsigned long min_sample_time = DEFAULT_MIN_SAMPLE_TIME;

/*
 * The sample rate of the timer used to increase frequency
 */
#define DEFAULT_TIMER_RATE 20000
static unsigned long timer_rate = DEFAULT_TIMER_RATE;

/* Busy SDF parameters*/
#define MIN_BUSY_TIME (100 * USEC_PER_MSEC)

/*
 * Wait this long before raising speed above hispeed, by default a single
 * timer interval.
 */
#define DEFAULT_ABOVE_HISPEED_DELAY 20000
static unsigned int default_above_hispeed_delay[] = {
	DEFAULT_ABOVE_HISPEED_DELAY };
static spinlock_t above_hispeed_delay_lock;
static unsigned int *above_hispeed_delay = default_above_hispeed_delay;
static int nabove_hispeed_delay = ARRAY_SIZE(default_above_hispeed_delay);

/* Non-zero means indefinite speed boost active */
static int boost_val;
/* Duration of a boot pulse in usecs */
static int boostpulse_duration_val = DEFAULT_MIN_SAMPLE_TIME;
/* End time of boost pulse in ktime converted to usecs */
static u64 boostpulse_endtime;

/*
 * Max additional time to wait in idle, beyond timer_rate, at speeds above
 * minimum before wakeup to reduce speed, or -1 if unnecessary.
 */
#define DEFAULT_TIMER_SLACK 20000
static int timer_slack_val = DEFAULT_TIMER_SLACK;

static bool io_is_busy = 1;

#ifdef CONFIG_MODE_AUTO_CHANGE
struct cpufreq_loadinfo {
	unsigned int load;
	unsigned int freq;
	u64 timestamp;
};

static DEFINE_PER_CPU(struct cpufreq_loadinfo, loadinfo);

static spinlock_t mode_lock;

#define MULTI_MODE	2
#define SINGLE_MODE	1
#define NO_MODE	0

static unsigned int mode = 0;
static unsigned int enforced_mode = 0;
static u64 mode_check_timestamp = 0;

#define DEFAULT_MULTI_ENTER_TIME (4 * DEFAULT_TIMER_RATE)
static unsigned long multi_enter_time = DEFAULT_MULTI_ENTER_TIME;
static unsigned long time_in_multi_enter = 0;
static unsigned int multi_enter_load = 4 * DEFAULT_TARGET_LOAD;

#define DEFAULT_MULTI_EXIT_TIME (16 * DEFAULT_TIMER_RATE)
static unsigned long multi_exit_time = DEFAULT_MULTI_EXIT_TIME;
static unsigned long time_in_multi_exit = 0;
static unsigned int multi_exit_load = 4 * DEFAULT_TARGET_LOAD;

#define DEFAULT_SINGLE_ENTER_TIME (8 * DEFAULT_TIMER_RATE)
static unsigned long single_enter_time = DEFAULT_SINGLE_ENTER_TIME;
static unsigned long time_in_single_enter = 0;
static unsigned int single_enter_load = DEFAULT_TARGET_LOAD;

#define DEFAULT_SINGLE_EXIT_TIME (4 * DEFAULT_TIMER_RATE)
static unsigned long single_exit_time = DEFAULT_SINGLE_EXIT_TIME;
static unsigned long time_in_single_exit = 0;
static unsigned int single_exit_load = DEFAULT_TARGET_LOAD;

static unsigned int param_index = 0;
static unsigned int cur_param_index = 0;

#define MAX_PARAM_SET 4 /* ((MULTI_MODE | SINGLE_MODE | NO_MODE) + 1) */
static unsigned int hispeed_freq_set[MAX_PARAM_SET];
static unsigned long go_hispeed_load_set[MAX_PARAM_SET];
static unsigned int *target_loads_set[MAX_PARAM_SET];
static int ntarget_loads_set[MAX_PARAM_SET];
static unsigned long min_sample_time_set[MAX_PARAM_SET];
static unsigned long timer_rate_set[MAX_PARAM_SET];
static unsigned int *above_hispeed_delay_set[MAX_PARAM_SET];
static int nabove_hispeed_delay_set[MAX_PARAM_SET];
static unsigned int sampling_down_factor_set[MAX_PARAM_SET];
#endif /* CONFIG_MODE_AUTO_CHANGE */
#ifdef CONFIG_RETENTION_CHANGE
static void do_toggle_retention(struct work_struct *work);
extern void msm_pm_retention_mode_enable(bool enable);
static struct workqueue_struct *retention_toggle_wq;
static struct work_struct retention_toggle_work;
static int mode_count = 0;
#endif
/*
 * If the max load among other CPUs is higher than up_threshold_any_cpu_load
 * and if the highest frequency among the other CPUs is higher than
 * up_threshold_any_cpu_freq then do not let the frequency to drop below
 * sync_freq
 */
static unsigned int up_threshold_any_cpu_load = 95;
static unsigned int sync_freq = 729600;
static unsigned int up_threshold_any_cpu_freq = 960000;

static int cpufreq_governor_arteractive(struct cpufreq_policy *policy,
		unsigned int event);

#define DYN_DEFER (1)

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_ARTERACTIVE
static
#endif
struct cpufreq_governor cpufreq_gov_arteractive = {
	.name = "arteractive",
	.governor = cpufreq_governor_arteractive,
	.max_transition_latency = 10000000,
	.owner = THIS_MODULE,
};

static inline cputime64_t get_cpu_idle_time_jiffy(unsigned int cpu,
						  cputime64_t *wall)
{
	u64 idle_time;
	u64 cur_wall_time;
	u64 busy_time;

	cur_wall_time = jiffies64_to_cputime64(get_jiffies_64());

	busy_time  = kcpustat_cpu(cpu).cpustat[CPUTIME_USER];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_SYSTEM];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_IRQ];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_SOFTIRQ];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_STEAL];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_NICE];

	idle_time = cur_wall_time - busy_time;
	if (wall)
		*wall = jiffies_to_usecs(cur_wall_time);

	return jiffies_to_usecs(idle_time);
}

#ifdef DYN_DEFER
static inline void timer_set_nondeferrable(struct timer_list *timer)
{
       timer->base =
	       ((struct tvec_base *)((unsigned long)timer->base &
		       ~TBASE_DEFERRABLE_FLAG));
}

static inline void timer_set_deferrable(struct timer_list *timer)
{
       timer->base =
	       ((struct tvec_base *)((unsigned long)timer->base |
		       TBASE_DEFERRABLE_FLAG));
}
#endif

static inline cputime64_t get_cpu_idle_time(unsigned int cpu,
					    cputime64_t *wall)
{
	u64 idle_time = get_cpu_idle_time_us(cpu, wall);

	if (idle_time == -1ULL)
		idle_time = get_cpu_idle_time_jiffy(cpu, wall);
	else if (!io_is_busy)
		idle_time += get_cpu_iowait_time_us(cpu, wall);

	return idle_time;
}

static void cpufreq_interactive_timer_resched(
	struct cpufreq_interactive_cpuinfo *pcpu)
{
	unsigned long expires;
	unsigned long flags;

	spin_lock_irqsave(&pcpu->load_lock, flags);
	pcpu->time_in_idle =
		get_cpu_idle_time(smp_processor_id(),
				     &pcpu->time_in_idle_timestamp);
	pcpu->cputime_speedadj = 0;
	pcpu->cputime_speedadj_timestamp = pcpu->time_in_idle_timestamp;
	expires = jiffies + usecs_to_jiffies(timer_rate);

#ifdef DYN_DEFER
       if (pcpu->target_freq > pcpu->policy->min)
	       timer_set_nondeferrable(&pcpu->cpu_timer);
       else
	       timer_set_deferrable(&pcpu->cpu_timer);
#endif

	mod_timer_pinned(&pcpu->cpu_timer, expires);

	if (timer_slack_val >= 0 && pcpu->target_freq > pcpu->policy->min) {
		expires += usecs_to_jiffies(timer_slack_val);
		mod_timer_pinned(&pcpu->cpu_slack_timer, expires);
	}

	spin_unlock_irqrestore(&pcpu->load_lock, flags);
}

/* The caller shall take enable_sem write semaphore to avoid any timer race.
 * The cpu_timer and cpu_slack_timer must be deactivated when calling this
 * function.
 */
static void cpufreq_interactive_timer_start(int cpu)
{
	struct cpufreq_interactive_cpuinfo *pcpu = &per_cpu(cpuinfo, cpu);
	unsigned long expires = jiffies + usecs_to_jiffies(timer_rate);
	unsigned long flags;

	pcpu->cpu_timer.expires = expires;
	del_timer_sync(&pcpu->cpu_timer);
	add_timer_on(&pcpu->cpu_timer, cpu);
	if (timer_slack_val >= 0 && pcpu->target_freq > pcpu->policy->min) {
		expires += usecs_to_jiffies(timer_slack_val);
		pcpu->cpu_slack_timer.expires = expires;
		del_timer_sync(&pcpu->cpu_slack_timer);
		add_timer_on(&pcpu->cpu_slack_timer, cpu);
	}

	spin_lock_irqsave(&pcpu->load_lock, flags);
	pcpu->time_in_idle =
		get_cpu_idle_time(cpu, &pcpu->time_in_idle_timestamp);
	pcpu->cputime_speedadj = 0;
	pcpu->cputime_speedadj_timestamp = pcpu->time_in_idle_timestamp;
	spin_unlock_irqrestore(&pcpu->load_lock, flags);
}

static unsigned int freq_to_above_hispeed_delay(unsigned int freq)
{
	int i;
	unsigned int ret;
	unsigned long flags;

	spin_lock_irqsave(&above_hispeed_delay_lock, flags);

	for (i = 0; i < nabove_hispeed_delay - 1 &&
			freq >= above_hispeed_delay[i+1]; i += 2)
		;

	ret = above_hispeed_delay[i];
	ret = (ret > (1 * USEC_PER_MSEC)) ? (ret - (1 * USEC_PER_MSEC)) : ret;

	spin_unlock_irqrestore(&above_hispeed_delay_lock, flags);
	return ret;
}

static unsigned int freq_to_targetload(unsigned int freq)
{
	int i;
	unsigned int ret;
	unsigned long flags;

	spin_lock_irqsave(&target_loads_lock, flags);

	for (i = 0; i < ntarget_loads - 1 && freq >= target_loads[i+1]; i += 2)
		;

	ret = target_loads[i];
	spin_unlock_irqrestore(&target_loads_lock, flags);
	return ret;
}

/*
 * If increasing frequencies never map to a lower target load then
 * choose_freq() will find the minimum frequency that does not exceed its
 * target load given the current load.
 */

static unsigned int choose_freq(
	struct cpufreq_interactive_cpuinfo *pcpu, unsigned int loadadjfreq)
{
	unsigned int freq = pcpu->policy->cur;
	unsigned int prevfreq, freqmin, freqmax;
	unsigned int tl;
	int index;

	freqmin = 0;
	freqmax = UINT_MAX;

	do {
		prevfreq = freq;
		tl = freq_to_targetload(freq);

		/*
		 * Find the lowest frequency where the computed load is less
		 * than or equal to the target load.
		 */

		if (cpufreq_frequency_table_target(
			    pcpu->policy, pcpu->freq_table, loadadjfreq / tl,
			    CPUFREQ_RELATION_L, &index))
			break;
		freq = pcpu->freq_table[index].frequency;

		if (freq > prevfreq) {
			/* The previous frequency is too low. */
			freqmin = prevfreq;

			if (freq >= freqmax) {
				/*
				 * Find the highest frequency that is less
				 * than freqmax.
				 */
				if (cpufreq_frequency_table_target(
					    pcpu->policy, pcpu->freq_table,
					    freqmax - 1, CPUFREQ_RELATION_H,
					    &index))
					break;
				freq = pcpu->freq_table[index].frequency;

				if (freq == freqmin) {
					/*
					 * The first frequency below freqmax
					 * has already been found to be too
					 * low.  freqmax is the lowest speed
					 * we found that is fast enough.
					 */
					freq = freqmax;
					break;
				}
			}
		} else if (freq < prevfreq) {
			/* The previous frequency is high enough. */
			freqmax = prevfreq;

			if (freq <= freqmin) {
				/*
				 * Find the lowest frequency that is higher
				 * than freqmin.
				 */
				if (cpufreq_frequency_table_target(
					    pcpu->policy, pcpu->freq_table,
					    freqmin + 1, CPUFREQ_RELATION_L,
					    &index))
					break;
				freq = pcpu->freq_table[index].frequency;

				/*
				 * If freqmax is the first frequency above
				 * freqmin then we have already found that
				 * this speed is fast enough.
				 */
				if (freq == freqmax)
					break;
			}
		}

		/* If same frequency chosen as previous then done. */
	} while (freq != prevfreq);

	return freq;
}

static u64 update_load(int cpu)
{
	struct cpufreq_interactive_cpuinfo *pcpu = &per_cpu(cpuinfo, cpu);
	u64 now;
	u64 now_idle;
	unsigned int delta_idle;
	unsigned int delta_time;
	u64 active_time;
#if defined(CONFIG_SEC_PM) || defined(CONFIG_MODE_AUTO_CHANGE)
	unsigned int cur_load = 0;
#endif
#ifdef CONFIG_MODE_AUTO_CHANGE
	struct cpufreq_loadinfo *cur_loadinfo = &per_cpu(loadinfo, cpu);
#endif

	now_idle = get_cpu_idle_time(cpu, &now);
	delta_idle = (unsigned int)(now_idle - pcpu->time_in_idle);
	delta_time = (unsigned int)(now - pcpu->time_in_idle_timestamp);

	if (delta_time <= delta_idle)
		active_time = 0;
	else
		active_time = delta_time - delta_idle;

	pcpu->cputime_speedadj += active_time * pcpu->policy->cur;

	pcpu->time_in_idle = now_idle;
	pcpu->time_in_idle_timestamp = now;

#if defined(CONFIG_SEC_PM) || defined(CONFIG_MODE_AUTO_CHANGE)
	cur_load = (unsigned int)(active_time * 100) / delta_time;
#endif
#ifdef CONFIG_SEC_PM
	pcpu->policy->load_at_max = (cur_load * pcpu->policy->cur) /
		pcpu->policy->cpuinfo.max_freq;
#endif
#ifdef CONFIG_MODE_AUTO_CHANGE
	cur_loadinfo->load = (cur_load * pcpu->policy->cur) /
									pcpu->policy->cpuinfo.max_freq;
	cur_loadinfo->freq = pcpu->policy->cur;
	cur_loadinfo->timestamp = now;
#endif

	return now;
}

#ifdef CONFIG_MODE_AUTO_CHANGE
static unsigned int check_mode(int cpu, unsigned int cur_mode, u64 now)
{
	int i;
	unsigned int ret=cur_mode, total_load=0, max_single_load=0;
	struct cpufreq_loadinfo *cur_loadinfo;

	if (now - mode_check_timestamp < timer_rate - 1000)
		return ret;

	if (now - mode_check_timestamp > timer_rate + 1000)
		mode_check_timestamp = now - timer_rate;

	for_each_online_cpu(i) {
		cur_loadinfo = &per_cpu(loadinfo, i);
		total_load += cur_loadinfo->load;
		if (cur_loadinfo->load > max_single_load)
			max_single_load = cur_loadinfo->load;
	}

	if (!(cur_mode & SINGLE_MODE)) {
		if (max_single_load >= single_enter_load)
			time_in_single_enter += now - mode_check_timestamp;
		else
			time_in_single_enter = 0;

		if (time_in_single_enter >= single_enter_time)
			ret |= SINGLE_MODE;
	}

	if (!(cur_mode & MULTI_MODE)) {
		if (total_load >= multi_enter_load)
			time_in_multi_enter += now - mode_check_timestamp;
		else
			time_in_multi_enter = 0;

		if (time_in_multi_enter >= multi_enter_time)
			ret |= MULTI_MODE;
	}

	if (cur_mode & SINGLE_MODE) {
		if (max_single_load < single_exit_load)
			time_in_single_exit += now - mode_check_timestamp;
		else
			time_in_single_exit = 0;

		if (time_in_single_exit >= single_exit_time)
			ret &= ~SINGLE_MODE;
	}

	if (cur_mode & MULTI_MODE) {
		if (total_load < multi_exit_load)
			time_in_multi_exit += now - mode_check_timestamp;
		else
			time_in_multi_exit = 0;

		if (time_in_multi_exit >= multi_exit_time)
			ret &= ~MULTI_MODE;
	}

	if (time_in_single_enter >= single_enter_time)
		time_in_single_enter = 0;
	if (time_in_multi_enter >= multi_enter_time)
		time_in_multi_enter = 0;
	if (time_in_single_exit >= single_exit_time)
		time_in_single_exit = 0;
	if (time_in_multi_exit >= multi_exit_time)
		time_in_multi_exit = 0;
	mode_check_timestamp = now;

	return ret;
}

static void set_new_param_set(unsigned int index)
{
	unsigned long flags;

	hispeed_freq = hispeed_freq_set[index];
	go_hispeed_load = go_hispeed_load_set[index];

	spin_lock_irqsave(&target_loads_lock, flags);
	target_loads = target_loads_set[index];
	ntarget_loads = ntarget_loads_set[index];
	spin_unlock_irqrestore(&target_loads_lock, flags);

	min_sample_time = min_sample_time_set[index];
	timer_rate = timer_rate_set[index];

	spin_lock_irqsave(&above_hispeed_delay_lock, flags);
	above_hispeed_delay = above_hispeed_delay_set[index];
	nabove_hispeed_delay = nabove_hispeed_delay_set[index];
	spin_unlock_irqrestore(&above_hispeed_delay_lock, flags);

	cur_param_index = index;
}

static void enter_mode(void)
{
#if 1
	set_new_param_set(mode);
#else
	set_new_param_set(1);
#endif
#ifdef CONFIG_RETENTION_CHANGE
	queue_work(retention_toggle_wq, &retention_toggle_work);
#endif
}

static void exit_mode(void)
{
	set_new_param_set(0);
#ifdef CONFIG_RETENTION_CHANGE
	queue_work(retention_toggle_wq, &retention_toggle_work);
#endif
}
#endif

static void __cpufreq_interactive_timer(unsigned long data, bool is_notif)
{
	u64 now;
	unsigned int delta_time;
	u64 cputime_speedadj;
	int cpu_load;
	struct cpufreq_interactive_cpuinfo *pcpu =
		&per_cpu(cpuinfo, data);
	unsigned int new_freq;
	unsigned int loadadjfreq;
	unsigned int index;
	unsigned long flags;
	unsigned int this_hispeed_freq;
	bool boosted;
	unsigned long mod_min_sample_time;
	int i, max_load;
	unsigned int max_freq;
	struct cpufreq_interactive_cpuinfo *picpu;
#ifdef CONFIG_MODE_AUTO_CHANGE
	unsigned int new_mode;
#endif
	if (!down_read_trylock(&pcpu->enable_sem))
		return;
	if (!pcpu->governor_enabled)
		goto exit;

	spin_lock_irqsave(&pcpu->load_lock, flags);
	now = update_load(data);
	delta_time = (unsigned int)(now - pcpu->cputime_speedadj_timestamp);
	cputime_speedadj = pcpu->cputime_speedadj;
	spin_unlock_irqrestore(&pcpu->load_lock, flags);

	if (WARN_ON_ONCE(!delta_time))
		goto rearm;

#ifdef CONFIG_MODE_AUTO_CHANGE
	if (!suspended) {
		spin_lock_irqsave(&mode_lock, flags);
		if (enforced_mode)
			new_mode = enforced_mode;
		else
			new_mode = check_mode(data, mode, now);
		if (new_mode != mode) {
			mode = new_mode;
			if (new_mode & MULTI_MODE || new_mode & SINGLE_MODE) {
#ifdef CONFIG_RETENTION_CHANGE
				++mode_count;
#endif
				pr_info("Governor: enter mode 0x%x\n", mode);
				enter_mode();
			} else {
#ifdef CONFIG_RETENTION_CHANGE
				mode_count=0;
#endif
				pr_info("Governor: exit mode 0x%x\n", mode);
				exit_mode();
			}
		}
		spin_unlock_irqrestore(&mode_lock, flags);
	}
#endif

	spin_lock_irqsave(&pcpu->target_freq_lock, flags);
	do_div(cputime_speedadj, delta_time);
	loadadjfreq = (unsigned int)cputime_speedadj * 100;
	cpu_load = loadadjfreq / pcpu->policy->cur;
	pcpu->prev_load = cpu_load;
	boosted = boost_val || now < boostpulse_endtime;
	this_hispeed_freq = max(hispeed_freq, pcpu->policy->min);

#ifdef CONFIG_SEC_PM
	pcpu->policy->util = cpu_load;
#endif

	if (cpu_load >= go_hispeed_load || boosted) {
		if (pcpu->policy->cpu == 0) {
			if (pcpu->target_freq < this_hispeed_freq) {
				new_freq = this_hispeed_freq;
			} else {
				new_freq = choose_freq(pcpu, loadadjfreq);

				if (new_freq < this_hispeed_freq)
					new_freq = this_hispeed_freq;
			}
		}
#if 0 /* for debugging purpose */
		  else {
			pr_info("%s: not boosting up cpu%d\n", __func__, pcpu->policy->cpu);
		}
#endif
	} else {
		new_freq = choose_freq(pcpu, loadadjfreq);

		if (sync_freq && new_freq < sync_freq) {

			max_load = 0;
			max_freq = 0;

			for_each_online_cpu(i) {
				picpu = &per_cpu(cpuinfo, i);

				if (i == data || picpu->prev_load <
						up_threshold_any_cpu_load)
					continue;

				max_load = max(max_load, picpu->prev_load);
				max_freq = max(max_freq, picpu->target_freq);
			}

			if (max_freq > up_threshold_any_cpu_freq &&
				max_load >= up_threshold_any_cpu_load)
				new_freq = sync_freq;
		}
	}

	if (pcpu->target_freq >= this_hispeed_freq &&
	    new_freq > pcpu->target_freq &&
	    now - pcpu->hispeed_validate_time <
	    freq_to_above_hispeed_delay(pcpu->target_freq)) {
		spin_unlock_irqrestore(&pcpu->target_freq_lock, flags);
		goto rearm;
	}

	pcpu->hispeed_validate_time = now;

	if (cpufreq_frequency_table_target(pcpu->policy, pcpu->freq_table,
					   new_freq, CPUFREQ_RELATION_L,
					   &index)) {
		spin_unlock_irqrestore(&pcpu->target_freq_lock, flags);
		goto rearm;
	}

	new_freq = pcpu->freq_table[index].frequency;

	/*
	 * Do not scale below floor_freq unless we have been at or above the
	 * floor frequency for the minimum sample time since last validated.
	 */
	if (sampling_down_factor && pcpu->policy->cur == pcpu->policy->max){
		mod_min_sample_time = sampling_down_factor;
		pcpu->minfreq_boost=0;
	}
	else
		mod_min_sample_time = min_sample_time;

	if (pcpu->minfreq_boost) {
		mod_min_sample_time = 0;
		pcpu->minfreq_boost = 0;
	}
	if (!is_notif && new_freq < pcpu->floor_freq) {
		if (now - pcpu->floor_validate_time < mod_min_sample_time) {
			spin_unlock_irqrestore(&pcpu->target_freq_lock, flags);
			goto rearm;
		}
	}

	/*
	 * Update the timestamp for checking whether speed has been held at
	 * or above the selected frequency for a minimum of min_sample_time,
	 * if not boosted to this_hispeed_freq.  If boosted to this_hispeed_freq
	 * then we allow the speed to drop as soon as the boostpulse duration
	 * expires (or the indefinite boost is turned off).
	 */

	if (!boosted || new_freq > this_hispeed_freq) {
		pcpu->floor_freq = new_freq;
		pcpu->floor_validate_time = now;
	}

	if (pcpu->target_freq == new_freq) {
		spin_unlock_irqrestore(&pcpu->target_freq_lock, flags);
		goto rearm_if_notmax;
	}

	pcpu->target_freq = new_freq;
	spin_unlock_irqrestore(&pcpu->target_freq_lock, flags);
	spin_lock_irqsave(&speedchange_cpumask_lock, flags);
	cpumask_set_cpu(data, &speedchange_cpumask);
	spin_unlock_irqrestore(&speedchange_cpumask_lock, flags);
	wake_up_process(speedchange_task);

rearm_if_notmax:
	/*
	 * Already set max speed and don't see a need to change that,
	 * wait until next idle to re-evaluate, don't need timer.
	 */
	if (pcpu->target_freq == pcpu->policy->max)
#ifdef CONFIG_MODE_AUTO_CHANGE
		goto rearm;
#else
		goto exit;
#endif

rearm:
	if (!timer_pending(&pcpu->cpu_timer))
		cpufreq_interactive_timer_resched(pcpu);

exit:
	up_read(&pcpu->enable_sem);
	return;
}

static void cpufreq_interactive_idle_start(void)
{
	struct cpufreq_interactive_cpuinfo *pcpu =
		&per_cpu(cpuinfo, smp_processor_id());
	int pending;
	u64 now;

	if (!down_read_trylock(&pcpu->enable_sem))
		return;
	if (!pcpu->governor_enabled) {
		up_read(&pcpu->enable_sem);
		return;
	}

	pending = timer_pending(&pcpu->cpu_timer);

	if (pcpu->target_freq != pcpu->policy->min) {
		/*
		 * Entering idle while not at lowest speed.  On some
		 * platforms this can hold the other CPU(s) at that speed
		 * even though the CPU is idle. Set a timer to re-evaluate
		 * speed so this idle CPU doesn't hold the other CPUs above
		 * min indefinitely.  This should probably be a quirk of
		 * the CPUFreq driver.
		 */
		if (!pending) {
			cpufreq_interactive_timer_resched(pcpu);

			now = ktime_to_us(ktime_get());
			if ((pcpu->policy->cur == pcpu->policy->max) &&
				(now - pcpu->hispeed_validate_time) >
							MIN_BUSY_TIME) {
				pcpu->floor_validate_time = now;
			}

		}
	}

	up_read(&pcpu->enable_sem);
}

static void cpufreq_interactive_idle_end(void)
{
	struct cpufreq_interactive_cpuinfo *pcpu =
		&per_cpu(cpuinfo, smp_processor_id());

	if (!down_read_trylock(&pcpu->enable_sem))
		return;
	if (!pcpu->governor_enabled) {
		up_read(&pcpu->enable_sem);
		return;
	}

	/* Arm the timer for 1-2 ticks later if not already. */
	if (!timer_pending(&pcpu->cpu_timer)) {
		cpufreq_interactive_timer_resched(pcpu);
	} else if (time_after_eq(jiffies, pcpu->cpu_timer.expires)) {
		del_timer(&pcpu->cpu_timer);
		del_timer(&pcpu->cpu_slack_timer);
		__cpufreq_interactive_timer(smp_processor_id(), true);
	}

	up_read(&pcpu->enable_sem);
}

static int cpufreq_interactive_speedchange_task(void *data)
{
	unsigned int cpu;
	cpumask_t tmp_mask;
	unsigned long flags;
	struct cpufreq_interactive_cpuinfo *pcpu;

	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		spin_lock_irqsave(&speedchange_cpumask_lock, flags);

		if (cpumask_empty(&speedchange_cpumask)) {
			spin_unlock_irqrestore(&speedchange_cpumask_lock,
					       flags);
			schedule();

			if (kthread_should_stop())
				break;

			spin_lock_irqsave(&speedchange_cpumask_lock, flags);
		}

		set_current_state(TASK_RUNNING);
		tmp_mask = speedchange_cpumask;
		cpumask_clear(&speedchange_cpumask);
		spin_unlock_irqrestore(&speedchange_cpumask_lock, flags);

		for_each_cpu(cpu, &tmp_mask) {
			unsigned int j;
			unsigned int max_freq = 0;

			pcpu = &per_cpu(cpuinfo, cpu);
			if (!down_read_trylock(&pcpu->enable_sem))
				continue;
			if (!pcpu->governor_enabled) {
				up_read(&pcpu->enable_sem);
				continue;
			}

			for_each_cpu(j, pcpu->policy->cpus) {
				struct cpufreq_interactive_cpuinfo *pjcpu =
					&per_cpu(cpuinfo, j);

				if (pjcpu->target_freq > max_freq)
					max_freq = pjcpu->target_freq;
			}

			if (max_freq != pcpu->policy->cur)
				__cpufreq_driver_target(pcpu->policy,
							max_freq,
							CPUFREQ_RELATION_H);

			up_read(&pcpu->enable_sem);
		}
	}

	return 0;
}

static void cpufreq_interactive_boost(void)
{
	int i;
	int anyboost = 0;
	unsigned long flags[2];
	struct cpufreq_interactive_cpuinfo *pcpu;

	spin_lock_irqsave(&speedchange_cpumask_lock, flags[0]);

	for_each_online_cpu(i) {
		pcpu = &per_cpu(cpuinfo, i);
		spin_lock_irqsave(&pcpu->target_freq_lock, flags[1]);
		if (pcpu->target_freq < hispeed_freq) {
			pcpu->target_freq = hispeed_freq;
			cpumask_set_cpu(i, &speedchange_cpumask);
			pcpu->hispeed_validate_time =
				ktime_to_us(ktime_get());
			anyboost = 1;
		}

		/*
		 * Set floor freq and (re)start timer for when last
		 * validated.
		 */

		pcpu->floor_freq = hispeed_freq;
		pcpu->floor_validate_time = ktime_to_us(ktime_get());
		spin_unlock_irqrestore(&pcpu->target_freq_lock, flags[1]);
	}

	spin_unlock_irqrestore(&speedchange_cpumask_lock, flags[0]);

	if (anyboost)
		wake_up_process(speedchange_task);
}

static int cpufreq_interactive_notifier(
	struct notifier_block *nb, unsigned long val, void *data)
{
	struct cpufreq_freqs *freq = data;
	struct cpufreq_interactive_cpuinfo *pcpu;
	int cpu;
	unsigned long flags;

	if (val == CPUFREQ_PRECHANGE) {
		pcpu = &per_cpu(cpuinfo, freq->cpu);
		if (!down_read_trylock(&pcpu->enable_sem))
			return 0;
		if (!pcpu->governor_enabled) {
			up_read(&pcpu->enable_sem);
			return 0;
		}

		for_each_cpu(cpu, pcpu->policy->cpus) {
			struct cpufreq_interactive_cpuinfo *pjcpu =
				&per_cpu(cpuinfo, cpu);
			if (cpu != freq->cpu) {
				if (!down_read_trylock(&pjcpu->enable_sem))
					continue;
				if (!pjcpu->governor_enabled) {
					up_read(&pjcpu->enable_sem);
					continue;
				}
			}
			spin_lock_irqsave(&pjcpu->load_lock, flags);
			update_load(cpu);
			spin_unlock_irqrestore(&pjcpu->load_lock, flags);
			if (cpu != freq->cpu)
				up_read(&pjcpu->enable_sem);
		}

		up_read(&pcpu->enable_sem);
	}
	return 0;
}

static struct notifier_block cpufreq_notifier_block = {
	.notifier_call = cpufreq_interactive_notifier,
};

static unsigned int *get_tokenized_data(const char *buf, int *num_tokens)
{
	const char *cp;
	int i;
	int ntokens = 1;
	unsigned int *tokenized_data;
	int err = -EINVAL;

	cp = buf;
	while ((cp = strpbrk(cp + 1, " :")))
		ntokens++;

	if (!(ntokens & 0x1))
		goto err;

	tokenized_data = kmalloc(ntokens * sizeof(unsigned int), GFP_KERNEL);
	if (!tokenized_data) {
		err = -ENOMEM;
		goto err;
	}

	cp = buf;
	i = 0;
	while (i < ntokens) {
		if (sscanf(cp, "%u", &tokenized_data[i++]) != 1)
			goto err_kfree;

		cp = strpbrk(cp, " :");
		if (!cp)
			break;
		cp++;
	}

	if (i != ntokens)
		goto err_kfree;

	*num_tokens = ntokens;
	return tokenized_data;

err_kfree:
	kfree(tokenized_data);
err:
	return ERR_PTR(err);
}

static ssize_t show_target_loads(
	struct kobject *kobj, struct attribute *attr, char *buf)
{
	int i;
	ssize_t ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&target_loads_lock, flags);

#ifdef CONFIG_MODE_AUTO_CHANGE
	for (i = 0; i < ntarget_loads_set[param_index]; i++)
		ret += sprintf(buf + ret, "%u%s", target_loads_set[param_index][i],
			       i & 0x1 ? ":" : " ");
#else
	for (i = 0; i < ntarget_loads; i++)
		ret += sprintf(buf + ret, "%u%s", target_loads[i],
			       i & 0x1 ? ":" : " ");
#endif

	sprintf(buf + ret - 1, "\n");
	spin_unlock_irqrestore(&target_loads_lock, flags);
	return ret;
}

static ssize_t store_target_loads(
	struct kobject *kobj, struct attribute *attr, const char *buf,
	size_t count)
{
	int ntokens;
	unsigned int *new_target_loads = NULL;
	unsigned long flags;
#ifdef CONFIG_MODE_AUTO_CHANGE
	unsigned long flags2;
#endif

	new_target_loads = get_tokenized_data(buf, &ntokens);
	if (IS_ERR(new_target_loads))
		return PTR_RET(new_target_loads);

#ifdef CONFIG_MODE_AUTO_CHANGE
	spin_lock_irqsave(&mode_lock, flags2);
#endif
	spin_lock_irqsave(&target_loads_lock, flags);
#ifdef CONFIG_MODE_AUTO_CHANGE
	if (target_loads_set[param_index] != default_target_loads)
		kfree(target_loads_set[param_index]);
	target_loads_set[param_index] = new_target_loads;
	ntarget_loads_set[param_index] = ntokens;
	if (cur_param_index == param_index) {
		target_loads = new_target_loads;
		ntarget_loads = ntokens;
	}
#else
	if (target_loads != default_target_loads)
		kfree(target_loads);
	target_loads = new_target_loads;
	ntarget_loads = ntokens;
#endif
	spin_unlock_irqrestore(&target_loads_lock, flags);
#ifdef CONFIG_MODE_AUTO_CHANGE
	spin_unlock_irqrestore(&mode_lock, flags2);
#endif
	return count;
}

static struct global_attr target_loads_attr =
	__ATTR(target_loads, S_IRUGO | S_IWUSR,
		show_target_loads, store_target_loads);

static ssize_t show_above_hispeed_delay(
	struct kobject *kobj, struct attribute *attr, char *buf)
{
	int i;
	ssize_t ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&above_hispeed_delay_lock, flags);

#ifdef CONFIG_MODE_AUTO_CHANGE
	for (i = 0; i < nabove_hispeed_delay_set[param_index]; i++)
		ret += sprintf(buf + ret, "%u%s", above_hispeed_delay_set[param_index][i],
			       i & 0x1 ? ":" : " ");
#else
	for (i = 0; i < nabove_hispeed_delay; i++)
		ret += sprintf(buf + ret, "%u%s", above_hispeed_delay[i],
			       i & 0x1 ? ":" : " ");
#endif

	sprintf(buf + ret - 1, "\n");
	spin_unlock_irqrestore(&above_hispeed_delay_lock, flags);
	return ret;
}

static ssize_t store_above_hispeed_delay(
	struct kobject *kobj, struct attribute *attr, const char *buf,
	size_t count)
{
	int ntokens;
	unsigned int *new_above_hispeed_delay = NULL;
	unsigned long flags;
#ifdef CONFIG_MODE_AUTO_CHANGE
	unsigned long flags2;
#endif

	new_above_hispeed_delay = get_tokenized_data(buf, &ntokens);
	if (IS_ERR(new_above_hispeed_delay))
		return PTR_RET(new_above_hispeed_delay);
#ifdef CONFIG_MODE_AUTO_CHANGE
	spin_lock_irqsave(&mode_lock, flags2);
#endif
	spin_lock_irqsave(&above_hispeed_delay_lock, flags);
#ifdef CONFIG_MODE_AUTO_CHANGE
	if (above_hispeed_delay_set[param_index] != default_above_hispeed_delay)
		kfree(above_hispeed_delay_set[param_index]);
	above_hispeed_delay_set[param_index] = new_above_hispeed_delay;
	nabove_hispeed_delay_set[param_index] = ntokens;
	if (cur_param_index == param_index) {
		above_hispeed_delay = new_above_hispeed_delay;
		nabove_hispeed_delay = ntokens;
	}
#else
	if (above_hispeed_delay != default_above_hispeed_delay)
		kfree(above_hispeed_delay);
	above_hispeed_delay = new_above_hispeed_delay;
	nabove_hispeed_delay = ntokens;
#endif
	spin_unlock_irqrestore(&above_hispeed_delay_lock, flags);
#ifdef CONFIG_MODE_AUTO_CHANGE
	spin_unlock_irqrestore(&mode_lock, flags2);
#endif
	return count;

}

static struct global_attr above_hispeed_delay_attr =
	__ATTR(above_hispeed_delay, S_IRUGO | S_IWUSR,
		show_above_hispeed_delay, store_above_hispeed_delay);

static ssize_t show_hispeed_freq(struct kobject *kobj,
				 struct attribute *attr, char *buf)
{
#ifdef CONFIG_MODE_AUTO_CHANGE
	return sprintf(buf, "%u\n", hispeed_freq_set[param_index]);
#else
	return sprintf(buf, "%u\n", hispeed_freq);
#endif
}

static ssize_t store_hispeed_freq(struct kobject *kobj,
				  struct attribute *attr, const char *buf,
				  size_t count)
{
	int ret;
	long unsigned int val;
#ifdef CONFIG_MODE_AUTO_CHANGE
	unsigned long flags2;
#endif
	ret = strict_strtoul(buf, 0, &val);
	if (ret < 0)
		return ret;
#ifdef CONFIG_MODE_AUTO_CHANGE
	spin_lock_irqsave(&mode_lock, flags2);
	hispeed_freq_set[param_index] = val;
	if (cur_param_index == param_index)
		hispeed_freq = val;
	spin_unlock_irqrestore(&mode_lock, flags2);
#else
	hispeed_freq = val;
#endif
	return count;
}

static struct global_attr hispeed_freq_attr = __ATTR(hispeed_freq, 0644,
		show_hispeed_freq, store_hispeed_freq);

static ssize_t show_sampling_down_factor(struct kobject *kobj,
				struct attribute *attr, char *buf)
{
#ifdef CONFIG_MODE_AUTO_CHANGE
	return sprintf(buf, "%u\n", sampling_down_factor_set[param_index]);
#else
	return sprintf(buf, "%u\n", sampling_down_factor);
#endif
}

static ssize_t store_sampling_down_factor(struct kobject *kobj,
				struct attribute *attr, const char *buf,
				size_t count)
{
	int ret;
	long unsigned int val;
#ifdef CONFIG_MODE_AUTO_CHANGE
	unsigned long flags2;
#endif
	ret = strict_strtoul(buf, 0, &val);
	if (ret < 0)
		return ret;
#ifdef CONFIG_MODE_AUTO_CHANGE
	spin_lock_irqsave(&mode_lock, flags2);
	sampling_down_factor_set[param_index] = val;
	if (cur_param_index == param_index)
		sampling_down_factor = val;
	spin_unlock_irqrestore(&mode_lock, flags2);
#else
	sampling_down_factor = val;
#endif
	return count;
}

static struct global_attr sampling_down_factor_attr =
				__ATTR(sampling_down_factor, 0644,
		show_sampling_down_factor, store_sampling_down_factor);

static ssize_t show_go_hispeed_load(struct kobject *kobj,
				     struct attribute *attr, char *buf)
{
#ifdef CONFIG_MODE_AUTO_CHANGE
	return sprintf(buf, "%lu\n", go_hispeed_load_set[param_index]);
#else
	return sprintf(buf, "%lu\n", go_hispeed_load);
#endif
}

static ssize_t store_go_hispeed_load(struct kobject *kobj,
			struct attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long val;
#ifdef CONFIG_MODE_AUTO_CHANGE
	unsigned long flags2;
#endif

	ret = strict_strtoul(buf, 0, &val);
	if (ret < 0)
		return ret;
#ifdef CONFIG_MODE_AUTO_CHANGE
	spin_lock_irqsave(&mode_lock, flags2);
	go_hispeed_load_set[param_index] = val;
	if (cur_param_index == param_index)
		go_hispeed_load = val;
	spin_unlock_irqrestore(&mode_lock, flags2);
#else
	go_hispeed_load = val;
#endif
	return count;
}

static struct global_attr go_hispeed_load_attr = __ATTR(go_hispeed_load, 0644,
		show_go_hispeed_load, store_go_hispeed_load);

static ssize_t show_min_sample_time(struct kobject *kobj,
				struct attribute *attr, char *buf)
{
#ifdef CONFIG_MODE_AUTO_CHANGE
	return sprintf(buf, "%lu\n", min_sample_time_set[param_index]);
#else
	return sprintf(buf, "%lu\n", min_sample_time);
#endif
}

static ssize_t store_min_sample_time(struct kobject *kobj,
			struct attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long val;
#ifdef CONFIG_MODE_AUTO_CHANGE
	unsigned long flags2;
#endif

	ret = strict_strtoul(buf, 0, &val);
	if (ret < 0)
		return ret;
#ifdef CONFIG_MODE_AUTO_CHANGE
	spin_lock_irqsave(&mode_lock, flags2);
	min_sample_time_set[param_index] = val;
	if (cur_param_index == param_index)
		min_sample_time = val;
	spin_unlock_irqrestore(&mode_lock, flags2);
#else
	min_sample_time = val;
#endif
	return count;
}

static struct global_attr min_sample_time_attr = __ATTR(min_sample_time, 0644,
		show_min_sample_time, store_min_sample_time);

static ssize_t show_timer_rate(struct kobject *kobj,
			struct attribute *attr, char *buf)
{
#ifdef CONFIG_MODE_AUTO_CHANGE
	return sprintf(buf, "%lu\n", timer_rate_set[param_index]);
#else
	return sprintf(buf, "%lu\n", timer_rate);
#endif
}

static ssize_t store_timer_rate(struct kobject *kobj,
			struct attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long val, val_round;
#ifdef CONFIG_MODE_AUTO_CHANGE
	unsigned long flags2;
#endif
	ret = strict_strtoul(buf, 0, &val);
	if (ret < 0)
		return ret;

	val_round = jiffies_to_usecs(usecs_to_jiffies(val));
	if (val != val_round)
		pr_warn("timer_rate not aligned to jiffy. Rounded up to %lu\n",
			val_round);

#ifdef CONFIG_MODE_AUTO_CHANGE
	spin_lock_irqsave(&mode_lock, flags2);
	timer_rate_set[param_index] = val_round;
	if (cur_param_index == param_index)
		timer_rate = val_round;
	spin_unlock_irqrestore(&mode_lock, flags2);
#else
	timer_rate = val_round;
#endif
	return count;
}

static struct global_attr timer_rate_attr = __ATTR(timer_rate, 0644,
		show_timer_rate, store_timer_rate);

static ssize_t show_timer_slack(
	struct kobject *kobj, struct attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", timer_slack_val);
}

static ssize_t store_timer_slack(
	struct kobject *kobj, struct attribute *attr, const char *buf,
	size_t count)
{
	int ret;
	unsigned long val;

	ret = kstrtol(buf, 10, &val);
	if (ret < 0)
		return ret;

	timer_slack_val = val;
	return count;
}

define_one_global_rw(timer_slack);

static ssize_t show_boost(struct kobject *kobj, struct attribute *attr,
			  char *buf)
{
	return sprintf(buf, "%d\n", boost_val);
}

static ssize_t store_boost(struct kobject *kobj, struct attribute *attr,
			   const char *buf, size_t count)
{
	int ret;
	unsigned long val;

	ret = kstrtoul(buf, 0, &val);
	if (ret < 0)
		return ret;

	boost_val = val;

	if (boost_val) {
		cpufreq_interactive_boost();
	} else {
		boostpulse_endtime = ktime_to_us(ktime_get());
	}

	return count;
}

define_one_global_rw(boost);

static ssize_t store_boostpulse(struct kobject *kobj, struct attribute *attr,
				const char *buf, size_t count)
{
	int ret;
	unsigned long val;

	ret = kstrtoul(buf, 0, &val);
	if (ret < 0)
		return ret;

	boostpulse_endtime = ktime_to_us(ktime_get()) + boostpulse_duration_val;
	cpufreq_interactive_boost();
	return count;
}

static struct global_attr boostpulse =
	__ATTR(boostpulse, 0200, NULL, store_boostpulse);

static ssize_t show_boostpulse_duration(
	struct kobject *kobj, struct attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", boostpulse_duration_val);
}

static ssize_t store_boostpulse_duration(
	struct kobject *kobj, struct attribute *attr, const char *buf,
	size_t count)
{
	int ret;
	unsigned long val;

	ret = kstrtoul(buf, 0, &val);
	if (ret < 0)
		return ret;

	boostpulse_duration_val = val;
	return count;
}

define_one_global_rw(boostpulse_duration);

static ssize_t show_io_is_busy(struct kobject *kobj,
			struct attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", io_is_busy);
}

static ssize_t store_io_is_busy(struct kobject *kobj,
			struct attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long val;

	ret = kstrtoul(buf, 0, &val);
	if (ret < 0)
		return ret;
	io_is_busy = val;
	return count;
}

static struct global_attr io_is_busy_attr = __ATTR(io_is_busy, 0644,
		show_io_is_busy, store_io_is_busy);

static ssize_t show_sync_freq(struct kobject *kobj,
			struct attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", sync_freq);
}

static ssize_t store_sync_freq(struct kobject *kobj,
			struct attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long val;

	ret = kstrtoul(buf, 0, &val);
	if (ret < 0)
		return ret;
	sync_freq = val;
	return count;
}

static struct global_attr sync_freq_attr = __ATTR(sync_freq, 0644,
		show_sync_freq, store_sync_freq);

static ssize_t show_up_threshold_any_cpu_load(struct kobject *kobj,
			struct attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", up_threshold_any_cpu_load);
}

static ssize_t store_up_threshold_any_cpu_load(struct kobject *kobj,
			struct attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long val;

	ret = kstrtoul(buf, 0, &val);
	if (ret < 0)
		return ret;
	up_threshold_any_cpu_load = val;
	return count;
}

static struct global_attr up_threshold_any_cpu_load_attr =
		__ATTR(up_threshold_any_cpu_load, 0644,
		show_up_threshold_any_cpu_load,
				store_up_threshold_any_cpu_load);

static ssize_t show_up_threshold_any_cpu_freq(struct kobject *kobj,
			struct attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", up_threshold_any_cpu_freq);
}

static ssize_t store_up_threshold_any_cpu_freq(struct kobject *kobj,
			struct attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long val;

	ret = kstrtoul(buf, 0, &val);
	if (ret < 0)
		return ret;
	up_threshold_any_cpu_freq = val;
	return count;
}

static struct global_attr up_threshold_any_cpu_freq_attr =
		__ATTR(up_threshold_any_cpu_freq, 0644,
		show_up_threshold_any_cpu_freq,
				store_up_threshold_any_cpu_freq);

#ifdef CONFIG_MODE_AUTO_CHANGE
#define index(obj_name, obj_attr)					\
static ssize_t show_##obj_name(struct kobject *kobj,			\
			      struct attribute *attr, char *buf)	\
{									\
	return sprintf(buf, "%u\n", obj_name);				\
}									\
									\
static ssize_t store_##obj_name(struct kobject *kobj,			\
			       struct attribute *attr, const char *buf,	\
			       size_t count)				\
{									\
	int ret;							\
	long unsigned int val;						\
									\
	ret = strict_strtoul(buf, 0, &val);				\
	if (ret < 0)							\
		return ret;						\
									\
	val &= MULTI_MODE | SINGLE_MODE | NO_MODE;			\
	obj_name = val;							\
	return count;							\
}									\
									\
static struct global_attr obj_attr = __ATTR(obj_name, 0644,		\
		show_##obj_name, store_##obj_name);			\

index(mode, mode_attr);
index(enforced_mode, enforced_mode_attr);
index(param_index, param_index_attr);

#define load(obj_name, obj_attr)					\
static ssize_t show_##obj_name(struct kobject *kobj,			\
			      struct attribute *attr, char *buf)	\
{									\
	return sprintf(buf, "%u\n", obj_name);				\
}									\
									\
static ssize_t store_##obj_name(struct kobject *kobj,			\
			       struct attribute *attr, const char *buf,	\
			       size_t count)				\
{									\
	int ret;							\
	long unsigned int val;						\
									\
	ret = strict_strtoul(buf, 0, &val);				\
	if (ret < 0)							\
		return ret;						\
									\
	obj_name = val;							\
	return count;							\
}									\
									\
static struct global_attr obj_attr = __ATTR(obj_name, 0644,		\
		show_##obj_name, store_##obj_name);			\

load(multi_enter_load, multi_enter_load_attr);
load(multi_exit_load, multi_exit_load_attr);
load(single_enter_load, single_enter_load_attr);
load(single_exit_load, single_exit_load_attr);

#define time(obj_name, obj_attr)					\
static ssize_t show_##obj_name(struct kobject *kobj,			\
			      struct attribute *attr, char *buf)	\
{									\
	return sprintf(buf, "%lu\n", obj_name);				\
}									\
									\
static ssize_t store_##obj_name(struct kobject *kobj,			\
			       struct attribute *attr, const char *buf,	\
			       size_t count)				\
{									\
	int ret;							\
	unsigned long val;						\
									\
	ret = strict_strtoul(buf, 0, &val);				\
	if (ret < 0)							\
		return ret;						\
									\
	obj_name = val;							\
	return count;							\
}									\
									\
static struct global_attr obj_attr = __ATTR(obj_name, 0644,		\
		show_##obj_name, store_##obj_name);			\

time(multi_enter_time, multi_enter_time_attr);
time(multi_exit_time, multi_exit_time_attr);
time(single_enter_time, single_enter_time_attr);
time(single_exit_time, single_exit_time_attr);

#endif
static struct attribute *interactive_attributes[] = {
	&target_loads_attr.attr,
	&above_hispeed_delay_attr.attr,
	&hispeed_freq_attr.attr,
	&go_hispeed_load_attr.attr,
	&min_sample_time_attr.attr,
	&timer_rate_attr.attr,
	&timer_slack.attr,
	&boost.attr,
	&boostpulse.attr,
	&boostpulse_duration.attr,
	&io_is_busy_attr.attr,
	&sampling_down_factor_attr.attr,
	&sync_freq_attr.attr,
	&up_threshold_any_cpu_load_attr.attr,
	&up_threshold_any_cpu_freq_attr.attr,
#ifdef CONFIG_MODE_AUTO_CHANGE
	&mode_attr.attr,
	&enforced_mode_attr.attr,
	&param_index_attr.attr,
	&multi_enter_load_attr.attr,
	&multi_exit_load_attr.attr,
	&single_enter_load_attr.attr,
	&single_exit_load_attr.attr,
	&multi_enter_time_attr.attr,
	&multi_exit_time_attr.attr,
	&single_enter_time_attr.attr,
	&single_exit_time_attr.attr,
#endif
	NULL,
};

#if TOUCHBOOST
static void interactive_input_event(struct input_handle *handle,
		unsigned int type,
		unsigned int code, int value)
{
	if (type == EV_SYN && code == SYN_REPORT) {
		boostpulse_endtime = ktime_to_us(ktime_get()) + TOUCHBOOST_DURATION;
		cpufreq_interactive_boost();
	}
}

static int interactive_input_connect(struct input_handler *handler,
		struct input_dev *dev, const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "cpufreq";

	error = input_register_handle(handle);
	if (error)
		goto err2;

	error = input_open_device(handle);
	if (error)
		goto err1;

	return 0;
err1:
	input_unregister_handle(handle);
err2:
	kfree(handle);
	return error;
}

static void interactive_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id interactive_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
			 INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT_MASK(EV_ABS) },
		.absbit = { [BIT_WORD(ABS_MT_POSITION_X)] =
			    BIT_MASK(ABS_MT_POSITION_X) |
			    BIT_MASK(ABS_MT_POSITION_Y) },
	}, /* multi-touch touchscreen */
	{
		.flags = INPUT_DEVICE_ID_MATCH_KEYBIT |
			 INPUT_DEVICE_ID_MATCH_ABSBIT,
		.keybit = { [BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH) },
		.absbit = { [BIT_WORD(ABS_X)] =
			    BIT_MASK(ABS_X) | BIT_MASK(ABS_Y) },
	}, /* touchpad */
};

static struct input_handler interactive_input_handler = {
	.event		= interactive_input_event,
	.connect	= interactive_input_connect,
	.disconnect	= interactive_input_disconnect,
	.name		= "arteractive",
	.id_table	= interactive_ids,
};
#endif

static struct attribute_group interactive_attr_group = {
	.attrs = interactive_attributes,
	.name = "arteractive",
};

static int cpufreq_interactive_idle_notifier(struct notifier_block *nb,
					     unsigned long val,
					     void *data)
{
	switch (val) {
	case IDLE_START:
		cpufreq_interactive_idle_start();
		break;
	case IDLE_END:
		cpufreq_interactive_idle_end();
		break;
	}

	return 0;
}

static struct notifier_block cpufreq_interactive_idle_nb = {
	.notifier_call = cpufreq_interactive_idle_notifier,
};

#ifdef CONFIG_MODE_AUTO_CHANGE
static void cpufreq_param_set_init(void)
{
	unsigned int i;
	unsigned long flags;

	multi_enter_load = DEFAULT_TARGET_LOAD * num_possible_cpus();

	spin_lock_irqsave(&mode_lock, flags);
	for (i=0 ; i<MAX_PARAM_SET; i++) {
		hispeed_freq_set[i] = 0;
		go_hispeed_load_set[i] = go_hispeed_load;
		target_loads_set[i] = target_loads;
		ntarget_loads_set[i] = ntarget_loads;
		min_sample_time_set[i] = min_sample_time;
		timer_rate_set[i] = timer_rate;
		above_hispeed_delay_set[i] = above_hispeed_delay;
		nabove_hispeed_delay_set[i] = nabove_hispeed_delay;
		sampling_down_factor_set[i] = sampling_down_factor;
	}
	spin_unlock_irqrestore(&mode_lock, flags);
}
#endif

static int cpufreq_governor_arteractive(struct cpufreq_policy *policy,
		unsigned int event)
{
	int rc;
	unsigned int j;
	struct cpufreq_interactive_cpuinfo *pcpu;
	struct cpufreq_frequency_table *freq_table;
	unsigned long flags;

	switch (event) {
	case CPUFREQ_GOV_START:
		mutex_lock(&gov_lock);

		freq_table =
			cpufreq_frequency_get_table(policy->cpu);
		if (!hispeed_freq)
			hispeed_freq = policy->max;
#ifdef CONFIG_MODE_AUTO_CHANGE
		for (j=0 ; j<MAX_PARAM_SET ; j++)
			if (!hispeed_freq_set[j])
				hispeed_freq_set[j] = policy->max;
#endif
		for_each_cpu(j, policy->cpus) {
			pcpu = &per_cpu(cpuinfo, j);
			pcpu->policy = policy;
			pcpu->target_freq = policy->cur;
			pcpu->freq_table = freq_table;
			pcpu->floor_freq = pcpu->target_freq;
			pcpu->floor_validate_time =
				ktime_to_us(ktime_get());
			pcpu->hispeed_validate_time =
				pcpu->floor_validate_time;
			pcpu->max_freq = policy->max;
			down_write(&pcpu->enable_sem);
			del_timer_sync(&pcpu->cpu_timer);
			del_timer_sync(&pcpu->cpu_slack_timer);
			cpufreq_interactive_timer_start(j);
			pcpu->governor_enabled = 1;
			up_write(&pcpu->enable_sem);
		}

		/*
		 * Do not register the idle hook and create sysfs
		 * entries if we have already done so.
		 */
		if (++active_count > 1) {
			mutex_unlock(&gov_lock);
			return 0;
		}

		rc = sysfs_create_group(cpufreq_global_kobject,
				&interactive_attr_group);
		if (rc) {
			mutex_unlock(&gov_lock);
			return rc;
		}

		idle_notifier_register(&cpufreq_interactive_idle_nb);
		cpufreq_register_notifier(
			&cpufreq_notifier_block, CPUFREQ_TRANSITION_NOTIFIER);
		mutex_unlock(&gov_lock);
		break;

	case CPUFREQ_GOV_STOP:
		mutex_lock(&gov_lock);
		for_each_cpu(j, policy->cpus) {
			pcpu = &per_cpu(cpuinfo, j);
			down_write(&pcpu->enable_sem);
			pcpu->governor_enabled = 0;
			pcpu->target_freq = 0;
			del_timer_sync(&pcpu->cpu_timer);
			del_timer_sync(&pcpu->cpu_slack_timer);
			up_write(&pcpu->enable_sem);
		}

		if (--active_count > 0) {
			mutex_unlock(&gov_lock);
			return 0;
		}

		cpufreq_unregister_notifier(
			&cpufreq_notifier_block, CPUFREQ_TRANSITION_NOTIFIER);
		idle_notifier_unregister(&cpufreq_interactive_idle_nb);
		sysfs_remove_group(cpufreq_global_kobject,
				&interactive_attr_group);
		mutex_unlock(&gov_lock);

		break;

	case CPUFREQ_GOV_LIMITS:
		if (policy->max < policy->cur)
			__cpufreq_driver_target(policy,
					policy->max, CPUFREQ_RELATION_H);
		else if (policy->min > policy->cur)
			__cpufreq_driver_target(policy,
					policy->min, CPUFREQ_RELATION_L);
		for_each_cpu(j, policy->cpus) {
			pcpu = &per_cpu(cpuinfo, j);

			down_read(&pcpu->enable_sem);
			if (pcpu->governor_enabled == 0) {
				up_read(&pcpu->enable_sem);
				continue;
			}

			spin_lock_irqsave(&pcpu->target_freq_lock, flags);
			if (policy->max < pcpu->target_freq)
				pcpu->target_freq = policy->max;
			else if (policy->min > pcpu->target_freq)
				pcpu->target_freq = policy->min;

			spin_unlock_irqrestore(&pcpu->target_freq_lock, flags);
			up_read(&pcpu->enable_sem);

			/* Reschedule timer only if policy->max is raised.
			 * Delete the timers, else the timer callback may
			 * return without re-arm the timer when failed
			 * acquire the semaphore. This race may cause timer
			 * stopped unexpectedly.
			 */
			if (policy->max > pcpu->max_freq) {
				down_write(&pcpu->enable_sem);
				del_timer_sync(&pcpu->cpu_timer);
				del_timer_sync(&pcpu->cpu_slack_timer);
				cpufreq_interactive_timer_start(j);
				pcpu->minfreq_boost = 1;
				up_write(&pcpu->enable_sem);
			}

			pcpu->max_freq = policy->max;
		}
		break;
	}
	return 0;
}

static void cpufreq_interactive_nop_timer(unsigned long data)
{
}

static void arteractive_early_suspend(struct power_suspend *handler)
{
	suspended = true;
	go_hispeed_load = DEFAULT_GO_HISPEED_LOAD_SCREEN_OFF;
	return;
}

static void arteractive_late_resume(struct power_suspend *handler)
{
	suspended = false;
	go_hispeed_load = DEFAULT_GO_HISPEED_LOAD;
	return;
}

static struct power_suspend arteractive_suspend = {
	.suspend = arteractive_early_suspend,
	.resume = arteractive_late_resume,
};

static void cpufreq_interactive_timer(unsigned long data)
{
	__cpufreq_interactive_timer(data, false);
}

static int __init cpufreq_arteractive_init(void)
{
#if TOUCHBOOST
	unsigned int i, rc;
#else
	unsigned int i;
#endif
	struct cpufreq_interactive_cpuinfo *pcpu;
	struct sched_param param = { .sched_priority = MAX_RT_PRIO-1 };

	/* Initalize per-cpu timers */
	for_each_possible_cpu(i) {
		pcpu = &per_cpu(cpuinfo, i);
		init_timer_deferrable(&pcpu->cpu_timer);
		pcpu->cpu_timer.function = cpufreq_interactive_timer;
		pcpu->cpu_timer.data = i;
		init_timer(&pcpu->cpu_slack_timer);
		pcpu->cpu_slack_timer.function = cpufreq_interactive_nop_timer;
		spin_lock_init(&pcpu->load_lock);
		spin_lock_init(&pcpu->target_freq_lock);
		init_rwsem(&pcpu->enable_sem);
#if TOUCHBOOST
		if (!i)
			rc = input_register_handler(&interactive_input_handler);
#endif
	}

	register_power_suspend(&arteractive_suspend);

	spin_lock_init(&target_loads_lock);
	spin_lock_init(&speedchange_cpumask_lock);
	spin_lock_init(&above_hispeed_delay_lock);
#ifdef CONFIG_MODE_AUTO_CHANGE
	spin_lock_init(&mode_lock);
	cpufreq_param_set_init();
#endif
#ifdef CONFIG_RETENTION_CHANGE
	retention_toggle_wq = alloc_workqueue("retentionToggle_wq", WQ_HIGHPRI, 0);
	if(!retention_toggle_wq)
		pr_info("retention toggle workqueue init error\n");
	INIT_WORK(&retention_toggle_work, do_toggle_retention);
#endif
	mutex_init(&gov_lock);
	speedchange_task =
		kthread_create(cpufreq_interactive_speedchange_task, NULL,
			       "cfarteractive");
	if (IS_ERR(speedchange_task))
		return PTR_ERR(speedchange_task);

	sched_setscheduler_nocheck(speedchange_task, SCHED_FIFO, &param);
	get_task_struct(speedchange_task);

	/* NB: wake up so the thread does not look hung to the freezer */
	wake_up_process(speedchange_task);

	return cpufreq_register_governor(&cpufreq_gov_arteractive);
}

#ifdef CONFIG_RETENTION_CHANGE
static void do_toggle_retention(struct work_struct *work)
{
	if(mode_count == 1)
		msm_pm_retention_mode_enable(0);
	else if(mode_count == 0)
		msm_pm_retention_mode_enable(1);
}
#endif	// CONFIG_RETENTION_CHANGE

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_ARTERACTIVE
fs_initcall(cpufreq_arteractive_init);
#else
module_init(cpufreq_arteractive_init);
#endif

static void __exit cpufreq_interactive_exit(void)
{
#if TOUCHBOOST
	unsigned int cpu;
#endif

	cpufreq_unregister_governor(&cpufreq_gov_arteractive);
#if TOUCHBOOST
	for_each_possible_cpu(cpu) {
		if(!cpu)
			input_unregister_handler(&interactive_input_handler);
	}
#endif
	kthread_stop(speedchange_task);
	put_task_struct(speedchange_task);
}

module_exit(cpufreq_interactive_exit);

MODULE_AUTHOR("Mike Chan <mike@android.com>");
MODULE_AUTHOR("Park Ju Hyung <qkrwngud825@gmail.com>");
MODULE_DESCRIPTION("'cpufreq_arteractive' - A cpufreq governor for "
	"Latency sensitive workloads based on Samsung's Galaxy S5 Interactive");
MODULE_LICENSE("GPL");
