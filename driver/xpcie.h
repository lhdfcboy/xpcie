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


/* SECTION: Structure definitions */

struct config_regs {
	u32 identifier;
	u32 reserved_1[4];
	u32 msi_enable;
};

/**
* SG DMA Controller status and control registers
*
* These registers make the control interface for DMA transfers.
*
* It sits in End Point (FPGA) memory BAR[0] for 32-bit or BAR[0:1] for 64-bit.
* It references the first descriptor which exists in Root Complex (PC) memory.
*
* @note The registers must be accessed using 32-bit (PCI DWORD) read/writes,
* and their values are in little-endian byte ordering.
*/

/* Target internal components on XDMA control BAR */
#define XDMA_OFS_INT_CTRL	(0x2000UL)
#define XDMA_OFS_CONFIG		(0x3000UL)

#define XDMA_CHANNEL_NUM_MAX (4)

/* maximum number of bytes per transfer request */
#define XDMA_TRANSFER_MAX_BYTES (2048 * 4096)

/* maximum size of a single DMA transfer descriptor */
#define XDMA_DESC_MAX_BYTES ((1 << 18) - 1)

/* bits of the SG DMA control register */
#define XDMA_CTRL_RUN_STOP			(1UL << 0)
#define XDMA_CTRL_IE_DESC_STOPPED		(1UL << 1)
#define XDMA_CTRL_IE_DESC_COMPLETED		(1UL << 2)
#define XDMA_CTRL_IE_DESC_ALIGN_MISMATCH	(1UL << 3)
#define XDMA_CTRL_IE_MAGIC_STOPPED		(1UL << 4)
#define XDMA_CTRL_IE_IDLE_STOPPED		(1UL << 6)
#define XDMA_CTRL_IE_READ_ERROR			(0x1FUL << 9)
#define XDMA_CTRL_IE_DESC_ERROR			(0x1FUL << 19)
#define XDMA_CTRL_NON_INCR_ADDR			(1UL << 25)
#define XDMA_CTRL_POLL_MODE_WB			(1UL << 26)

/* bits of the SG DMA status register */
#define XDMA_STAT_BUSY			(1UL << 0)
#define XDMA_STAT_DESC_STOPPED		(1UL << 1)
#define XDMA_STAT_DESC_COMPLETED	(1UL << 2)
#define XDMA_STAT_ALIGN_MISMATCH	(1UL << 3)
#define XDMA_STAT_MAGIC_STOPPED		(1UL << 4)
#define XDMA_STAT_FETCH_STOPPED		(1UL << 5)
#define XDMA_STAT_IDLE_STOPPED		(1UL << 6)
#define XDMA_STAT_READ_ERROR		(0x1FUL << 9)
#define XDMA_STAT_DESC_ERROR		(0x1FUL << 19)

/* bits of the SGDMA descriptor control field */
#define XDMA_DESC_STOPPED	(1UL << 0)
#define XDMA_DESC_COMPLETED	(1UL << 1)
#define XDMA_DESC_EOP		(1UL << 4)

#define MAGIC_ENGINE	0xEEEEEEEEUL
#define MAGIC_DEVICE	0xDDDDDDDDUL
#define MAGIC_CHAR	0xCCCCCCCCUL
#define MAGIC_BITSTREAM 0xBBBBBBBBUL

/* upper 16-bits of engine identifier register */
#define XDMA_ID_H2C 0x1fc0U
#define XDMA_ID_C2H 0x1fc1U


#define DESC_MAGIC 0xAD4B0000UL


#define MAX_NUM_ENGINES (XDMA_CHANNEL_NUM_MAX * 2)
#define H2C_CHANNEL_OFFSET 0x1000
#define SGDMA_OFFSET_FROM_CHANNEL 0x4000
#define CHANNEL_SPACING 0x100
#define TARGET_SPACING 0x1000

/* obtain the 32 most significant (high) bits of a 32-bit or 64-bit address */
#define PCI_DMA_H(addr) ((addr >> 16) >> 16)
/* obtain the 32 least significant (low) bits of a 32-bit or 64-bit address */
#define PCI_DMA_L(addr) (addr & 0xffffffffUL)

/*  00 H2C Channel Registers
*  01 C2H Channel Registers */
struct engine_regs {
	u32 identifier;
	u32 control;
	u32 control_w1s;
	u32 control_w1c;
	u32 reserved_1[12];	/* padding */

	u32 status;
	u32 status_rc;
	u32 completed_desc_count;
	u32 alignments;
	u32 reserved_2[14];	/* padding */

