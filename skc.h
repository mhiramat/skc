/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SKC_H
#define _LINUX_SKC_H

/* SKC tree node */
struct skc_node {
	u16 next;
	u16 child;
	u32 data;
} __attribute__ ((__packed__));

#define SKC_KEY		0
#define SKC_VALUE	(1 << 31)

#define SKC_NODE_MAX	512
#define SKC_KEYLEN_MAX	256

/* Node tree access raw API's */
int skc_node_index(struct skc_node *node);
struct skc_node *skc_node_get_parent(struct skc_node *node);
struct skc_node *skc_node_get_child(struct skc_node *node);
struct skc_node *skc_node_get_next(struct skc_node *node);

/* SKC node accessors */
static inline bool skc_node_is_value(struct skc_node *node)
{
	return !!(node->data & SKC_VALUE);
}

static inline bool skc_node_is_key(struct skc_node *node)
{
	return !(node->data & SKC_VALUE);
}

static inline bool skc_node_is_array_value(struct skc_node *node)
{
	return skc_node_is_value(node) && node->next;
}

const char *skc_node_get_data(struct skc_node *node);

/* Compose complete key */
int skc_node_compose_key(struct skc_node *node, char *buf, size_t size);

/* SKC key-value accessor */
const char *skc_get_value(const char *key);

/* SKC node initializer */
int skc_parse(char *buf, size_t size);

/* Debug dump functions */
void skc_dump(void);
void skc_show_tree(void);
void skc_show_kvlist(void);

/* Iterator interface */
struct skc_iter {
	struct skc_node *cur_key;	/* matched intermediate key */
	struct skc_node *cur_val_key;	/* k-v pair key node */
	const char 	*prefix;
	int		prefix_len;
	int 		prefix_offs;	/* matched position of prefix */
	int		key_offs;	/* matched position of cur_key */
};

const char *skc_iter_start(struct skc_iter *iter, const char *prefix);
const char *skc_iter_next(struct skc_iter *iter);

/* This iterates the value nodes whoes key is matched to prefix */
#define skc_for_each_value(iter, prefix, value)	\
	for (value = skc_iter_start(iter, prefix); \
	     value != NULL; value = skc_iter_next(iter))

/*
 * This returns current value node. Note that if the key has no value
 * (key-only node) returns NULL.
 */
static inline struct skc_node *skc_iter_value_node(struct skc_iter *iter)
{
	return iter->cur_val_key ? skc_node_get_child(iter->cur_val_key) : NULL;
}

/* For composing key, you may need the key node */
static inline struct skc_node *skc_iter_key_node(struct skc_iter *iter)
{
	return iter->cur_val_key;
}

/*
 * Get unmatched part of words in key (n = 0 then all of the words).
 * Returns how many words are copied.
 */
int skc_iter_unmatched_words(struct skc_iter *iter, int n,
				char *buf, size_t size);

#endif
