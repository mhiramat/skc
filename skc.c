/* 
 * Structure Kernel Commandline 
 * Masami Hiramatsu <mhiramat@kernel.org>
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>

typedef unsigned short u16;
typedef unsigned int   u32;
#define BUG_ON(cond)	\
	if (cond) {						\
		printf("Internal error(%s:%d, %s): %s\n",	\
			__FILE__, __LINE__, __func__, #cond );	\
		exit(1);					\
	}
#define printk	printf

char *skip_spaces(const char *str)
{
	while (isspace(*str))
		++str;
	return (char *)str;
}

char *strim(char *s)
{
	size_t size;
	char *end;

	size = strlen(s);
	if (!size)
		return s;

	end = s + size - 1;
	while (end >= s && isspace(*end))
		end--;
	*(end + 1) = '\0';

	return skip_spaces(s);
}

struct skc_node {
	u16 next;
	u16 child;
	u32 data;
} __attribute__ ((__packed__));

#define SKC_KEY		0
#define SKC_VALUE	(1 << 31)

#define SKC_NODE_MAX	512
struct skc_node skc_nodes[SKC_NODE_MAX];
int skc_node_num;
char *skc_data;
int skc_data_size;

static int __skc_parse_error(const char *func, const char *str, const char *p)
{
	int line = 0, col = 0;
	int i, pos = p - skc_data;

	for (i = 0; i < pos; i++) {
		if (skc_data[i] == '\n') {
			line++;
			col = pos - i;
		}
	}
	printk("Parse error @%s (data: %d:%d): %s\n", func,
		line + 1, col, str);
	return -EINVAL;
}

#define skc_parse_error(m, p)	__skc_parse_error(__func__, #m, p)

bool skc_node_is_value(struct skc_node *node)
{
	return !!(node->data & SKC_VALUE);
}

bool skc_node_is_key(struct skc_node *node)
{
	return !(node->data & SKC_VALUE);
}

int skc_node_index(struct skc_node *node)
{
	return node - &skc_nodes[0];
}

struct skc_node *skc_node_get_parent(struct skc_node *node)
{
	int idx = skc_node_index(node);

	while (node != &skc_nodes[0]) {
		node--;
		if (node->next == idx)
			idx = skc_node_index(node);
		else if (node->child == idx)
			return node;
	}

	return NULL;
}

struct skc_node *skc_node_get_child(struct skc_node *node)
{
	return node->child ? &skc_nodes[node->child] : NULL;
}

struct skc_node *skc_node_get_next(struct skc_node *node)
{
	return node->next ? &skc_nodes[node->next] : NULL;
}

const char *skc_node_get_data(struct skc_node *node)
{
	int offset = node->data & ~SKC_VALUE;

	return offset >= skc_data_size ? NULL : skc_data + offset;
}

int skc_node_compose_key(struct skc_node *node, char *buf, size_t size)
{
	int ret = 0;

	if (skc_node_is_value(node))
		node = skc_node_get_parent(node);

	if (skc_node_index(node) != 0) {
		ret = skc_node_compose_key(skc_node_get_parent(node),
					   buf, size);
		if (ret >= size)
			return -E2BIG;
		if (ret < 0)
			return ret;

		buf += ret;
		size -= ret;
	}
	return snprintf(buf, size, "%s%s", ret == 0 ? "" : ".",
			skc_node_get_data(node)) + ret;
}

static struct skc_node *skc_add_node(char *data, u32 flag)
{
	struct skc_node *node;
	unsigned long offset;

	if (skc_node_num == SKC_NODE_MAX)
		return NULL;

	node = &skc_nodes[skc_node_num++];
	offset = data - skc_data;
	node->data = (u32)offset;
	BUG_ON(offset != node->data);
	node->data |= flag;
	node->child = 0;
	node->next = skc_node_num;

	return node;
}

static struct skc_node *skc_peek_node(void)
{
	return (skc_node_num == 0) ? NULL : &skc_nodes[skc_node_num - 1];
}

static struct skc_node *skc_push_node(char *data, u32 flag)
{
	struct skc_node *parent = skc_peek_node();
	struct skc_node *node;
 
	BUG_ON(parent == NULL);

	node = skc_add_node(data, flag);
	if (!node)
		return NULL;
	parent->child = parent->next;
	parent->next = skc_node_num;

	return node;
}

static bool skc_valid_key(char *key)
{
	if (key[0] == '\0')
		return false;

	while (isalnum(*key) || *key == '.' || *key == '_')
		key++;
	return *key == '\0';
}

static char *find_ending_quote(char *str)
{
	char *p = str;

	do {
		p = strchr(p + 1, '"');
		if (!p)
			goto end;
	} while (*(p - 1) == '\\');
end:
	return p;
}

/* Return delimiter or error, no node added */
static int __skc_parse_value(char **__v, char **__n)
{
	char *p, *v = *__v;
	int c;

	v = skip_spaces(v);
	if (*v == '"') {
		v++;
		p = find_ending_quote(v);
		if (!p)
			return skc_parse_error(NOENDQUO, v);
		*p++ = '\0';
		p = skip_spaces(p);
		if (*p != ',' && *p != ';')
			return skc_parse_error(NODELIM, v);
		c = *p;
		*p++ = '\0';
	} else {
		p = strpbrk(v, ",;");
		if (!p)
			return skc_parse_error(NODELIM, v);
		c = *p;
		*p++ = '\0';
		v = strim(v);
	}
	*__v = v;
	*__n = p;

	return c;
}