	u32 poll_mode_wb_lo;
	u32 poll_mode_wb_hi;
	u32 interrupt_enable_mask;
	u32 interrupt_enable_mask_w1s;
	u32 interrupt_enable_mask_w1c;
	u32 reserved_3[9];	/* padding */

	u32 perf_ctrl;
	u32 perf_cyc_lo;
	u32 perf_cyc_hi;
	u32 perf_dat_lo;
	u32 perf_dat_hi;
	u32 perf_pnd_lo;
	u32 perf_pnd_hi;
} __packed;
struct engine_regs *engine_regs_h2c;
struct engine_regs *engine_regs_c2h;

struct engine_sgdma_regs {
	u32 identifier;
	u32 reserved_1[31];	/* padding */

						/* bus address to first descriptor in Root Complex Memory */
	u32 first_desc_lo;
	u32 first_desc_hi;
	/* number of adjacent descriptors at first_desc */
	u32 first_desc_adjacent;
	u32 credits;
} __packed;
struct engine_sgdma_regs *engine_sgdma_regs;

struct msix_vec_table_entry {
	u32 msi_vec_addr_lo;
	u32 msi_vec_addr_hi;
	u32 msi_vec_data_lo;
	u32 msi_vec_data_hi;
} __packed;
struct msix_vec_table_entry *msix_vec_table_entry;


struct msix_vec_table {
	struct msix_vec_table_entry entry_list[32];
} __packed;
struct msix_vec_table *msix_vec_table;

/* Performance counter for AXI Streaming */
struct performance_regs {
	u32 identifier;
	u32 control;
	u32 status;
	u32 period_low;		/* period in 8 ns units (low 32-bit) */
	u32 period_high;	/* period (high 32-bit) */
	u32 performance_low;	/* perf count in 8-byte units (low 32-bit) */
	u32 performance_high;	/* perf count (high 32-bit) */
	u32 wait_low;		/* wait count in 8-byte units (low 32-bit) */
	u32 wait_high;		/* wait (high 32-bit) */
} __packed;
struct performance_regs  *performance_regs;

struct interrupt_regs {
	u32 identifier;
	u32 user_int_enable;
	u32 user_int_enable_w1s;
	u32 user_int_enable_w1c;
	u32 channel_int_enable;
	u32 channel_int_enable_w1s;
	u32 channel_int_enable_w1c;
	u32 reserved_1[9];	/* padding */

	u32 user_int_request;
	u32 channel_int_request;
	u32 user_int_pending;
	u32 channel_int_pending;
	u32 reserved_2[12];	/* padding */

	u32 user_msi_vector[8];
	u32 channel_msi_vector[8];
} __packed;
struct interrupt_regs *interrupt_regs;

#if 0
struct sgdma_common_regs {
	u32 padding[9];
	u32 credit_feature_enable;
} __packed;
#else
struct sgdma_common_regs {
	u32 identifier;
	u32 padding1[3];
	u32 control;
	u32 padding2[4];
	u32 credit_feature_enable;
} __packed;
#endif
struct sgdma_common_regs *sgdma_common_regs;

/**
* Descriptor for a single contiguous memory block transfer.
*
* Multiple descriptors are linked by means of the next pointer. An additional
* extra adjacent number gives the amount of extra contiguous descriptors.
*
* The descriptors are in root complex memory, and the bytes in the 32-bit
* words must be in little-endian byte ordering.
*/
struct xdma_desc {
	u32 control;
	u32 bytes;		/* transfer length in bytes */
	u32 src_addr_lo;	/* source address (low 32-bit) */
	u32 src_addr_hi;	/* source address (high 32-bit) */
	u32 dst_addr_lo;	/* destination address (low 32-bit) */
	u32 dst_addr_hi;	/* destination address (high 32-bit) */
						/*
						* next descriptor in the single-linked list of descriptors;
						* this is the PCIe (bus) address of the next descriptor in the
						* root complex memory
						*/
	u32 next_lo;		/* next desc address (low 32-bit) */
	u32 next_hi;		/* next desc address (high 32-bit) */
} __packed;

/* 32 bytes (four 32-bit words) or 64 bytes (eight 32-bit words) */
struct xdma_result {
	u32 status;
	u32 length;
	u32 reserved_1[6];	/* padding */
} __packed;

/* Structure for polled mode descriptor writeback */
struct xdma_poll_wb {
	u32 completed_desc_count;
	u32 reserved_1[7];
} __packed;
