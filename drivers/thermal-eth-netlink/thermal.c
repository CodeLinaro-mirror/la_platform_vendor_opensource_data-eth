//SPDX-License-Identifier: GPL-2.0-only
//Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.


#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "thermal_netlink.h"

static void thermal_cdev_state_change(int cdev_id, int cus_state, void *data)
{
	printf("%s: CDEV ID:%d cur_state:%d\n", __func__, cdev_id, cus_state);
}

int main(int argc, char **argv)
{
	printf("Thermal cdev test app started\n");

	if (thermal_nl_init())
		return 0;
	thermal_nl_register_trip(thermal_cdev_state_change, NULL);
	while (1)
		pause();

	printf("Thermal cdev nl_stop\n");
	thermal_nl_stop();

	printf("Thermal cdev test app exited\n");

	return 0;
}