static int skc_parse_array(char **__v)
{
	struct skc_node *node;
	char *next;
	int c = 0;

	do {
		c = __skc_parse_value(__v, &next);
		if (c < 0)
			return c;

		node = skc_add_node(*__v, SKC_VALUE);
		if (!node)
			return -ENOMEM;
		*__v = next;
	} while (c != ';');
	node->next = 0;

	return 0;
}

static int skc_parse_kv(char **k, char *v)
{
	struct skc_node *node, *knode;
	char *next;
	int c, ret;

	*k = strim(*k);
	if (!skc_valid_key(*k))
		return skc_parse_error(INVKEY, *k);

	knode = skc_add_node(*k, SKC_KEY);
	if (!knode)
		return -ENOMEM;

	c = __skc_parse_value(&v, &next);
	if (c < 0)
		return c;

	node = skc_push_node(v, SKC_VALUE);
	if (c == ',') {	/* Array */
		ret = skc_parse_array(&next);
		if (ret < 0)
			return ret;
	} else	/* End */
		node->next = 0;

	knode->next = skc_node_num;

	*k = next;

	return 0;
}

static int skc_parse_key(char **k, char *n)
{
	*k = strim(*k);

	if (**k == '\0') /* Empty item */
		goto skipped;
	if (!skc_valid_key(*k))
		return skc_parse_error(INVKEY, *k);
	if (!skc_add_node(*k, SKC_KEY))
		return -ENOMEM;
skipped:
	*k = n;

	return 0;
}

static int skc_open_brace(char **k, char *n)
{
	struct skc_node *node;

	*k = strim(*k);
	if (!skc_valid_key(*k))
		return skc_parse_error(INVKEY, *k);
	node = skc_add_node(*k, SKC_KEY);
	if (!node)
		return -ENOMEM;
	node->child = skc_node_num;
	node->next = SKC_NODE_MAX;

	*k = n;

	return 0;
}

static int skc_close_brace(char **k, char *n)
{
	struct skc_node *node;

	node = skc_peek_node();
	if (!node)
		return skc_parse_error(UNEXPCLO, *k);

	if (skc_node_is_value(node)) {
		node = skc_node_get_parent(node);
		BUG_ON(!node);
	}
	while (node->next == 0) {
		node = skc_node_get_parent(node);
		if (!node)
			return skc_parse_error(TOOMANYCLO, *k);
	}
	node->next = 0;
	node = skc_node_get_parent(node);
	if (!node)
		return skc_parse_error(UNEXPCLO, *k);

	node->next = skc_node_num;

	*k = n;

	return 0;
}

static int skc_verify_tree(void)
{
	int i, top = 0;

	for (i = 0; i < skc_node_num; i++) {
		if (skc_nodes[i].next > skc_node_num) {
			BUG_ON(skc_node_is_value(skc_nodes + i));
			return skc_parse_error(NOBRACECLO,
				skc_node_get_data(skc_nodes + i));
		} else if (skc_nodes[i].next == skc_node_num) {
			BUG_ON(top);
			top = i;
			skc_nodes[i].next = 0;
		}
	}

	return 0;
}

int skc_parse(char *buf, int size)
{
	char *p, *q;
	int ret, c;

	skc_data = buf;
	skc_data_size = size;

	p = buf;
	do {
		q = strpbrk(p, "{}=;");
		if (!q)
			break;
		c = *q;
		*q++ = '\0';
		switch (c) {
		case '=':
			ret = skc_parse_kv(&p, q);
			break;
		case '{':
			ret = skc_open_brace(&p, q);
			break;
		case ';':
			ret = skc_parse_key(&p, q);
			break;
		case '}':
			ret = skc_close_brace(&p, q);
			break;
		}
	} while (ret == 0);

	if (ret < 0)
		return ret;

	if (!q) {
		p = skip_spaces(p);
		if (*p != '\0')
			return skc_parse_error(UNEXPSTR, p);
	}

	return skc_verify_tree();
}

/* Dump current skc */
void skc_dump(void)
{
	int i;

	for (i = 0; i < skc_node_num; i++) {
		printk("[%d] %s (%s) .next=%d, .child=%d\n", i,
			skc_node_get_data(skc_nodes + i),
			skc_node_is_value(skc_nodes + i) ? "value" : "key",
			skc_nodes[i].next, skc_nodes[i].child);
	}
}

void skc_show_array(struct skc_node *node)
{
	printk("\"%s\"", skc_node_get_data(node));
	while (node->next) {
		node = skc_node_get_next(node);
		printk(", \"%s\"", skc_node_get_data(node));
	}
	printk(";\n");
}

void skc_show_tree(void)
{
	struct skc_node *node, *cnode;
	int depth = 0, i;

	node = skc_nodes;
	while (skc_node_is_key(node)) {
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

#define SKC_MAX_KEYLEN 256

void skc_show_kvlist(void)
{
	struct skc_node *node;
	char buf[SKC_MAX_KEYLEN];
	int i;

	for (i = 0; i < skc_node_num; i++) {
		node = skc_nodes + i;
		if (skc_node_is_value(node)) {
			skc_node_compose_key(node, buf, SKC_MAX_KEYLEN);
			printk("%s = ", buf);
			if (skc_nodes[i].next)
				skc_show_array(skc_nodes + i);
			else
				printk("\"%s\";\n", skc_node_get_data(node));
		}
	}
}

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

	ret = skc_parse(buf, ret);

	printf("parsed : %d\n", ret);

	skc_dump();
	printf("\n=========================\n\n");
	skc_show_tree();
	printf("\n=========================\n\n");
	skc_show_kvlist();

	return 0;
}

