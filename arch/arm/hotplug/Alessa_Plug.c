/*
 * Copyright (c) 2015, klozz Jesus  @TeamMEX-XDA-Developers <xxx.reptar.rawrr....>
 *  Alessa_plug
 *  Dedicated to Stephanny Marlene [Stephanny Cooper]
 * 	         For the great momments!
 * 		    ALESSA PLUG...
 * 	             27/04/2011
 *
 * 	GLPv3 Licensed
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 3, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * A simple hotplugging driver optimized for Quad Core CPUs
 * you need to add my Copyright if you use this code. and my name on it.
 */

/*In the present state the driver only works with Quad core devices. It plugs down 3 cores
 *when the device suspends and brings them online only when awake leading to
 *significant power savings
 *V.01-A
 *
 *v1.1 transform your device to dual-core or sigle core to make more powersave
 * v1.2.1 Sampling rate tunable-
 * v1.3 Fix some endurance mode
 * Threshold are now tunable
 * v1.3.1 Added switch to enable or disable hotplug
 * v1.4.0 Added WQ(Workqueue) for resume on LCD_notify
 * and introducing touch boost
 * v1.4.1 Fix some logic
 * v1.4.2 Add some cpu idle info required if aren't present on cpufreq.c
 * v1.4.4 fix some logic
 * v1.4.5 code cleanup
 * v2.0.0 Improve hotplug algorithm
 * v2.1.0 Now compatible with 2 cores to 8 cores.
 */
#include <asm/cputime.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/lcd_notify.h>
#include <linux/cpufreq.h>
#include <linux/tick.h>

#define ALESSAPLUG "AlessaPlug"
#define ALESSA_VERSION 2
#define ALESSA_SUB_VERSION 1
#define ALESSA_MAINTENANCE 0

//disable messages
#define DEBUGMODE 0

#define CPU_LOAD_THRESHOLD    (65)
#define MIN_CPU_LOAD_THRESHOLD (10)
#define DEF_SAMPLING_MS (500)
#define MIN_SAMPLING_MS (50)
//Define the min time of the cpu are up
#define CPU_MIN_TIME_UP (750)
#define TOUCH_BOOST_ENABLED (0)

static bool isSuspended = false;
static int suspend_cpu_num = 2, resume_cpu_num = (NR_CPUS -1);
static int endurance_level = 0;
static int core_limit = NR_CPUS;
struct notifier_block lcd_worker;
static int sampling_time = DEF_SAMPLING_MS;
static int load_threshold = CPU_LOAD_THRESHOLD;

static int alessa_HP_enabled = 1;//To enable or disable hotplug
static int touch_boost_enabled = TOUCH_BOOST_ENABLED; //To enable Touch boost

//Resume
static struct workqueue_struct *Alessa_plug_resume_wq;
static struct delayed_work Alessa_plug_resume_work;
//work
static struct workqueue_struct *Alessa_plug_wq;
static struct delayed_work Alessa_plug_work;
//Touch boost
static struct workqueue_struct *Alessa_plug_boost_wq;
static struct delayed_work Alessa_plug_touch_boost;
//CPU CHARGE
static unsigned int last_load[8] ={0};
static int now[8], last_time[8];

struct cpu_load_data{
	u64 prev_cpu_idle;
	u64 prev_cpu_wall;
	unsigned int average_load_maxfreq;
	unsigned int cur_load_maxfreq;
	unsigned int samples;
	unsigned int windows_size;
	cpumask_var_t related_cpu;
};

static DEFINE_PER_CPU(struct cpu_load_data, cpuload);

// add endurance levels for octa quad and dual core
// 2 for octa and quad 1 for dual
static inline void offline_cpu(void)
{
	unsigned int cpu;
	switch(endurance_level){
	case 1:
		if(suspend_cpu_num >= NR_CPUS / 2)
			suspend_cpu_num = NR_CPUS / 2;
	break;
	case 2:
		if(NR_CPUS >= 4 && suspend_cpu_num > NR_CPUS / 4)
			suspend_cpu_num = NR_CPUS / 4;
	break;
	default:
	break;
}
	for(cpu = NR_CPUS - 1; cpu >(suspend_cpu_num - 1); cpu--) {
		if(cpu_online(cpu))
			cpu_down(cpu);
}

	pr_info("%s: %d cpus were offline\n", ALESSAPLUG, (NR_CPUS - suspend_cpu_num));
}

