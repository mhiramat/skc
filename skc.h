/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SKC_H
#define _LINUX_SKC_H

struct skc_node {
	u16 next;
	u16 child;
	u32 data;
} __attribute__ ((__packed__));

#define SKC_KEY		0
#define SKC_VALUE	(1 << 31)

#define SKC_NODE_MAX	512
#define SKC_KEYLEN_MAX	256

/* SKC node accessors */
static inline bool skc_node_is_value(struct skc_node *node)
{
	return !!(node->data & SKC_VALUE);
}

static inline bool skc_node_is_key(struct skc_node *node)
{
	return !(node->data & SKC_VALUE);
}

int skc_node_index(struct skc_node *node);
struct skc_node *skc_node_get_parent(struct skc_node *node);
struct skc_node *skc_node_get_child(struct skc_node *node);
struct skc_node *skc_node_get_next(struct skc_node *node);
const char *skc_node_get_data(struct skc_node *node);

/* Compose complete key */
int skc_node_compose_key(struct skc_node *node, char *buf, size_t size);

/* SKC node initializer */
int skc_parse(char *buf, size_t size);

/* Debug dump functions */
void skc_dump(void);
void skc_show_tree(void);
void skc_show_kvlist(void);


#endif
