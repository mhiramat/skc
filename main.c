// SPDX-License-Identifier: GPL-2.0

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <linux/kernel.h>
#include <linux/skc.h>

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
	printf("Usage: skc [-q KEY|-t|-d|-l] skc-file \n");
	return -1;
}

int main(int argc, char **argv)
{
	char *path = NULL;
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

	path = argv[optind];
	ret = load_skc_file(path, &buf);
	if (ret < 0) {
		printf("Failed to load %s : %d\n", path, ret);
		return ret;
	}

	ret = skc_init(buf);
	if (ret < 0) {
		printf("Failed to parse %s: %d\n", path, ret);
		return ret;
	}

	/* Key - Value query example */
	if (query_key) {
		struct skc_node *vnode;
		const char *val = skc_find_value(query_key, &vnode);

		if (!val) {
			printf("No value for \"%s\" key\n", query_key);
			return -ENOENT;
		}
		printf("%s = ", query_key);
		if (skc_node_is_array(vnode)) {
			skc_array_for_each_value(vnode, val)
				printf("\"%s\"%s", skc_node_get_data(vnode),
					vnode->next ? ", " : "\n");
		} else
			printf("\"%s\"\n", val);
		return 0;
	}

	/* Dumping SKC examples */
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