static inline void cpu_online_all(void)
{

	unsigned int cpu;
	switch(endurance_level){
		case 0:
		resume_cpu_num =(NR_CPUS - 1);
		break;
	case 1:
		if(resume_cpu_num > (NR_CPUS / 2) - 1 || resume_cpu_num == 1)
		resume_cpu_num = ((NR_CPUS / 2) - 1);
	break;
	case 2:
		if(NR_CPUS >= 4 && resume_cpu_num > ((NR_CPUS / 4) -1))
			resume_cpu_num = ((NR_CPUS / 4) -1);
	break;
	default:
	break;
	}

	if(DEBUGMODE)
			pr_info("%s: resume_cpu_num = %d\n", ALESSAPLUG, resume_cpu_num);
for(cpu = 1; cpu <= resume_cpu_num; cpu++)
{
	if(cpu_is_offline(cpu))
	cpu_up(cpu);
}

pr_info("%s: all cpu were online\n", ALESSAPLUG);
}

//Code related to add touch boost support
static void __ref alessa_plug_boost_work_fn(struct work_struct *work)
{
	int cpu;
	for(cpu = 1; cpu < NR_CPUS; cpu++){
		if(cpu_is_offline(cpu))
			cpu_up(cpu);
	}
}

static void alessa_plug_input_event(struct input_handle *handle, unsigned int type, unsigned int code, int value)
{
	if ((type == EV_KEY) && (code == BTN_TOUCH) && (value == 1) && touch_boost_enabled == 1 && alessa_HP_enabled == 1)
	{
	pr_info("%s: touch boost\n", ALESSAPLUG);
		queue_delayed_work_on(0, Alessa_plug_boost_wq, &Alessa_plug_touch_boost, msecs_to_jiffies(0));

	}
}

static int alessa_plug_input_connect(struct input_handler *handler,
			struct input_dev *dev, const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;
/*
* kzalloc — allocate memory. The memory is set to zero.
* used by slab cache
*
* Kmalloc info
* http://www.makelinux.net/books/lkd2/ch11lev1sec4
*GFP_KERNEL
*
* This is a normal allocation and might block. This is the flag to use  * in process context code when it is safe to sleep. The kernel will do  * whatever it has to in order to obtain the memory requested by the     * caller. This flag should be your first choice.
*
*/
	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if(!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "cpufreq";

	error = input_register_handle(handle);
	if(error)
		goto error2;

	error = input_open_device(handle);
	if(error)
		goto error1;

	return 0;

error1:
	input_unregister_handle(handle);
error2:
	kfree(handle);
	return error;
}

static void alessa_plug_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);//kfree — free previously allocated memory
}

static const struct input_device_id alessa_plug_ids[] = {
	{.driver_info = 1},
	{},
};

static struct input_handler alessa_plug_input_handler = {
	.event      = alessa_plug_input_event,
	.connect    = alessa_plug_input_connect,
	.disconnect = alessa_plug_input_disconnect,
	.name       = "alessa_plug_handler",
	.id_table   = alessa_plug_ids,
};

//Finish part of the touch boost code
static ssize_t alessa_plug_suspend_cpu(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d", suspend_cpu_num);
}


static ssize_t alessa_plug_suspend_cpu_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{//This check How many cores are active
	int val;
	sscanf(buf, "%d", &val);
	if(val < 1 || val > NR_CPUS)
		pr_info("%s: suspend cpus off-limits\n", ALESSAPLUG);
	else
		suspend_cpu_num = val;

	return count;
}

static ssize_t alessa_plug_endurance(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d", endurance_level);
}

static ssize_t __ref alessa_plug_endurance_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int val;
	sscanf(buf, "%d", &val);
	switch(val) {
	case 0:
	case 1:
	case 2:
		if(endurance_level!=val && !(endurance_level > 1 && NR_CPUS < 4)) {
		endurance_level = val;
		offline_cpu();
		cpu_online_all();
	}
	break;
	default:
		pr_info("%s: invalid endurance level\n", ALESSAPLUG);
	break;
	}
	return count;
}

static ssize_t alessa_plug_sampling(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d", sampling_time);
}

static ssize_t __ref alessa_plug_sampling_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int val;
	sscanf(buf, "%d", &val);
	if(val > MIN_SAMPLING_MS)
		sampling_time = val;
	return count;
}

static ssize_t alessa_plug_hp_enabled_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d", alessa_HP_enabled);
}

static ssize_t __ref alessa_plug_hp_enabled_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{

	int val;
	int last_val = alessa_HP_enabled;
	sscanf(buf, "%d", &val);
		switch(val)
	{
	case 0:
	case 1:
		alessa_HP_enabled = val;
	break;
	default:
		pr_info("%s: invalid choice\n", ALESSAPLUG);
	break;

	}

	if(alessa_HP_enabled == 1 && alessa_HP_enabled != last_val)
		queue_delayed_work_on(0, Alessa_plug_wq, &Alessa_plug_work, msecs_to_jiffies(sampling_time));
	return count;
}
//start touch boost reelated
static ssize_t alessa_plug_tb_enabled(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d", touch_boost_enabled);
}

