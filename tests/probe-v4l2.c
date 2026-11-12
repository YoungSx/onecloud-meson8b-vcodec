/* SPDX-License-Identifier: GPL-2.0 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <linux/videodev2.h>

int main(int argc, char **argv)
{
	struct v4l2_capability cap = {0};
	struct v4l2_fmtdesc fmt;
	unsigned int types[] = {V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE};
	int fd;
	unsigned int i, index;

	if (argc != 2)
		return 2;
	fd = open(argv[1], O_RDWR | O_NONBLOCK);
	if (fd < 0 || ioctl(fd, VIDIOC_QUERYCAP, &cap)) {
		perror("QUERYCAP");
		return 1;
	}
	printf("driver=%s card=%s caps=%08x\n", cap.driver, cap.card, cap.device_caps);
	for (i = 0; i < 2; i++) {
		for (index = 0;; index++) {
			memset(&fmt, 0, sizeof(fmt));
			fmt.type = types[i];
			fmt.index = index;
			if (ioctl(fd, VIDIOC_ENUM_FMT, &fmt)) {
				if (errno != EINVAL)
					return 1;
				break;
			}
			printf("type=%u format=%c%c%c%c description=%s flags=%x\n", fmt.type,
			       fmt.pixelformat & 255, (fmt.pixelformat >> 8) & 255,
			       (fmt.pixelformat >> 16) & 255, fmt.pixelformat >> 24,
			       fmt.description, fmt.flags);
		}
	}
	return close(fd) ? 1 : 0;
}
