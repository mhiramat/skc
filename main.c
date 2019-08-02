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

int usage(void)
{
	printf("Usage: skc [-q query_key|-t|-d|-l] skc-file \n");
	return -1;
}

int main(int argc, char **argv)
{
	char *path;
	char *query_key = NULL;
	char *buf;
	int ret, opt, mode = 'l';

	while ((opt = getopt(argc, argv, "q:tdl")) != -1) {
		switch (opt) {
		case 'q':
			query_key = strdup(optarg);
			break;
		/* Output mode */
		case 't':
		case 'd':
		case 'l':
			mode = opt;
			break;
		default:
			return usage();
		}
	}

	if (optind >= argc) {
		printf("Error: No .skc file is specified after options.\n");
		return -2;
	}

	ret = load_skc_file(argv[optind], &buf);
	if (ret < 0) {
		printf("Failed to load %s : %d\n", argv[1], ret);
		return ret;
	}

	ret = skc_parse(buf, ret);
	if (ret < 0) {
		printf("Failed to parse %s: %d\n", path, ret);
		return ret;
	}

	if (query_key) {
		const char *val = skc_get_value(query_key);
		if (!val)
			printf("No value for \"%s\" key\n", query_key);
		else
			printf("%s = \"%s\"\n", query_key, val);
		return 0;
	}

	switch (mode) {
	case 't':
		skc_show_tree();
		break;
	case 'd':
		skc_dump();
		break;
	case 'l':
	default:
		skc_show_kvlist();
		break;
	}

	return 0;
}

