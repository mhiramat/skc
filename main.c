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
	printf("Usage: skc [-w NUM][-q KEY|-t|-d|-l] skc-file \n");
	return -1;
}

int main(int argc, char **argv)
{
	char *path = NULL;
	char *query_key = NULL;
	char *buf;
	int ret, opt, mode = 'l';
	int words = -1;

	while ((opt = getopt(argc, argv, "w:q:tdl")) != -1) {
		switch (opt) {
		case 'w':
			words = atoi(optarg);
			break;
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

	ret = skc_parse(buf, ret);
	if (ret < 0) {
		printf("Failed to parse %s: %d\n", path, ret);
		return ret;
	}

	/* Key - Value query example */
	if (query_key && words < 0) {
		struct skc_node *vnode;
		const char *val = skc_get_value(query_key, &vnode);

		if (!val) {
			printf("No value for \"%s\" key\n", query_key);
			return -ENOENT;
		}
		printf("%s = ", query_key);
		if (!vnode || !vnode->next)
			printf("\"%s\"\n", val);
		else {
			/* Array node */
			while (vnode->next) {
				printf("\"%s\", ", skc_node_get_data(vnode));
				vnode = skc_node_get_next(vnode);
			}
			printf("\"%s\"\n", skc_node_get_data(vnode));
		}
		return 0;
	}
	/* Iterator example */
	if (query_key && words >= 0) {
		char buf[SKC_KEYLEN_MAX];
		const char *val;
		struct skc_iter iter;

		skc_for_each_value(&iter, query_key, val) {
			if (skc_iter_unmatched_words(&iter, words, buf, SKC_KEYLEN_MAX) < 0)
				printf("Error: No matched words?\n");
			else
				printf("\"%s\"\n", buf);
		}
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

