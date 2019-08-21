/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SKC_H
#define _LINUX_SKC_H

/* SKC tree node */
struct skc_node {
	u16 next;
	u16 child;
	u16 parent;
	u16 data;
} __attribute__ ((__packed__));

#define SKC_KEY		0
#define SKC_VALUE	(1 << 15)

#define SKC_NODE_MAX	512
#define SKC_KEYLEN_MAX	256

/* Node tree access raw API's */
struct skc_node *skc_root_node(void);
int skc_node_index(struct skc_node *node);
struct skc_node *skc_node_get_parent(struct skc_node *node);
struct skc_node *skc_node_get_child(struct skc_node *node);
struct skc_node *skc_node_get_next(struct skc_node *node);
const char *skc_node_get_data(struct skc_node *node);

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

/* Tree-based key-value access APIs */
struct skc_node *skc_node_find_child(struct skc_node *parent, const char *key);

const char * skc_node_find_value(struct skc_node *parent, const char *key,
				 struct skc_node **result);

/* SKC key-value accessor */
static inline const char *
skc_find_value(const char *key, struct skc_node **value)
{
	return skc_node_find_value(NULL, key, value);
}

static inline struct skc_node *skc_find_node(const char *key)
{
	return skc_node_find_child(NULL, key);
}

static inline bool skc_node_is_array(struct skc_node *node)
{
	return skc_node_is_value(node) && node->next != 0;
}

#define skc_array_for_each_value(anode, value)		\
	for (value = skc_node_get_data(anode); anode != NULL ;	\
	     anode = skc_node_get_next(anode),	\
	     value = anode ? skc_node_get_data(anode) : NULL)

#define skc_node_for_each_child(parent, child)		\
	for (child = skc_node_get_child(parent); child != NULL ;	\
	     child = skc_node_get_next(child))

#define skc_node_for_each_value(node, key, anode, value)	\
	for (value = skc_node_find_value(node, key, &anode); value != NULL; \
	     anode = skc_node_get_next(anode),	\
	     value = anode ? skc_node_get_data(anode) : NULL)

/* Compose complete key */
int skc_node_compose_key(struct skc_node *node, char *buf, size_t size);

/* SKC node initializer */
int skc_init(char *buf);

/* Debug dump functions */
void skc_dump(void);
void skc_show_tree(void);
void skc_show_kvlist(void);

#endif
