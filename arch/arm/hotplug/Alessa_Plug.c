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
 */

/*In the present state the driver only works with Quad core devices. It plugs down 3 cores
 *when the device suspends and brings them online only when awake leading to
 *significant power savings
 *V.01-A
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/cpu.h>
#include <linux/powersuspend.h>
#include <linux/cpufreq.h>

#define ALESSAPLUG "AlessaPlug"
#define ALESSA_VERSION 1
#define ALESSA_SUB_VERSION 1

static int suspend_cpu_num = 2;
static int resume_cpu_num = 3;
static int endurance_level = 0;
static int device_cpu = 4;


static inline void offline_cpu(void)
{
	unsigned int cpu;
	switch(endurance_level){
	case 1:
		if(suspend_cpu_num > 3)
			suspend_cpu_num = 3;
	break;
	case 2:
		if(suspend_cpu_num > 2)
			suspend_cpu_num = 2;
	break;
	default:
	break;
}
	for(cpu = 3; cpu >(suspend_cpu_num - 1); cpu--) {
		if(cpu_online(cpu))
			cpu_down(cpu);
}

	pr_info("%s: %d cpus were offline\n", ALESSAPLUG, (device_cpu - suspend_cpu_num));
}

static inline void cpu_online_all(void)
{

	unsigned int cpu;
	switch(endurance_level){
	case 1:
		if(resume_cpu_num > 2 || resume_cpu_num == 1)
		resume_cpu_num = 2;
	break;
	case 2:
		if(resume_cpu_num > 1)
			resume_cpu_num =1;
	break;
	case 0:
	if(resume_cpu_num < 3)
		resume_cpu_num =3;
	break;
	default:
	break;
	}

for(cpu = 1; cpu <= resume_cpu_num; cpu++)
{
	if(cpu_is_offline(cpu))
	cpu_up(cpu);
}

pr_info("%s: all cpu were online\n", ALESSAPLUG);
}
/*static inline void cpu_online_all(void) //All cpu's enabled
{
	unsigned int cpu;

	for (cpu = 1; cpu < 4; cpu++) {//Check the 4 cores
		if (cpu_is_offline(cpu))
			cpu_up(cpu);
	}

	pr_info("%s: all cpus were onlined\n", ALESSAPLUG);
}
OLDER CODE*/
/*
info about ssize if you don't know what is it

size_t comes from standard C,is an unsigned type used for sizes of objects.

ssize_t comes from posix, is a signed type used for a count of bytes or
an error indication
*/
static ssize_t alessa_plug_suspend_cpu(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d", suspend_cpu_num);
}


static ssize_t alessa_plug_suspend_cpu_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{//This check How many cores are active
	int val;
	sscanf(buf, "%d", &val);
	if(val < 1 || val > 4)
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
		if(endurance_level!=val) {
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

static void alessa_plug_suspend(struct power_suspend *h)
{
	offline_cpu();
	pr_info("%s: Suspend\n", ALESSAPLUG);
}

static void __ref alessa_plug_resume(struct power_suspend *h)//when you resume from screen off
{
	cpu_online_all();
	pr_info("%s: Resume\n", ALESSAPLUG);
}

static ssize_t alessa_plug_ver_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
       return sprintf(buf, "Alessa_Plug %u.%u", ALESSA_VERSION, ALESSA_SUB_VERSION);
}

static struct kobj_attribute alessa_plug_ver_attribute =
       __ATTR(version,
               0444,
               alessa_plug_ver_show, NULL);

static struct kobj_attribute alessa_plug_suspend_cpu_attribute =
       __ATTR(suspend_cpus,
               0666,
               alessa_plug_suspend_cpu, alessa_plug_suspend_cpu_store);

static struct kobj_attribute alessa_plug_endurance_attribute =
	__ATTR(endurance_level,
		0666,
		alessa_plug_endurance, alessa_plug_endurance_store);

static struct attribute *alessa_plug_attrs[] =
    {
        &alessa_plug_ver_attribute.attr,
        &alessa_plug_suspend_cpu_attribute.attr,
	&alessa_plug_endurance_attribute.attr,
        NULL,
    };

static struct attribute_group alessa_plug_attr_group =
    {
        .attrs = alessa_plug_attrs,
    };

static struct power_suspend alessa_plug_power_suspend_handler =
{
	.suspend = alessa_plug_suspend,
	.resume = alessa_plug_resume,
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

        register_power_suspend(&alessa_plug_power_suspend_handler);

        pr_info("%s: init\n", ALESSAPLUG);

        return ret;
}

/*static void __exit alessa_plug_exit(void)
{
	unregister_power_suspend(&alessa_plug_power_suspend_handler);

}*/

MODULE_LICENSE("GPL and additional rights");
MODULE_AUTHOR("Carlos "klozz" Jesus <tsubaki.ayumi@gmail.com");
MODULE_DESCRIPTION("Hotplug driver for QuadCore CPU");
late_initcall(alessa_plug_init);
//module_exit(alessa_plug_exit);
