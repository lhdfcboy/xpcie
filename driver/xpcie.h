#include <linux/types.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/poll.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/aio.h>
#include <linux/splice.h>
#include <linux/version.h>
#include <linux/uio.h>



#define DRV_NAME "xpcie"
#define DRV_NAME_USER DRV_NAME "_user"
#define DRV_NAME_H2C DRV_NAME "_h2c"
#define DRV_NAME_C2H DRV_NAME "_c2h"

#ifndef VM_RESERVED
#define VMEM_FLAGS (VM_IO | VM_DONTEXPAND | VM_DONTDUMP)
#else
#define VMEM_FLAGS (VM_IO | VM_RESERVED)
#endif

#define XPCIE_DEBUG 1
#if (XPCIE_DEBUG == 0)
#define dbg_desc(...)
#define dbg_io(...)
#define dbg_fops(...)
#define dbg_perf(fmt, ...)
#define dbg_sg(...)
#define dbg_tfr(...)
#define dbg_irq(...)
#define dbg_init(...)

#else
/* descriptor, ioread/write, scatter-gather, transfer debugging */
#define dbg_desc(fmt, ...) pr_debug("%s():" fmt, \
		__func__, ##__VA_ARGS__)

#define dbg_io(fmt, ...) pr_debug("%s():" fmt, \
		__func__, ##__VA_ARGS__)

#define dbg_fops(fmt, ...) pr_debug("%s():" fmt, \
		__func__, ##__VA_ARGS__)

#define dbg_perf(fmt, ...) pr_debug("%s():" fmt, \
		__func__, ##__VA_ARGS__)

#define dbg_sg(fmt, ...) pr_debug("%s():" fmt, \
		__func__, ##__VA_ARGS__)

#define dbg_irq(fmt, ...) pr_debug("%s():" fmt, \
		__func__, ##__VA_ARGS__)

#define dbg_init(fmt, ...) pr_debug("%s():" fmt, \
		__func__, ##__VA_ARGS__)

#define dbg_tfr(fmt, ...) pr_debug("%s(): %s%c: " fmt, \
		__func__, \
		engine ? (engine->number_in_channel ? "C2H" : "H2C") \
		: "---", engine ? '0' + engine->channel : '-', ##__VA_ARGS__)
#endif
