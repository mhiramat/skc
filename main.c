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

static int skc_show_array(struct skc_node *node)
{
	const char *val;
	int i = 0;

	skc_array_for_each_value(node, val) {
		printk("\"%s\"%s", val, node->next ? ", " : ";\n");
		i++;
	}
	return i;
}

static void skc_show_tree(void)
{
	struct skc_node *node, *cnode;
	int depth = 0, i;

	node = skc_root_node();
	while (node && skc_node_is_key(node)) {
		for (i = 0; i < depth; i++)
			printk("\t");
		cnode = skc_node_get_child(node);
		if (cnode && skc_node_is_key(cnode)) {
			printk("%s {\n", skc_node_get_data(node));
			node = cnode;
			depth++;
			continue;
		} else if (cnode && skc_node_is_value(cnode)) {
			printk("%s = ", skc_node_get_data(node));
			if (cnode->next)
				skc_show_array(cnode);
			else
				printk("\"%s\";\n", skc_node_get_data(cnode));
		} else {
			printk("%s;\n", skc_node_get_data(node));
		}

		if (node->next) {
			node = skc_node_get_next(node);
			continue;
		}
		while (!node->next) {
			node = skc_node_get_parent(node);
			if (!node)
				return ;
			depth--;
			for (i = 0; i < depth; i++)
				printk("\t");
			printk("}\n");
		}
		node = skc_node_get_next(node);
	}
}

#define PAGE_SIZE	4096

/* Read the skc from stdin */
int read_skc_input(char **buf)
{
	int size = PAGE_SIZE;
	int ret, total = 0;

	*buf = malloc(size);

	while (!feof(stdin) && !ferror(stdin)) {
		ret = fread(*buf + total, 1, size - total, stdin);
		total += ret;
		if (size - total == 0) {
			size += PAGE_SIZE;
			*buf = realloc(*buf, size);
		}
	}

	if (ferror(stdin)) {
		ret = -errno;
		goto error;
	} else if (total == 0) {
		ret = -EINVAL;
		goto error;
	}

	(*buf)[total] = '\0';

	return total;
error:
	free(*buf);
	*buf = NULL;
	return ret;
}

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
	printf("Usage: skc [-q KEY|-p PREFIX|-t|-d] [skc-file] \n");
	return -1;
}

int main(int argc, char **argv)
{
	char *path = NULL;
	char *query_key = NULL;
	char *prefix = NULL;
	char *buf;
	int ret, opt, mode = 'l';

	while ((opt = getopt(argc, argv, "hp:q:td")) != -1) {
		switch (opt) {
		case 'p':
			prefix = strdup(optarg);
			break;
		case 'q':
			query_key = strdup(optarg);
			break;
		/* Output mode */
		case 't':
		case 'd':
			mode = opt;
			break;
		case 'h':
		default:
			return usage();
		}
	}

	if (optind >= argc || strcmp(argv[optind], "-")) {
		path = "(stdin)";
		ret = read_skc_input(&buf);
	} else {
		path = argv[optind];
		ret = load_skc_file(path, &buf);
	}
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
	/* Key - value iterating example */
	if (prefix) {
		struct skc_node *parent, *leaf, *vnode;
		char key[SKC_KEYLEN_MAX];
		const char *val;

		if (prefix[0] == '\0')
			parent = NULL;
		else {
			parent = skc_find_node(prefix);
			if (!parent) {
				printf("No key-value has %s prefix\n", prefix);
				return -ENOENT;
			}
		}
		skc_node_for_each_key_value(parent, leaf, val) {
			if (skc_node_compose_key(leaf, key, SKC_KEYLEN_MAX) < 0) {
				printf("Failed to compose key");
				return -EINVAL;
			}
			printf("%s = ", key);
			vnode = skc_node_get_child(leaf);
			if (vnode && skc_node_is_array(vnode)) {
				skc_array_for_each_value(vnode, val)
					printf("\"%s\"%s",
					       skc_node_get_data(vnode),
					       vnode->next ? ", " : "\n");
			} else
				printf("\"%s\"\n", val);
		}
		return 0;
	}

	/* Dumping SKC examples */
	switch (mode) {
	case 't':
	default:
		skc_show_tree();
		break;
	case 'd':
		skc_debug_dump();
		break;
	}

	return 0;
}

