// SPDX-License-Identifier: GPL-2.0
/* Query one V4L2 control on a device and try to set it. Usage:
 * ctrl-probe DEVICE CTRL_ID_HEX [VALUE]
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/videodev2.h>

int main(int argc, char **argv)
{
	struct v4l2_control ctrl = {0};
	int fd;

	if (argc == 2 && !strcmp(argv[1], "--list")) {
		fprintf(stderr, "--list DEVICE needed\n");
		return 2;
	}
	if (argc == 3 && !strcmp(argv[1], "--list")) {
		struct v4l2_queryctrl qc = { .id = V4L2_CTRL_CLASS_CODEC | 1 };

		fd = open(argv[2], O_RDWR);
		if (fd < 0) {
			perror("open");
			return 1;
		}
		for (;;) {
			if (ioctl(fd, VIDIOC_QUERYCTRL, &qc) < 0)
				break;
			if (!(qc.flags & V4L2_CTRL_FLAG_DISABLED))
				printf("0x%08x %s [%d..%d] type=%u\n",
				       qc.id, qc.name, qc.minimum, qc.maximum,
				       qc.type);
			qc.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
		}
		return 0;
	}

	if (argc < 3) {
		fprintf(stderr, "usage: %s DEVICE CTRL_ID_HEX [VALUE]\n"
			"       %s --list DEVICE\n", argv[0], argv[0]);
		return 2;
	}
	fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		perror("open");
		return 1;
	}
	ctrl.id = strtoul(argv[2], NULL, 0);
	if (argc > 3) {
		struct v4l2_ext_controls ecs = {0};
		struct v4l2_ext_control ec = {0};

		ec.id = ctrl.id;
		ec.value = atoi(argv[3]);
		ecs.ctrl_class = V4L2_CTRL_CLASS_CODEC;
		ecs.count = 1;
		ecs.controls = &ec;
		if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &ecs) < 0) {
			fprintf(stderr, "S_EXT_CTRLS(0x%x): %s\n", ctrl.id,
				strerror(errno));
			return 1;
		}
		printf("S_EXT_CTRLS(0x%x) = %d OK\n", ctrl.id, ec.value);
	} else {
		if (ioctl(fd, VIDIOC_G_CTRL, &ctrl) < 0) {
			fprintf(stderr, "G_CTRL(0x%x): %s\n", ctrl.id,
				strerror(errno));
			return 1;
		}
		printf("G_CTRL(0x%x) = %d\n", ctrl.id, ctrl.value);
	}
	return 0;
}
