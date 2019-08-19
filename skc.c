// SPDX-License-Identifier: GPL-2.0
/*
 * Structure Kernel Commandline
 * Masami Hiramatsu <mhiramat@kernel.org>
 */

#define pr_fmt(fmt)    "skc: " fmt

#include <linux/bug.h>
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/skc.h>
#include <linux/string.h>

/*
 * Structured Kernel Commandline (SKC) is given as an ascii text on memory.
 * skc_parse() parses the text to build a simple tree. Each tree node is
 * simply whether key or value. A key node may have a next key node or/and
 * a child node (both key and value). A value node may have a next value
 * node (for array).
 */

static struct skc_node skc_nodes[SKC_NODE_MAX];
static int skc_node_num;
static char *skc_data;
static size_t skc_data_size;
static struct skc_node *last_parent;

static int skc_parse_error(const char *msg, const char *p)
{
	int line = 0, col = 0;
	int i, pos = p - skc_data;

	for (i = 0; i < pos; i++) {
		if (skc_data[i] == '\n') {
			line++;
			col = pos - i;
		}
	}
	pr_err("Parse error at line %d, col %d: %s\n", line + 1, col, msg);
	return -EINVAL;
}

int skc_node_index(struct skc_node *node)
{
	return node - &skc_nodes[0];
}

struct skc_node *skc_node_get_parent(struct skc_node *node)
{
	return node->parent == SKC_NODE_MAX ? NULL : &skc_nodes[node->parent];
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

	if (skc_node_get_parent(node)) {
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

int skc_iter_unmatched_words(struct skc_iter *iter, int n,
			     char *buf, size_t size)
{
	struct skc_node *pnode, *ppnode, *node;
	const char *p;
	int len, m = 0;

	ppnode = iter->cur_key;
	pnode = iter->cur_val_key;
	while (pnode != ppnode) {
		/* Search child node connected to cur_val_key */
		while (pnode != ppnode) {
			node = pnode;
			pnode = skc_node_get_parent(node);
		}

		p = skc_node_get_data(node);
		len = strlen(p);
		if (size < len + 1)
			return -E2BIG;
		strcpy(buf, p);
		m++;
		if (n && n == m)
			return m;
		size -= len + 1;
		buf += len;

		ppnode = node;
		pnode = iter->cur_val_key;
		if (pnode != ppnode)
			*buf++ = '.';
	}

	return m;
}


/* SKC Iterator */

static bool skc_iter_match_prefix(struct skc_iter *iter)
{
	const char *p = skc_node_get_data(iter->cur_key);
	int len = strlen(p);

	if (len > iter->prefix_len - iter->prefix_offs)
		return false;

	if (strncmp(p, iter->prefix + iter->prefix_offs, len))
		return false;

	switch (iter->prefix[iter->prefix_offs + len]) {
	case '.':
		iter->prefix_offs += len + 1;
		break;
	case '\0':
		iter->prefix_offs += len;
		break;
	default:
		return false;
	}

	return true;
}

static int skc_iter_find_next_key(struct skc_iter *iter)
{
	iter->cur_key = skc_node_get_next(iter->cur_key);

	return iter->cur_key ? 0 : -ENOENT;
}

static const char *skc_iter_find_next_value(struct skc_iter *iter)
{
	struct skc_node *node = iter->cur_val_key;

	while (node && skc_node_is_key(node)) {
		iter->cur_val_key = node;
		node = skc_node_get_child(node);
	}

	return node ? skc_node_get_data(node) : "";
}

static const char *skc_iter_find_next(struct skc_iter *iter)
{
	while (iter->cur_key && skc_node_is_key(iter->cur_key)) {
		if (!skc_iter_match_prefix(iter)) {
			if (skc_iter_find_next_key(iter) < 0)
				goto out;
			continue;
		}
		if (iter->prefix[iter->prefix_offs] != '\0') {
			/* Partially matched, need to dig deeper */
			iter->cur_key = skc_node_get_child(iter->cur_key);
			continue;
		} else { /* Matching complete */
			iter->cur_val_key = iter->cur_key;
			return skc_iter_find_next_value(iter);
		}
	}
out:
	/* Failed to find key */
	iter->cur_val_key = NULL;

	return NULL;
}

const char *skc_iter_start(struct skc_iter *iter, const char *prefix)
{
	iter->cur_key = skc_nodes;
	iter->cur_val_key = NULL;
	iter->prefix = prefix;
	iter->prefix_len = strlen(prefix);
	iter->prefix_offs = 0;

	return skc_iter_find_next(iter);
}

const char *skc_iter_next(struct skc_iter *iter)
{
	struct skc_node *node = iter->cur_val_key;

	if (!node)
		return NULL;

	while (node != iter->cur_key) {
		if (node->next) {
			iter->cur_val_key = skc_node_get_next(node);
			return skc_iter_find_next_value(iter);
		}
		node = skc_node_get_parent(node);
	}

	return NULL;
}

static bool skc_node_match_prefix(struct skc_node *node, const char **prefix)
{
	const char *p = skc_node_get_data(node);
	int len = strlen(p);

	if (strncmp(*prefix, p, len))
		return false;

	p = *prefix + len;
	if (*p == '.')
		p++;
	else if (*p != '\0')
		return false;

	*prefix = p;

	return true;
}

/* key-only data returns "", no key matched return NULL  */
const char *skc_get_value(const char *key, struct skc_node **value)
{
	struct skc_node *node = skc_nodes;
	const char *p = key;

	while (node && skc_node_is_key(node)) {
		if (!skc_node_match_prefix(node, &p))
			node = skc_node_get_next(node);
		else {
			node = skc_node_get_child(node);
			if (*p == '\0')	{	/* Matching complete */
				if (node && skc_node_is_value(node)) {
					*value = node;
					return skc_node_get_data(node);
				}
				*value = NULL;
				return !node ? "" : NULL;
			}
		}
	}
	return NULL;
}

/* SKC parse and tree build */

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
	node->next = 0;

	return node;
}

static struct skc_node *skc_last_sibling(struct skc_node *node)
{
	while (node->next)
		node = skc_node_get_next(node);