static ssize_t __ref alessa_plug_tb_enabled_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{

	int val;
	sscanf(buf, "%d", &val);
	switch(val)
	{
	case 0:
	case 1:
		touch_boost_enabled = val;
	break;
	default:
		pr_info("%s : invalid choice\n", ALESSAPLUG);
	break;
	}
	return count;
}
//finish touch boost related
static ssize_t alessa_plug_load(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d", load_threshold);
}

static ssize_t __ref alessa_plug_load_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int val;
	sscanf(buf, "%d", &val);
	if(val > 10)
		load_threshold = val;
	return count;
}

static unsigned int get_curr_load(unsigned int cpu)//Current cpu load
{
	int ret;
	unsigned int idle_time, wall_time;
	unsigned int cur_load;
	u64 cur_wall_time, cur_idle_time;
	struct cpu_load_data *pcpu = &per_cpu(cpuload, cpu);
	struct cpufreq_policy policy;

	ret = cpufreq_get_policy(&policy, cpu);
	if(ret)
		return -EINVAL;

	cur_idle_time = get_cpu_idle_time(cpu, &cur_wall_time, 0);

	wall_time = (unsigned int) (cur_wall_time - pcpu->prev_cpu_wall);
	pcpu->prev_cpu_wall = cur_wall_time;

	idle_time = (unsigned int) (cur_idle_time - pcpu->prev_cpu_idle);

	pcpu->prev_cpu_idle = cur_idle_time;

	if(unlikely(!wall_time || wall_time < idle_time))
		return 0;

	cur_load = 100 * (wall_time - idle_time) / wall_time;
		return cur_load;
}

static void alessa_plug_suspend(void)
{
	offline_cpu();
	pr_info("%s: suspend\n", ALESSAPLUG);

}

static void __ref alessa_plug_resume(void)
{
	cpu_online_all();
	pr_info("%s: resume\n", ALESSAPLUG);
}

static void __cpuinit alessa_plug_resume_work_fn(struct work_struct *work)
{
	alessa_plug_resume();
}

static void __cpuinit alessa_plug_work_fn(struct work_struct *work)
{
	int i;
	unsigned int load[8], average_load[8];

	switch(endurance_level)
{
	case 0:
		core_limit = NR_CPUS;
	break;
	case 1:
		core_limit = NR_CPUS / 2;
	break;
	case 2:
		core_limit = NR_CPUS / 4;
	break;
	default:
		core_limit = NR_CPUS;
	break;
	}
	for(i=0; i < core_limit; i++){
		if(cpu_online(i))
			load[i] = get_curr_load(i);
		else
			load[i] = 0;

	average_load[i] = ((int) load[i] + (int) last_load[i]) / 2;
	last_load[i] = load[i];
}
	for(i=0;i<core_limit;i++)
{
	if(cpu_online(i) && average_load[i] > load_threshold && cpu_is_offline(i+1))
	{
		if(DEBUGMODE)
	pr_info("%s: Bringing back cpu %d\n", ALESSAPLUG, i);
		if(!((i+1) > NR_CPUS)){
					last_time[i+1] = ktime_to_ms(ktime_get());
		cpu_up(i+1);

		}
}
else if(cpu_online(i) && average_load[i] < load_threshold && cpu_online (i+1))
{
		if(DEBUGMODE)
		pr_info("%s: offlining cpu %d\n", ALESSAPLUG, i);
			if(!(i+1) == 0){
		 			now[i+1] = ktime_to_ms(ktime_get());
					if((now[i+1]-last_time[i+1]) > CPU_MIN_TIME_UP)
						cpu_down(i+1);
				}
		}
}
	if(alessa_HP_enabled != 0 && !isSuspended)
	queue_delayed_work_on(0,Alessa_plug_wq, &Alessa_plug_work,
				msecs_to_jiffies(sampling_time));

else {
	if(!isSuspended)
		cpu_online_all();
	else
		alessa_plug_suspend();
	}

}

static int lcd_notifier_callback(struct notifier_block *nb,
                                 unsigned long event, void *data)//when you resume from screen off
{

	switch(event)
	{
		case LCD_EVENT_ON_START:
			isSuspended = false;
			if(alessa_HP_enabled)
				queue_delayed_work_on(0, Alessa_plug_wq, &Alessa_plug_work,
			msecs_to_jiffies(sampling_time));
			else
				queue_delayed_work_on(0, Alessa_plug_resume_wq, &Alessa_plug_resume_work,
			msecs_to_jiffies(10));

		pr_info("%s: resume called\n", ALESSAPLUG);
		break;
		case LCD_EVENT_ON_END:
		break;
		case LCD_EVENT_OFF_START:
			isSuspended = true;
				pr_info("%s: suspend called\n", ALESSAPLUG);
		break;
		default:
		break;
	}
		return 0;


}

