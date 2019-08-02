// SPDX-License-Identifier: GPL-2.0
/*
 * Structure Kernel Commandline
 * Masami Hiramatsu <mhiramat@kernel.org>
 */
#include "compat.h"
#include "skc.h"

/*
 * Structured Kernel Commandline (SKC) is given as an ascii text on memory.
 * skc_parse() parses the text to build a simple tree. Each tree node is
 * simply whether key or value. A key node may have a next key node or/and
 * a child node (both key and value). A value node may have a next value
 * node (for array).
 */

struct skc_node skc_nodes[SKC_NODE_MAX];
int skc_node_num;
char *skc_data;
size_t skc_data_size;

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

static int copy_one_word(char *buf, const char *src, size_t size)
{
	const char *p = strchr(src, '.');
	int len;

	if (!p)
		len = strlen(src);
	else
		len = p - src;
	if (len + 1 >= size)
		return -E2BIG;
	strncpy(buf, src, len);
	buf[len] = '\0';

	return len;
}

int skc_iter_unmatched_words(struct skc_iter *iter, int n,
			     char *buf, size_t size)
{
	struct skc_node *pnode, *ppnode, *node = iter->cur_key;
	const char *p;
	int len, m = 0;

	p = skc_node_get_data(node) + iter->key_offs;
	do {
		/* Copy words from node */
		while (*p != '\0') {
			len = copy_one_word(buf, p, size);
			if (len < 0)
				return len;
			m++;
			if (n && n == m)
				return m;
			size -= len;
			buf += len;
			*buf++ = '.';
			p += len;
			if (*p == '.')
				p++;
		}
		ppnode = node;
		pnode = iter->cur_val_key;
		if (pnode == ppnode) {	/* No more keys */
			if (m)
				buf[-1] = '\0';
			return m;
		}

		do {
			node = pnode;
			pnode = skc_node_get_parent(node);
		} while (pnode != ppnode);

		p = skc_node_get_data(node);
	} while (1);
}


/* SKC Iterator */

static bool skc_iter_match_prefix(struct skc_iter *iter)
{
	const char *p = skc_node_get_data(iter->cur_key);
	int len = strlen(p);

	if (len > iter->prefix_len - iter->prefix_offs)
		len = iter->prefix_len - iter->prefix_offs;

	if (strncmp(p, iter->prefix + iter->prefix_offs, len))
		return false;

	p += len;
	if (*p != '.' && *p != '\0')
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

	iter->key_offs = len;
	if (*p == '.')
		iter->key_offs++;

	return true;
}

static void skc_iter_unwind_key(struct skc_iter *iter)
{
	const char *p;
	p = skc_node_get_data(iter->cur_key);
	/* Adjust '.' from offsets */
	if (iter->prefix[iter->prefix_offs] != '\0')
		iter->prefix_offs--;
	if (p[iter->key_offs] != '\0')
		iter->key_offs--;

	iter->prefix_offs -= iter->key_offs;
	iter->key_offs = 0;
}

static int skc_iter_find_next_key(struct skc_iter *iter)
{
	while (!iter->cur_key->next) {
		/* Back to parent key node */
		iter->cur_key = skc_node_get_parent(iter->cur_key);
		if (!iter->cur_key)
			return -ENOENT;
		iter->key_offs = strlen(skc_node_get_data(iter->cur_key));
		skc_iter_unwind_key(iter);
	}
	iter->cur_key = skc_node_get_next(iter->cur_key);

	return 0;
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
			iter->key_offs = 0;
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
	iter->key_offs = 0;
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

	skc_iter_unwind_key(iter);
	if (skc_iter_find_next_key(iter) < 0) {
		iter->cur_val_key = NULL;
		return NULL;
	}

	return skc_iter_find_next(iter);
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
const char *skc_get_value(const char *key)
{
	struct skc_node *node = skc_nodes;
	const char *p = key;

	while (node && skc_node_is_key(node)) {
		if (!skc_node_match_prefix(node, &p))
			node = skc_node_get_next(node);
		else {
			node = skc_node_get_child(node);
			if (*p == '\0')	{	/* Matching complete */
				if (node && skc_node_is_value(node))
					return skc_node_get_data(node);
				return !node ? "" : NULL;
			}
		}
	}
	return NULL;
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

int skc_parse(char *buf, size_t size)
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

static int skc_show_array(struct skc_node *node)
{
	int i = 0;

	printk("\"%s\"", skc_node_get_data(node));
	while (node->next) {
		node = skc_node_get_next(node);
		printk(", \"%s\"", skc_node_get_data(node));
		i++;
	}
	printk(";\n");
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
