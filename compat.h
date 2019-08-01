/* SPDX-License-Identifier: GPL-2.0 */

/* Linux compatible definitions etc. */

#include <stdio.h>
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

static inline char *skip_spaces(const char *str)
{
	while (isspace(*str))
		++str;
	return (char *)str;
}

static inline char *strim(char *s)
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

#define GFP_KERNEL	0

static inline void *kzalloc(size_t size, int flags)
{
	return calloc(size, 1);
}

