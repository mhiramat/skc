#ifndef _LINUX_KERNEL_H
#define _LINUX_KERNEL_H

#include <stdlib.h>
#include <stdbool.h>

#include <linux/printk.h>

typedef unsigned short u16;
typedef unsigned int   u32;

#define unlikely(cond)	(cond)

#define __init
#define __initdata

#endif