static ssize_t alessa_plug_ver_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
//       return sprintf(buf, "Alessa_Plug %u.%u.%u", ALESSA_VERSION, ALESSA_SUB_VERSION, ALESSA_MAINTENANCE);
       return sprintf(buf, "%u.%u.%u", ALESSA_VERSION, ALESSA_SUB_VERSION, ALESSA_MAINTENANCE);
}

static struct kobj_attribute alessa_plug_ver_attribute =
       __ATTR(version,
               0444,
               alessa_plug_ver_show, NULL);

static struct kobj_attribute alessa_plug_suspend_cpu_attribute =
       __ATTR(suspend_cpus,
               0660,
               alessa_plug_suspend_cpu, alessa_plug_suspend_cpu_store);

static struct kobj_attribute alessa_plug_endurance_attribute =
	__ATTR(endurance_level,
		0660,
		alessa_plug_endurance, alessa_plug_endurance_store);

static struct kobj_attribute alessa_plug_sampling_attribute =
	__ATTR(sampling_rate,
		0660,
		alessa_plug_sampling, alessa_plug_sampling_store);

static struct kobj_attribute alessa_plug_load_attribute =
	__ATTR(load_threshold,
		0660,
		alessa_plug_load, alessa_plug_load_store);

static struct kobj_attribute alessa_plug_hp_enabled_attribute =
	__ATTR(hotplug_enabled,
		0660,
		alessa_plug_hp_enabled_show, alessa_plug_hp_enabled_store);

static struct kobj_attribute alessa_plug_tb_enabled_attribute =
	__ATTR(touch_boost,
		0660,
		alessa_plug_tb_enabled, alessa_plug_tb_enabled_store);

static struct attribute *alessa_plug_attrs[] =
    {
        &alessa_plug_ver_attribute.attr,
        &alessa_plug_suspend_cpu_attribute.attr,
	&alessa_plug_endurance_attribute.attr,
	&alessa_plug_sampling_attribute.attr,
	&alessa_plug_load_attribute.attr,
	&alessa_plug_hp_enabled_attribute.attr,
	&alessa_plug_tb_enabled_attribute.attr,
        NULL,
    };

static struct attribute_group alessa_plug_attr_group =
    {
        .attrs = alessa_plug_attrs,
    };


static struct kobject *alessa_plug_kobj;

static int __init alessa_plug_init(void)
{

	int ret = 0;
	int sysfs_result;
	printk(KERN_DEBUG "[%s]\n", __func__);

	alessa_plug_kobj = kobject_create_and_add("alessa_plug", kernel_kobj);

	if (!alessa_plug_kobj){
		pr_err("%s Interface Create Failed!\n", __FUNCTION__);
	return -ENOMEM;
	}

	sysfs_result = sysfs_create_group(alessa_plug_kobj, &alessa_plug_attr_group);

        if (sysfs_result) {
                pr_info("%s sysfs create failed!\n", __FUNCTION__);
                kobject_put(alessa_plug_kobj);
        }

	lcd_worker.notifier_call = lcd_notifier_callback;

	lcd_register_client(&lcd_worker);

	//TOUCHBOOST
	pr_info("%s : registerin input boost", ALESSAPLUG);
	ret = input_register_handler(&alessa_plug_input_handler);
		if(ret){
	pr_err("%s: Failed to register input handler: %d\n", ALESSAPLUG, ret);
	}
	//TOUCH SDKSDALKDÑASLKDAKDAÑSLDKAÑDKL

	Alessa_plug_wq = alloc_workqueue("AlessaPlug",
				WQ_HIGHPRI | WQ_UNBOUND, 1);

	Alessa_plug_resume_wq = alloc_workqueue("Alessa_plug_resume",
				WQ_HIGHPRI | WQ_UNBOUND, 1);

	Alessa_plug_boost_wq = alloc_workqueue("Alessa_plug_touch_boost",
			WQ_HIGHPRI | WQ_UNBOUND, 1);

	INIT_DELAYED_WORK(&Alessa_plug_work, alessa_plug_work_fn);
	INIT_DELAYED_WORK(&Alessa_plug_resume_work, alessa_plug_resume_work_fn);
	INIT_DELAYED_WORK(&Alessa_plug_touch_boost, alessa_plug_boost_work_fn);

	queue_delayed_work_on(0,Alessa_plug_wq, &Alessa_plug_work,
				msecs_to_jiffies(10));

        pr_info("%s: init\n", ALESSAPLUG);

        return ret;
}

MODULE_LICENSE("GPL and additional rights");
MODULE_AUTHOR("Carlos "klozz" Jesus <klozz707@gmail.com");
MODULE_DESCRIPTION("Hotplug driver for MSM SoC's");
late_initcall(alessa_plug_init);
