// SPDX-License-Identifier: GPL-2.0

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "compat.h"
#include "skc.h"

int main(int argc, char **argv)
{
	struct stat stat;
	int fd, ret;
	char *buf;

	if (argc != 2)
		return -1;

	fd = open(argv[1], O_RDONLY);
	if (fd < 0)
		return fd;
	ret = fstat(fd, &stat);
	if (ret < 0)
		return ret;

	buf = malloc(stat.st_size + 1);
	if (!buf)
		return -ENOMEM;

	ret = read(fd, buf, stat.st_size);
	if (ret < 0)
		return ret;
	buf[ret] = '\0';

	close(fd);

	ret = skc_parse(buf, ret);

	printf("parsed : %d\n", ret);

	skc_dump();
	printf("\n=========================\n\n");
	skc_show_tree();
	printf("\n=========================\n\n");
	skc_show_kvlist();

	return 0;
}

