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

/* Simple real checksum */
int checksum(unsigned char *buf, int len)
{
	int i, sum = 0;

	for (i = 0; i < len; i++)
		sum += buf[i];

	return sum;
}

#define PAGE_SIZE	4096

int load_skc_fd(int fd, char **buf, int size)
{
	int ret;

	*buf = malloc(size + 1);
	if (!*buf)
		return -ENOMEM;

	ret = read(fd, *buf, size);
	if (ret < 0)
		return -errno;
	(*buf)[size] = '\0';

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

	ret = load_skc_fd(fd, buf, stat.st_size);

	close(fd);

	return ret;
}

int load_skc_from_initrd(int fd, char **buf)
{
	struct stat stat;
	int ret;
	u32 size = 0, csum = 0, rcsum;

	ret = fstat(fd, &stat);
	if (ret < 0)
		return -errno;

	if (stat.st_size < 8)
		return -EINVAL;

	if (lseek(fd, -8, SEEK_END) < 0) {
		printf("Faile to lseek: %d\n", -errno);
		return -errno;
	}

	if (read(fd, &size, sizeof(u32)) < 0)
		return -errno;

	if (read(fd, &csum, sizeof(u32)) < 0)
		return -errno;

	/* Wrong size, maybe no SKC here */
	if (stat.st_size < size + 8)
		return 0;

	if (lseek(fd, stat.st_size - 8 - size, SEEK_SET) < 0) {
		printf("Faile to lseek: %d\n", -errno);
		return -errno;
	}

	ret = load_skc_fd(fd, buf, size);
	if (ret < 0)
		return ret;

	/* Wrong Checksum, maybe no SKC here */
	rcsum = checksum((unsigned char *)*buf, size);
	if (csum != rcsum) {
		printf("checksum error: %d != %d\n", csum, rcsum);
		return 0;
	}

	ret = skc_init(*buf);
	/* Wrong data, maybe no SKC here */
	if (ret < 0)
		ret = 0;

	return size;
}

int show_skc(const char *path)
{
	int ret, fd;
	char *buf = NULL;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		printf("Failed to open initrd %s: %d\n", path, fd);
		return -errno;
	}

	ret = load_skc_from_initrd(fd, &buf);
	if (ret < 0)
		printf("Failed to load SKC from initrd: %d\n", ret);
	else
		skc_show_tree();

	close(fd);
	free(buf);

	return ret;
}

int delete_skc(const char *path)
{
	struct stat stat;
	int ret = 0, fd, size;
	char *buf = NULL;

	fd = open(path, O_RDWR);
	if (fd < 0) {
		printf("Failed to open initrd %s: %d\n", path, fd);
		return -errno;
	}

	size = load_skc_from_initrd(fd, &buf);
	if (size < 0) {
		ret = size;
		printf("Failed to load SKC from initrd: %d\n", ret);
	} else if (size > 0) {
		ret = fstat(fd, &stat);
		if (!ret)
			ret = ftruncate(fd, stat.st_size - size - 8);
		if (ret)
			ret = -errno;
	} /* Ignore if there is no SKC in initrd */

	close(fd);
	free(buf);

	return ret;
}

int append_skc(const char *path, const char *append)
{
	u32 size, csum;
	char *buf, *data;
	int ret, fd;

	ret = load_skc_file(append, &buf);
	if (ret < 0) {
		printf("Failed to load %s : %d\n", append, ret);
		return ret;
	}
	size = strlen(buf) + 1;
	csum = checksum((unsigned char *)buf, size);

	/* Prepare append data */
	data = malloc(size + 8);
	if (!data)
		return -ENOMEM;
	strcpy(data, buf);
	*(u32 *)(data + size) = size;
	*(u32 *)(data + size + 4) = csum;

	/* Check the data format */
	ret = skc_init(buf);
	if (ret < 0) {
		printf("Failed to parse %s: %d\n", append, ret);
		return ret;
	}
	/* TODO: Check the options by schema */
	free(buf);

	ret = delete_skc(path);
	if (ret < 0)
		return ret;

	fd = open(path, O_RDWR | O_APPEND);
	if (fd < 0) {
		printf("Failed to open %s: %d\n", path, fd);
		return fd;
	}
	ret = write(fd, data, size + 8);
	if (ret < 0) {
		printf("Failed to append SKC: %d\n", ret);
		return ret;
	}
	close(fd);
	free(data);

	return 0;
}

int usage(void)
{
	printf("Usage: skced [-a <SKC>|-d] <INITRD>\n");
	return -1;
}

int main(int argc, char **argv)
{
	char *path = NULL;
	char *append = NULL;
	bool delete = false;
	int opt;

	while ((opt = getopt(argc, argv, "hda:")) != -1) {
		switch (opt) {
		case 'd':
			delete = true;
			break;
		case 'a':
			append = strdup(optarg);
			break;
		case 'h':
		default:
			return usage();
		}
	}

	if (append && delete) {
		printf("You can not specify -a and -d\n");
		return usage();
	}

	if (optind >= argc) {
		printf("No initrd is specified.\n");
		return usage();
	}

	path = argv[optind];

	if (append) {
		append_skc(path, append);
	} else if (delete) {
		delete_skc(path);
	} else {	/* List */
		show_skc(path);
	}

	return 0;
}