	return node;
}

static struct skc_node *skc_add_sibling(char *data, u32 flag)
{
	struct skc_node *sib, *node = skc_add_node(data, flag);

	if (node) {
		if (!last_parent) {
			node->parent = SKC_NODE_MAX;
			sib = skc_last_sibling(skc_nodes);
			sib->next = skc_node_index(node);
		} else {
			node->parent = skc_node_index(last_parent);
			if (!last_parent->child) {
				last_parent->child = skc_node_index(node);
			} else {
				sib = skc_node_get_child(last_parent);
				sib = skc_last_sibling(sib);
				sib->next = skc_node_index(node);
			}
		}
	}
	return node;
}

static struct skc_node *skc_add_child(char *data, u32 flag)
{
	struct skc_node *node = skc_add_sibling(data, flag);

	if (node)
		last_parent = node;

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
			return skc_parse_error("No closing quotation", v);
		*p++ = '\0';
		p = skip_spaces(p);
		if (*p != ',' && *p != ';')
			return skc_parse_error("No delimiter for value", v);
		c = *p;
		*p++ = '\0';
	} else {
		p = strpbrk(v, ",;");
		if (!p)
			return skc_parse_error("No delimiter for value", v);
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

		node = skc_add_sibling(*__v, SKC_VALUE);
		if (!node)
			return -ENOMEM;
		*__v = next;
	} while (c != ';');
	node->next = 0;

	return 0;
}

static struct skc_node *find_match_node(struct skc_node *node, char *k)
{
	while (node) {
		if (!strcmp(skc_node_get_data(node), k))
			break;
		node = skc_node_get_next(node);
	}
	return node;
}

static int __skc_add_key(char *k)
{
	struct skc_node *node;

	if (!skc_valid_key(k))
		return skc_parse_error("Invalid key", k);

	if (unlikely(skc_node_num == 0))
		goto add_node;

	if (!last_parent)	/* the first level */
		node = find_match_node(skc_nodes, k);
	else
		node = find_match_node(skc_node_get_child(last_parent), k);

	if (node)
		last_parent = node;
	else {
add_node:
		node = skc_add_child(k, SKC_KEY);
		if (!node)
			return -ENOMEM;
	}
	return 0;
}

