// SPDX-License-Identifier: GPL-2.0

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "compat.h"
#include "skc.h"

/* Return the read size or -errno */
int load_skc_file(const char *path, char **buf)
{
	struct stat stat;
	int fd, ret;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -errno;
	ret = fstat(fd, &stat);
	if (ret < 0)
		return -errno;

	*buf = malloc(stat.st_size + 1);
	if (!*buf)
		return -ENOMEM;

	ret = read(fd, *buf, stat.st_size);
	if (ret < 0)
		return -errno;
	(*buf)[ret] = '\0';

	close(fd);

	return ret;
}

int main(int argc, char **argv)
{
	char *buf;
	int ret;

	if (argc != 2)
		return -1;

	ret = load_skc_file(argv[1], &buf);
	if (ret < 0) {
		printf("Failed to load %s : %d\n", argv[1], ret);
		return ret;
	}

	ret = skc_parse(buf, ret);

	printf("parsed : %d\n", ret);

	skc_dump();
	printf("\n=========================\n\n");
	skc_show_tree();
	printf("\n=========================\n\n");
	skc_show_kvlist();

	return 0;
}

