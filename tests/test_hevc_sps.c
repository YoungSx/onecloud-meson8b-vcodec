// SPDX-License-Identifier: GPL-2.0+
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
#include "../vdec/hevc-sps.h"

static void check_sps(const char *path, u32 width, u32 height, u32 visible_height,
		      u32 dpb)
{
	FILE *file;
	u8 *data;
	long size;
	struct hevc_sps_geometry g;
	unsigned int i, end, found = 0;

	file = fopen(path, "rb"); assert(file);
	fseek(file, 0, SEEK_END); size = ftell(file); rewind(file);
	data = malloc(size); assert(data);
	assert(fread(data, 1, size, file) == (size_t)size); fclose(file);
	for (i = 2; i + 2 < (unsigned long)size; i++) {
		if (data[i - 2] || data[i - 1] || data[i] != 1 ||
		    ((data[i + 1] >> 1) & 63) != 33) continue;
		for (end = i + 4; end + 2 < (unsigned long)size; end++)
			if (!data[end] && !data[end + 1] && data[end + 2] == 1) break;
		if (end + 2 >= (unsigned long)size) end = size;
		assert(hevc_parse_sps_geometry(data + i + 1, end - i - 1, &g) == 0);
		printf("coded %u x %u visible %u x %u dpb %u reorder %u\n",
		       g.coded_width,g.coded_height,g.width,g.height,g.dpb,g.reorder);
		assert(g.coded_width==width && g.coded_height==height);
		assert(g.width==width && g.height==visible_height && g.dpb==dpb);
		assert(g.dpb>=g.reorder+1);
		for (unsigned int length=0; length<20; length++)
			assert(hevc_parse_sps_geometry(data+i+1,length,&g)!=0);
		found++;
	}
	assert(found==1);
	free(data);
}

int main(int argc, char **argv)
{
	struct hevc_sps_geometry g;

	assert(argc == 3);
	check_sps(argv[1],1920,1088,1080,5);
	check_sps(argv[2],320,192,192,16);
	u8 zeros[512]={0};
	assert(hevc_parse_sps_geometry(zeros,sizeof(zeros),&g)!=0);
	zeros[0]=66;zeros[1]=1;
	assert(hevc_parse_sps_geometry(zeros,sizeof(zeros),&g)!=0);
	return 0;
}
