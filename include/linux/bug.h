#ifndef _LINUX_BUG_H
#define _LINUX_BUG_H

#include <stdio.h>
#include <stdlib.h>

#define BUG_ON(cond)	\
	if (cond) {						\
		printf("Internal error(%s:%d, %s): %s\n",	\
			__FILE__, __LINE__, __func__, #cond );	\
		exit(1);					\
	}

#endif