static int __skc_parse_keys(char *k)
{
	char *p;
	int ret;

	k = strim(k);
	while ((p = strchr(k, '.'))) {
		*p++ = '\0';
		ret = __skc_add_key(k);
		if (ret)
			return ret;
		k = p;
	}

	return __skc_add_key(k);
}

static int skc_parse_kv(char **k, char *v)
{
	struct skc_node *prev_parent = last_parent;
	struct skc_node *node;
	char *next;
	int c, ret;

	ret = __skc_parse_keys(*k);
	if (ret)
		return ret;

	c = __skc_parse_value(&v, &next);
	if (c < 0)
		return c;

	node = skc_add_sibling(v, SKC_VALUE);
	if (!node)
		return -ENOMEM;

	if (c == ',') {	/* Array */
		ret = skc_parse_array(&next);
		if (ret < 0)
			return ret;
	}

	last_parent = prev_parent;

	*k = next;

	return 0;
}

static int skc_parse_key(char **k, char *n)
{
	struct skc_node *prev_parent = last_parent;
	int ret;

	*k = strim(*k);
	if (**k != '\0') {
		ret = __skc_parse_keys(*k);
		if (ret)
			return ret;
		last_parent = prev_parent;
	}

	*k = n;

	return 0;
}

static int skc_open_brace(char **k, char *n)
{
	int ret;

	ret = __skc_parse_keys(*k);
	if (ret)
		return ret;

	/* Mark the last key as open brace */
	last_parent->next = SKC_NODE_MAX;

	*k = n;

	return 0;
}

static int skc_close_brace(char **k, char *n)
{
	struct skc_node *node;

	*k = strim(*k);
	if (**k != '\0')
		return skc_parse_error("Unexpected key, maybe forgot ;?", *k);

	if (!last_parent || last_parent->next != SKC_NODE_MAX)
		return skc_parse_error("Unexpected closing brace", *k);

	node = last_parent;
	node->next = 0;
	do {
		node = skc_node_get_parent(node);
	} while (node && node->next != SKC_NODE_MAX);
	last_parent = node;

	*k = n;

	return 0;
}

static int skc_verify_tree(void)
{
	int i, last = 0;

	for (i = 0; i < skc_node_num; i++) {
		if (skc_nodes[i].next > skc_node_num) {
			BUG_ON(skc_node_is_value(skc_nodes + i));
			return skc_parse_error("No closing brace",
				skc_node_get_data(skc_nodes + i));
		} else if (skc_nodes[i].next == skc_node_num) {
			if (WARN_ON(last)) {
				printk("Previous last node = %d\n", last);
				skc_dump();
			}
			last = i;
			skc_nodes[i].next = 0;
		}
	}

	return 0;
}

/* Setup SKC internal tree */
int skc_init(char *buf)
{
	char *p, *q;
	int ret, c;

	if (skc_data)
		return -EBUSY;

	skc_data = buf;
	skc_data_size = strlen(buf);

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
			return skc_parse_error("No delimiter", p);
	}

	ret = skc_verify_tree();
	if (!ret) {
		pr_info("Structured kernel cmdline:\n");
		skc_show_kvlist();
	}

	return ret;
}

/* Dump current skc */
void skc_dump(void)
{
	int i;

	for (i = 0; i < skc_node_num; i++) {
		printk("[%d] %s (%s) .next=%d, .child=%d .parent=%d\n", i,
			skc_node_get_data(skc_nodes + i),
			skc_node_is_value(skc_nodes + i) ? "value" : "key",
			skc_nodes[i].next, skc_nodes[i].child,
			skc_nodes[i].parent);
	}
}

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

void skc_show_kvlist(void)
{
	struct skc_node *node;
	char buf[SKC_KEYLEN_MAX];
	int i;

	for (i = 0; i < skc_node_num; i++) {
		node = skc_nodes + i;
		if (skc_node_is_value(node)) {
			skc_node_compose_key(node, buf, SKC_KEYLEN_MAX);
			printk("%s = ", buf);
			if (skc_nodes[i].next)
				i += skc_show_array(skc_nodes + i);
			else
				printk("\"%s\";\n", skc_node_get_data(node));
		}
	}
}
