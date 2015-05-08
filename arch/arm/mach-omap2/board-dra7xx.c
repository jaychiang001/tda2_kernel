/*
 * Copyright (C) 2015 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Venkateswara Rao Mandela <venkat.mandela@ti.com>
 *
 * Modified from the original mach-omap/omap2/board-generic.c did by Paul
 * to support the OMAP2+ device tree boards with an unique board file.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/of.h>
#include <asm/byteorder.h>
#include "board-dra7xx.h"


static void kernel_set_boottime_vals(struct device_node *chosen,
			      const char *propname,
			      u32 boot_time){
	struct property *prop;

	prop = of_find_property(chosen, propname, NULL);
	if (prop) {
		u32 *intPtr;

		intPtr = (u32 *)prop->value;
		*intPtr = cpu_to_be32(boot_time);
	} else {
		pr_err("%s not found\n", propname);
	}
}

void kernel_update_dt_with_boottimes(void)
{
	struct device_node *bus;
	u32 user_entry = read_fast_counter();

	bus = of_find_node_by_name(NULL, "chosen");
	if (!bus) {
		pr_err("Could not find chosen node");
		return;
	}

	pr_err("Kernel Entry time %d ticks\n", start_time);
	kernel_set_boottime_vals(bus, "k-start-time", start_time);
	kernel_set_boottime_vals(bus, "k-hwmod-dur", omaphwmod_dur);
	kernel_set_boottime_vals(bus, "k-mm-init-dur", mm_init_dur);
	kernel_set_boottime_vals(bus, "k-rest-init-time", rest_init_time);
	kernel_set_boottime_vals(bus, "k-cust-machine-dur", cust_machine_dur);
	kernel_set_boottime_vals(bus, "k-user-space-entry-time", user_entry);
	kernel_set_boottime_vals(bus, "k-init-call-dur", init_call_time);
	kernel_set_boottime_vals(bus, "k-root-wait-dur", root_wait_time);
}
