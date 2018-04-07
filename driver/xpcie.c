#include "xpcie.h"
#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm-generic/pci-dma-compat.h>
#include <asm-generic/iomap.h>



#include <linux/cdev.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <asm/byteorder.h>
#include <asm/cacheflush.h>
#include <asm/delay.h>
#include <asm/pci.h>




static struct class *xpcie_class;	/* sys filesystem */
static struct device *xpcie_class_user;	/* sys filesystem */
static struct device *xpcie_class_h2c;	/* sys filesystem */
static struct device *xpcie_class_c2h;	/* sys filesystem */
static int major_user;
static int major_h2c;
static int major_c2h;
static int user_minor = 1;
static int h2c_minor = 2;
static int c2h_minor = 3;
static struct pci_dev *xpcie_dev;
static int irq_line;		/* flag if irq allocated successfully */
static struct xdma_irq h2c_irq;
static const struct pci_device_id pci_ids[] = {
	{ PCI_DEVICE(0x10ee, 0x7024), },
	{ PCI_DEVICE(0x10ee, 0x7038), },
	{ 0, }
};


/**
* Driver module exercising scatterlist interfaces
*
* (C) Copyright 2008-2014 Leon Woestenberg  <leon@sidebranch.com>
*
*/

//#define DEBUG


/*
* sg_create_mapper() - Create a mapper for virtual memory to scatterlist.
*
* @max_len: Maximum number of bytes that can be mapped at a time.
*
* Allocates a book keeping structure, array to page pointers and a scatter
* list to map virtual user memory into.
*
*/
struct sg_mapping_t *sg_create_mapper(unsigned long max_len)
{
	struct sg_mapping_t *sgm;
	if (max_len == 0)
		return NULL;

	/* 分配自定义的sgm */
	sgm = kcalloc(1, sizeof(struct sg_mapping_t), GFP_KERNEL);
	if (sgm == NULL)
		return NULL;
	/* 计算页数 */
	sgm->max_pages = max_len / PAGE_SIZE + 2;

	/* 根据最大页数，分配page数组 */
	sgm->pages = kcalloc(sgm->max_pages, sizeof(*sgm->pages), GFP_KERNEL);
	if (sgm->pages == NULL) {
		kfree(sgm);
		return NULL;
	}
	pr_debug("Allocated %lu bytes for page pointer array for %d pages @0x%p.\n",
		sgm->max_pages * sizeof(*sgm->pages), sgm->max_pages, sgm->pages);


	/* 根据最大页数，分配scatterlist */
	sgm->sgl = kcalloc(sgm->max_pages, sizeof(struct scatterlist), GFP_KERNEL);
	if (sgm->sgl == NULL) {
		kfree(sgm->pages);
		kfree(sgm);
		return NULL;
	}
	pr_debug("Allocated %lu bytes for scatterlist for %d pages @0x%p.\n",
		sgm->max_pages * sizeof(struct scatterlist), sgm->max_pages, sgm->sgl);

	/* 根据最大页数，初始化scatterlist */
	sg_init_table(sgm->sgl, sgm->max_pages);

	pr_debug("sg_mapping_t *sgm=0x%p\n", sgm);
	pr_debug("sgm->pages=0x%p\n", sgm->pages);
	return sgm;
};

/*
* sg_destroy_mapper() - Destroy a mapper for virtual memory to scatterlist.
*
* @sgm scattergather mapper handle.
*/
void sg_destroy_mapper(struct sg_mapping_t *sgm)
{
	/* user failed to call sgm_unmap_user_pages() */
	BUG_ON(sgm->mapped_pages > 0);
	/* free scatterlist */
	kfree(sgm->sgl);
	/* free page array */
	kfree(sgm->pages);
	/* free mapper handle */
	kfree(sgm);
	pr_debug("Freed page pointer and scatterlist.\n");
};

/*
* sgm_map_user_pages() - Get user pages and build a scatterlist.
*
* @sgm scattergather mapper handle.
* @start User space buffer (virtual memory) address.
* @count Number of bytes in buffer.
* @to_user !0 if data direction is from device to user space.
*
* Returns Number of entries in the table on success, -1 on error.
*/
int sgm_get_user_pages(struct sg_mapping_t *sgm, const char *start, size_t count, int dir_to_dev)
{
	int rc, i;
	int nr_pages;
	struct scatterlist *sgl = NULL;
	struct page **pages = NULL;
	

	/* calculate page frame number @todo use macro's */
	const unsigned long first = ((unsigned long)start & PAGE_MASK) >> PAGE_SHIFT;
	const unsigned long last = (((unsigned long)start + count - 1) & PAGE_MASK) >> PAGE_SHIFT;

	/* the number of pages we want to map in 因为用户空间虚拟地址是线性的，所以能算出页数 */
	nr_pages = last - first + 1;
	sgl = sgm->sgl;
	/* pointer to array of page pointers */
	pages = sgm->pages;


	pr_debug("sgm_map_user_pages() first=%016lX, last=%016lX, nr_pages=%d\n", first, last, nr_pages);
	pr_debug("sgl = 0x%p.\n", sgl);

#if 0
	if (start + count < start)
		return -EINVAL;
	if (nr_pages > sgm->max_pages)
		return -EINVAL;
	if (count == 0)
		return 0;
#endif

	/* 初始化和清零 */
	sg_init_table(sgl, nr_pages);//重复
	pr_debug("pages=0x%p\n", pages);
	pr_debug("start = 0x%llx.\n",
		(unsigned long long)start);
	pr_debug("first = %lu, last = %lu\n", first, last);
	for (i = 0; i < nr_pages - 1; i++) {
		pages[i] = NULL;
	}

	/* try to fault in all of the necessary pages */
#if 0
	down_read(&current->mm->mmap_sem);
	/* to_user != 0 means read from device, write into user space buffer memory */
	rc = get_user_pages(current, current->mm, (unsigned long)start, nr_pages, to_user,
		0 /* don't force */, pages, NULL);
	pr_debug("get_user_pages(%lu, nr_pages = %d) == %d.\n", (unsigned long)start, nr_pages, rc);
	up_read(&current->mm->mmap_sem);
#else
	rc = get_user_pages_fast((unsigned long)start, nr_pages, !dir_to_dev/*write*/, pages);
	pr_debug("get_user_pages_fast(%lu, nr_pages = %d) == %d.\n", (unsigned long)start, nr_pages, rc);
#endif

	for (i = 0; i < nr_pages - 1; i++) {
		pr_debug("%04d: page=0x%p\n", i, pages[i]);
	}
	/* errors and no page mapped should return here */
	if (rc < nr_pages) {
		if (rc > 0) sgm->mapped_pages = rc;
		pr_debug("Could not get_user_pages(), %d.\n", rc);
		goto out_unmap;
	}
	/* XXX */
	BUG_ON(rc != nr_pages);
	sgm->mapped_pages = rc;
	pr_debug("sgm->mapped_pages = %d\n", sgm->mapped_pages);

	for (i = 0; i < nr_pages; i++) {
		flush_dcache_page(pages[i]);
	}

	/* populate the scatter/gather list */
	pr_debug("%04d: page=0x%p\n", 0, (void *)pages[0]);

	sg_set_page(&sgl[0], pages[0], 0 /*length*/, offset_in_page(start));
	pr_debug("sg_page(&sgl[0]) = 0x%p (pfn = %lu).\n", sg_page(&sgl[0]),
		page_to_pfn(sg_page(&sgl[0])));

	/* verify if the page start address got into the first sg entry */
	pr_debug("sg_dma_address(&sgl[0])=0x%016llx.\n", (u64)sg_dma_address(&sgl[0]));
	pr_debug("sg_dma_len(&sgl[0])=0x%08x.\n", sg_dma_len(&sgl[0]));

	/* multiple pages? */
	if (nr_pages > 1) {
		/* offset was already set above */
		sgl[0].length = PAGE_SIZE - sgl[0].offset;
		pr_debug("%04d: page=0x%p, pfn=%lu, offset=%u, length=%u (FIRST)\n", 0,
			(void *)sg_page(&sgl[0]), (unsigned long)page_to_pfn(sg_page(&sgl[0])), sgl[0].offset, sgl[0].length);
		count -= sgl[0].length;
		/* iterate over further pages, except the last page */
		for (i = 1; i < nr_pages - 1; i++) {
			BUG_ON(count < PAGE_SIZE);
			/* set the scatter gather entry i */
			sg_set_page(&sgl[i], pages[i], PAGE_SIZE, 0/*offset*/);
			count -= PAGE_SIZE;
			pr_debug("%04d: page=0x%p, pfn=%lu, offset=%u, length=%u\n", i,
				(void *)sg_page(&sgl[i]), (unsigned long)page_to_pfn(sg_page(&sgl[i])), sgl[i].offset, sgl[i].length);
		}
		/* last page */
		BUG_ON(count > PAGE_SIZE);
		/* set count bytes at offset 0 in the page */
		sg_set_page(&sgl[i], pages[i], count, 0);
		pr_debug("%04d: page=0x%p, pfn=%lu, offset=%u, length=%u (LAST)\n", i,
			(void *)sg_page(&sgl[i]), (unsigned long)page_to_pfn(sg_page(&sgl[i])), sgl[i].offset, sgl[i].length);
	}
	/* single page */
	else {
		/* limit the count */
		sgl[0].length = count;
		pr_debug("%04d: page=0x%p, pfn=%lu, offset=%u, length=%u (SINGLE/FIRST/LAST)\n", 0,
			(void *)sg_page(&sgl[i]), (unsigned long)page_to_pfn(sg_page(&sgl[0])), sgl[0].offset, sgl[0].length);
	}
	return nr_pages;

out_unmap:
	/* { rc < 0 means errors, >= 0 means not all pages could be mapped } */
	/* did we map any pages? */
	for (i = 0; i < sgm->mapped_pages; i++)
		put_page(pages[i]);
	rc = -ENOMEM;
	sgm->mapped_pages = 0;
	return rc;
}

/*
* sgm_unmap_user_pages() - Mark the pages dirty and release them.
*
* Pages mapped earlier with sgm_map_user_pages() are released here.
* after being marked dirtied by the DMA.
*
*/
int sgm_put_user_pages(struct sg_mapping_t *sgm, int dir_to_dev)
{
	int i;
	/* mark page dirty */
	if (!dir_to_dev)
		sgm_dirty_pages(sgm);
	/* put (i.e. release) pages */
	for (i = 0; i < sgm->mapped_pages; i++) {
		put_page(sgm->pages[i]);
	}
	/* remember we have zero pages */
	sgm->mapped_pages = 0;
	return 0;
}

void sgm_dirty_pages(struct sg_mapping_t *sgm)
{
	int i;
	/* iterate over all mapped pages */
	for (i = 0; i < sgm->mapped_pages; i++) {
		/* mark page dirty */
		SetPageDirty(sgm->pages[i]);
	}
}

/* sgm_kernel_pages() -- create a sgm map from a vmalloc()ed memory */
int sgm_kernel_pages(struct sg_mapping_t *sgm, const char *start, size_t count, int to_user)
{
	/* calculate page frame number @todo use macro's */
	const unsigned long first = ((unsigned long)start & PAGE_MASK) >> PAGE_SHIFT;
	const unsigned long last = (((unsigned long)start + count - 1) & PAGE_MASK) >> PAGE_SHIFT;

	/* the number of pages we want to map in */
	const int nr_pages = last - first + 1;
	int rc, i;
	struct scatterlist *sgl = sgm->sgl;
	/* pointer to array of page pointers */
	struct page **pages = sgm->pages;
	unsigned char *virt = (unsigned char *)start;

	pr_debug("sgm_kernel_pages()\n");
	pr_debug("sgl = 0x%p.\n", sgl);

	/* no pages should currently be mapped */
	BUG_ON(sgm->mapped_pages > 0);
	if (start + count < start)
		return -EINVAL;
	if (nr_pages > sgm->max_pages)
		return -EINVAL;
	if (count == 0)
		return 0;
	/* initialize scatter gather list */
	sg_init_table(sgl, nr_pages);

	pr_debug("pages=0x%p\n", pages);
	pr_debug("start = 0x%llx.\n",
		(unsigned long long)start);
	pr_debug("first = %lu, last = %lu\n", first, last);

	/* get pages belonging to vmalloc()ed space */
	for (i = 0; i < nr_pages; i++, virt += PAGE_SIZE) {
		pages[i] = vmalloc_to_page(virt);
		if (pages[i] == NULL)
			goto err;
		/* make sure page was allocated using vmalloc_32() */
		BUG_ON(PageHighMem(pages[i]));
	}
	for (i = 0; i < nr_pages; i++) {
		pr_debug("%04d: page=0x%p\n", i, pages[i]);
	}
	sgm->mapped_pages = nr_pages;
	pr_debug("sgm->mapped_pages = %d\n", sgm->mapped_pages);

	for (i = 0; i < nr_pages; i++) {
		flush_dcache_page(pages[i]);
	}

	/* populate the scatter/gather list */
	pr_debug("%04d: page=0x%p\n", 0, (void *)pages[0]);

	/* set first page */
	sg_set_page(&sgl[0], pages[0], 0 /*length*/, offset_in_page(start));
	pr_debug("sg_page(&sgl[0]) = 0x%p (pfn = %lu).\n", sg_page(&sgl[0]),
		page_to_pfn(sg_page(&sgl[0])));

	/* verify if the page start address got into the first sg entry */
	pr_debug("sg_dma_address(&sgl[0])=0x%016llx.\n", (u64)sg_dma_address(&sgl[0]));
	pr_debug("sg_dma_len(&sgl[0])=0x%08x.\n", sg_dma_len(&sgl[0]));

	/* multiple pages? */
	if (nr_pages > 1) {
		/* { sgl[0].offset is already set } */
		sgl[0].length = PAGE_SIZE - sgl[0].offset;
		pr_debug("%04d: page=0x%p, pfn=%lu, offset=%u length=%u (F)\n", 0,
			(void *)sg_page(&sgl[0]), (unsigned long)page_to_pfn(sg_page(&sgl[0])), sgl[0].offset, sgl[0].length);
		count -= sgl[0].length;
		/* iterate over further pages, except the last page */
		for (i = 1; i < nr_pages - 1; i++) {
			BUG_ON(count < PAGE_SIZE);
			/* set the scatter gather entry i */
			sg_set_page(&sgl[i], pages[i], PAGE_SIZE, 0/*offset*/);
			count -= PAGE_SIZE;
			pr_debug("%04d: page=0x%p, pfn=%lu, offset=%u length=%u\n", i,
				(void *)sg_page(&sgl[i]), (unsigned long)page_to_pfn(sg_page(&sgl[i])), sgl[i].offset, sgl[i].length);
		}
		/* last page */
		BUG_ON(count > PAGE_SIZE);
		/* 'count' bytes remaining at offset 0 in the page */
		sg_set_page(&sgl[i], pages[i], count, 0);
		pr_debug("%04d: pfn=%lu, offset=%u length=%u (L)\n", i,
			(unsigned long)page_to_pfn(sg_page(&sgl[i])), sgl[i].offset, sgl[i].length);
	}
	/* single page */
	else {
		/* limit the count */
		sgl[0].length = count;
		pr_debug("%04d: pfn=%lu, offset=%u length=%u (F)\n", 0,
			(unsigned long)page_to_pfn(sg_page(&sgl[0])), sgl[0].offset, sgl[0].length);
	}
	return nr_pages;

err:
	rc = -ENOMEM;
	sgm->mapped_pages = 0;
	return rc;
}


static inline void write_register(u32 value, void *iomem)
{
	iowrite32(value, iomem);
}

static inline u32 read_register(void *iomem)
{
	return ioread32(iomem);
}
static inline u32 build_u32(u32 hi, u32 lo)
{
	return ((hi & 0xFFFFUL) << 16) | (lo & 0xFFFFUL);
}
static inline u64 build_u64(u64 hi, u64 lo)
{
	return ((hi & 0xFFFFFFFULL) << 32) | (lo & 0xFFFFFFFFULL);
}

#define XDMA_BAR_NUM 2
static void *__iomem bar[XDMA_BAR_NUM];	/* addresses for mapped BARs */
const static int config_bar_idx = 1;

/* channel_interrupts_enable -- Enable interrupts we are interested in */
static void channel_interrupts_enable(u32 mask)
{
	struct interrupt_regs *reg = (struct interrupt_regs *)
		(bar[config_bar_idx] + XDMA_OFS_INT_CTRL);

	write_register(mask, &reg->channel_int_enable_w1s);
}

/* channel_interrupts_enable -- Enable interrupts we are interested in */
static void h2c_interrupts_enable(void)
{
	u32 reg_value=0;
	
	reg_value = XDMA_CTRL_IE_DESC_ALIGN_MISMATCH;
	reg_value |= XDMA_CTRL_IE_MAGIC_STOPPED;
	reg_value |= XDMA_CTRL_IE_MAGIC_STOPPED;
	reg_value |= XDMA_CTRL_IE_READ_ERROR;
	reg_value |= XDMA_CTRL_IE_DESC_ERROR;

	reg_value |= XDMA_CTRL_IE_DESC_STOPPED;
	reg_value |= XDMA_CTRL_IE_DESC_COMPLETED;
	
	write_register(reg_value, &engine_regs_h2c->interrupt_enable_mask);
	/* dummy read of status register to flush all previous writes */
	read_register(&engine_regs_h2c->interrupt_enable_mask);
}
static void h2c_interrupts_disable(void)
{
	u32 reg_value = 0;

	write_register(reg_value, &engine_regs_h2c->interrupt_enable_mask);
	/* dummy read of status register to flush all previous writes */
	read_register(&engine_regs_h2c->interrupt_enable_mask);
}
/* channel_interrupts_disable -- Disable interrupts we not interested in */
static void channel_interrupts_disable(u32 mask)
{
	struct interrupt_regs *reg = (struct interrupt_regs *)
		(bar[config_bar_idx] + XDMA_OFS_INT_CTRL);

	write_register(mask, &reg->channel_int_enable_w1c);
}

static int map_single_bar(struct pci_dev *dev, int idx)
{
	resource_size_t bar_start;
	resource_size_t bar_len;
	resource_size_t map_len;

	bar_start = pci_resource_start(dev, idx);
	bar_len = pci_resource_len(dev, idx);
	map_len = bar_len;



	/* do not map BARs with length 0. Note that start MAY be 0! */
	if (!bar_len) {
		dbg_init("BAR #%d is not present - skipping\n", idx);
		return 0;
	}

	/* BAR size exceeds maximum desired mapping? */
	if (bar_len > INT_MAX) {
		dbg_init("Limit BAR %d mapping from %llu to %d bytes\n", idx,
			(u64)bar_len, INT_MAX);
		map_len = (resource_size_t)INT_MAX;
	}
	/*
	* map the full device memory or IO region into kernel virtual
	* address space
	*/
	dbg_init("BAR%d: %llu bytes to be mapped.\n", idx, (u64)map_len);
	bar[idx] = pci_iomap(dev, idx, map_len);

	if (!bar[idx]) {
		dbg_init("Could not map BAR %d", idx);
		return -1;
	}

	dbg_init("BAR%d at 0x%llx mapped at 0x%p, length=%llu(/%llu)\n", idx,
		(u64)bar_start, bar[idx], (u64)map_len, (u64)bar_len);

	return (int)map_len;
}
static void unmap_bars(struct pci_dev *dev)
{
	int i;

	for (i = 0; i < XDMA_BAR_NUM; i++) {
		/* is this BAR mapped? */
		if (bar[i]) {
			/* unmap BAR */
			pci_iounmap(dev, bar[i]);
			/* mark as unmapped */
			bar[i] = NULL;
		}
	}
}



static int request_regions(struct pci_dev *pdev)
{
	int rc;


	dbg_init("pci_request_regions()\n");
	rc = pci_request_regions(pdev, DRV_NAME);
	/* could not request all regions? */
	if (rc) {
		dbg_init("pci_request_regions() = %d, device in use?\n", rc);
		/* assume device is in use so do not disable it later */
		//lro->regions_in_use = 1;
	}
	else {
		//lro->got_regions = 1;
	}

	return rc;
}

static void engine_start(void)
{
	u32 w;

	/* write control register of SG DMA engine */
	w = (u32)XDMA_CTRL_RUN_STOP;
	w |= (u32)XDMA_CTRL_IE_READ_ERROR;
	w |= (u32)XDMA_CTRL_IE_DESC_ERROR;
	w |= (u32)XDMA_CTRL_IE_DESC_ALIGN_MISMATCH;
	w |= (u32)XDMA_CTRL_IE_MAGIC_STOPPED;

 	w |= (u32)XDMA_CTRL_IE_DESC_STOPPED;
// 	w |= (u32)XDMA_CTRL_IE_DESC_COMPLETED;


	/* start the engine */
	write_register(w, &engine_regs_h2c->control);

	/* dummy read of status register to flush all previous writes */
	w = read_register(&engine_regs_h2c->status);
}

static void engine_stop(void)
{
	u32 w;

	/* stop the engine */
	write_register(0, &engine_regs_h2c->control);

	/* dummy read of status register to flush all previous writes */
	w = read_register(&engine_regs_h2c->status);
}
static int test_h2c(struct pci_dev *pdev)
{
	u32 value;
	struct xdma_desc *desc_virt;
	dma_addr_t desc_bus;//总线地址
	size_t desc_size;
	char* data_virt;
	size_t data_size = 128;
	dma_addr_t data_bus;
	u64 ep_addr = 0;
	u32 temp_control = 0;
	int i;

	BUG_ON(!pdev);


	/* 分配desc */
	desc_size = sizeof(struct xdma_desc);
	desc_virt = pci_alloc_consistent(pdev, desc_size, &desc_bus);
	if (!desc_virt)
	{
		dbg_init("pci_alloc_consistent failed \n");
		goto out1;
	}
	memset(desc_virt, 0, desc_size);
	dbg_init("pci_alloc_consistent OK, phy=%p, bus=%016llX \n", desc_virt, desc_bus);

	/* 分配数据换缓冲区，伪造数据 */
	data_virt = pci_alloc_consistent(pdev, data_size, &data_bus);
	if (!data_virt)
	{
		dbg_init("pci_alloc_consistent failed \n");
		goto out2;
	}
	for (i = 0; i < data_size; i++)
	{
		data_virt[i] = i;
	}

	/* 填充desc */
	temp_control = DESC_MAGIC | (0x13);
	desc_virt->bytes = data_size;
	desc_virt->control = cpu_to_le32(temp_control);
	desc_virt->dst_addr_lo = cpu_to_le32(PCI_DMA_L(ep_addr));
	desc_virt->dst_addr_hi = cpu_to_le32(PCI_DMA_H(ep_addr));
	desc_virt->next_hi = 0;
	desc_virt->next_lo = 0;
	desc_virt->src_addr_lo = cpu_to_le32(PCI_DMA_L(data_bus));
	desc_virt->src_addr_hi = cpu_to_le32(PCI_DMA_H(data_bus));


	/* 设置sgdma_regs */
	write_register(cpu_to_le32(PCI_DMA_L(desc_bus)), &engine_sgdma_regs->first_desc_lo);
	write_register(cpu_to_le32(PCI_DMA_H(desc_bus)), &engine_sgdma_regs->first_desc_hi);
	write_register(cpu_to_le32(0), &engine_sgdma_regs->first_desc_adjacent);

	/* 启动引擎 */
	engine_start();

	/* 等待传输完 */
	mdelay(1000);
	value = read_register(&engine_regs_h2c->status);
	dbg_init("engine_regs_h2c->status =  0x%08X\n", value);

	


	/* 停止引擎 */
	engine_stop();

	pci_free_consistent(pdev, data_size, data_virt, data_bus);
out2:
	pci_free_consistent(pdev, desc_size, desc_virt, desc_bus);
out1:
	return 0;
}
static int transfer_start(struct pci_dev *pdev, 
							struct xdma_transfer* transfer,
							struct xdma_irq* h2c_irq
	)
{
	u32 value;
	int extra_adj = 0;
	int rc = 0;
	unsigned long flags;
	u32 irq_mask;

	
	BUG_ON(!pdev);
	BUG_ON(!transfer);
	BUG_ON(!h2c_irq);

	value = read_register(&engine_regs_h2c->status);
	dbg_init("before transfer, engine_regs_h2c->status =  0x%08X\n", value);

	/* 设置extra_adj */
	if (transfer->desc_adjacent > 0) {
		extra_adj = transfer->desc_adjacent - 1;
		if (extra_adj > MAX_EXTRA_ADJ)
			extra_adj = MAX_EXTRA_ADJ;
	}
	dbg_init("iowrite32(0x%08x to engine_sgdma_regs->first_desc_adjacent ) (first_desc_adjacent)\n", extra_adj);
	write_register(cpu_to_le32(extra_adj), &engine_sgdma_regs->first_desc_adjacent);
	dbg_init("oops function=%s, line=%d\n", __FUNCTION__, __LINE__);
	/* 设置sgdma_regs */
	dbg_init("transfer->desc_bus =  0x%016llX\n", transfer->desc_bus);
	dbg_init("transfer->desc_adjacent =  0x%08X\n", transfer->desc_adjacent);
	write_register(cpu_to_le32(PCI_DMA_L(transfer->desc_bus)), &engine_sgdma_regs->first_desc_lo);
	write_register(cpu_to_le32(PCI_DMA_H(transfer->desc_bus)), &engine_sgdma_regs->first_desc_hi);
	dbg_init("oops function=%s, line=%d\n", __FUNCTION__, __LINE__);
	/* 启动引擎 */
	engine_start();
	dbg_init("oops function=%s, line=%d\n", __FUNCTION__, __LINE__);

	//模拟中断触发
// 	mdelay(2000);
// 	h2c_irq->events_irq = 1;

	/* 
	 *	W.1 sleep until any interrupt events have occurred, or a signal arrived	
	 *  正常返回0， 被打断返回 -ERESTARTSYS
	 */
	rc = wait_event_interruptible(h2c_irq->events_wq,
		h2c_irq->events_irq != 0);
	if (rc)
	dbg_init("oops function=%s, line=%d\n", __FUNCTION__, __LINE__);	dbg_sg("wait_event_interruptible=%d\n", rc);
	dbg_init("oops function=%s, line=%d\n", __FUNCTION__, __LINE__);
	/* W.2 清除标志 */
	spin_lock_irqsave(&h2c_irq->events_lock, flags);
	h2c_irq->events_irq = 0;
	spin_unlock_irqrestore(&h2c_irq->events_lock, flags);
	dbg_init("oops function=%s, line=%d\n", __FUNCTION__, __LINE__);
	/* W.3 开中断 */
	irq_mask = 0x01 ;
	channel_interrupts_enable(irq_mask);


	/* 等待传输完成后 */
#if 0
	value = read_register(&engine_regs_h2c->status);
	dbg_init("engine_regs_h2c->status =  0x%08X\n", value);
#endif


	/* 停止引擎 */
	engine_stop();

	return rc;
}

static int map_bars(struct pci_dev *pdev)
{
	const int USER_BAR_INDEX = 0;
	const int DMA_BAR_INDEX = 1;
	map_single_bar(pdev, USER_BAR_INDEX);
	map_single_bar(pdev, DMA_BAR_INDEX);

	/* 初始化操作寄存器组的结构体指针 */
	engine_regs_h2c = (struct engine_regs *) (bar[DMA_BAR_INDEX] + 0 * TARGET_SPACING);
	engine_regs_c2h = (struct engine_regs *) (bar[DMA_BAR_INDEX] + 1 * TARGET_SPACING);
	engine_sgdma_regs = (struct engine_sgdma_regs *) (bar[DMA_BAR_INDEX] + 4 * TARGET_SPACING);
	sgdma_common_regs = (struct sgdma_common_regs *) (bar[DMA_BAR_INDEX] + 6 * TARGET_SPACING);

	return 0;
}

static int set_dma_mask(struct pci_dev *pdev)
{
	int rc = 0;

	BUG_ON(!pdev);

	dbg_init("sizeof(dma_addr_t) == %ld\n", sizeof(dma_addr_t));
	/* 64-bit addressing capability for XDMA? */
	if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(64))) {
		/* query for DMA transfer */
		/* @see Documentation/DMA-mapping.txt */
		dbg_init("pci_set_dma_mask()\n");
		/* use 64-bit DMA */
		dbg_init("Using a 64-bit DMA mask.\n");
		/* use 32-bit DMA for descriptors */
		pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		/* use 64-bit DMA, 32-bit for consistent */
	}
	else if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(32))) {
		dbg_init("Could not set 64-bit DMA mask.\n");
		pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		/* use 32-bit DMA */
		dbg_init("Using a 32-bit DMA mask.\n");
	}
	else {
		dbg_init("No suitable DMA possible.\n");
		rc = -1;
	}

	return rc;
}

static int probe_scan_for_msi(struct pci_dev *pdev)
{
	int rc = 0;

	BUG_ON(!pdev);
	
	if (pci_find_capability(pdev, PCI_CAP_ID_MSI)) {
		/* enable message signalled interrupts */
		dbg_init("pci_enable_msi()\n");
		rc = pci_enable_msi(pdev);
		if (rc < 0)
			dbg_init("Couldn't enable MSI mode: rc = %d\n", rc);
	}
	else {
		dbg_init("MSI/MSI-X not detected - using legacy interrupts\n");
	}

	return rc;
}
/* 唤醒进程，仿照user_irq_service 函数*/
static void h2c_irq_service(struct xdma_irq *h2c_irq)
{
	unsigned long flags;

	BUG_ON(!h2c_irq);
	spin_lock_irqsave(&(h2c_irq->events_lock), flags);
	if (!h2c_irq->events_irq) {
		h2c_irq->events_irq = 1;
		wake_up_interruptible(&(h2c_irq->events_wq));
	}
	spin_unlock_irqrestore(&(h2c_irq->events_lock), flags);
}
/*
* xdma_isr() - Interrupt handler
*
* @dev_id pointer to xdma_dev
*/
static irqreturn_t xdma_isr(int irq, void *dev_id)
{
	u32 ch_irq;
	struct xdma_dev *lro;
	struct interrupt_regs *irq_regs;

	dbg_irq("(irq=%d) <<<< INTERRUPT SERVICE ROUTINE\n", irq);
	//BUG_ON(!dev_id);
	
	lro = (struct xdma_dev *)dev_id;


	irq_regs = (struct interrupt_regs *)(bar[config_bar_idx] + XDMA_OFS_INT_CTRL);

	/* read channel interrupt requests 
	 * This register (channel_int_request) ,
	 * reflects the interrupt source AND with the enable mask register
	 */
	ch_irq = read_register(&irq_regs->channel_int_request);
	dbg_irq("ch_irq = 0x%08x\n", ch_irq);

	/*
	* disable all interrupts that fired; these are re-enabled individually
	* after the causing module has been fully serviced.
	*/
	channel_interrupts_disable(ch_irq);

	h2c_irq_service(&h2c_irq);

#if 0
	/* read user interrupts - this read also flushes the above write */
	user_irq = read_register(&irq_regs->user_int_request);
	dbg_irq("user_irq = 0x%08x\n", user_irq);

	for (user_irq_bit = 0; user_irq_bit < MAX_USER_IRQ; user_irq_bit++) {
		if (user_irq & (1 << user_irq_bit))
			user_irq_service(&lro->user_irq[user_irq_bit]);
	}


	/* iterate over H2C (PCIe read) */
	for (channel = 0; channel < XDMA_CHANNEL_NUM_MAX; channel++) {
		engine = lro->engine[channel][0];
		/* engine present and its interrupt fired? */
		if (engine && (engine->irq_bitmask & ch_irq)) {
			dbg_tfr("schedule_work(engine=%p)\n", engine);
			schedule_work(&engine->work);
		}
	}

	/* iterate over C2H (PCIe write) */
	for (channel = 0; channel < XDMA_CHANNEL_NUM_MAX; channel++) {
		engine = lro->engine[channel][1];
		/* engine present and its interrupt fired? */
		if (engine && (engine->irq_bitmask & ch_irq)) {
			dbg_tfr("schedule_work(engine=%p)\n", engine);
			schedule_work(&engine->work);
		}
	}
#endif
	return IRQ_HANDLED;
}
static int irq_setup(struct pci_dev *pdev)
{
	int rc = 0;
	u32 irq_flag;

	BUG_ON(!pdev);

	//irq_flag = lro->msi_enabled ? 0 : IRQF_SHARED;
	irq_flag = 0;
	irq_line = (int)pdev->irq;  /*中断号是自动分配的*/
	rc = request_irq(pdev->irq, xdma_isr, irq_flag, DRV_NAME, NULL);
	if (rc)
		dbg_init("Couldn't use IRQ#%d, rc=%d\n", pdev->irq, rc);
	else
		dbg_init("Using IRQ#%d \n", pdev->irq);
	
	return rc;
}
static void irq_teardown(void)
{

	if (irq_line != -1) {
		dbg_init("Releasing IRQ#%d\n", irq_line);
		free_irq(irq_line, NULL);
	}
}


/* read_interrupts -- Print the interrupt controller status */
static u32 read_interrupts(void)
{
	struct interrupt_regs *reg = (struct interrupt_regs *)
		(bar[config_bar_idx] + XDMA_OFS_INT_CTRL);
	u32 lo;
	u32 hi;

	/* extra debugging; inspect complete engine set of registers */
	hi = read_register(&reg->user_int_request);
	dbg_io("ioread32(0x%p) returned 0x%08x (user_int_request).\n",
		&reg->user_int_request, hi);
	lo = read_register(&reg->channel_int_request);
	dbg_io("ioread32(0x%p) returned 0x%08x (channel_int_request)\n",
		&reg->channel_int_request, lo);

	/* return interrupts: user in upper 16-bits, channel in lower 16-bits */
	return build_u32(hi, lo);
}
static void h2c_irq_init(struct xdma_irq* h2c_irq)
{
	spin_lock_init(&h2c_irq->events_lock);
	init_waitqueue_head(&h2c_irq->events_wq);
	return;
}
static int xpcie_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int rc;
	int irq_mask;
	xpcie_dev = pdev;

	rc = pci_enable_device(pdev);
	if (rc) {
		dbg_init("pci_enable_device() failed, rc = %d.\n", rc);
		goto err_enable;
	}
	/* enable relaxed ordering */
	//enable_pcie_relaxed_ordering(pdev);

	/* enable bus master capability */
	dbg_init("pci_set_master()\n");
	pci_set_master(pdev);

	/* enable MSI interrupt */
	probe_scan_for_msi(pdev);


	rc = request_regions(pdev);
	if (rc)
		goto err_regions;

	map_bars(pdev);

	set_dma_mask(pdev);
	//test_h2c(pdev);

	/* 初始化h2c_irq */
	h2c_irq_init(&h2c_irq);

	/* 申请中断资源 */
	irq_setup(pdev);

	/* 中断控制器使能h2c0 */
	irq_mask = 0x01;
	channel_interrupts_enable(irq_mask);

	/* h2c0 引擎开启各个中断类型 */
	h2c_interrupts_enable();


	/* Flush writes */
	read_interrupts();

err_regions:
err_enable:
	dbg_init("probe() returning %d\n", rc);
	return rc;
}



static void xpcie_remove(struct pci_dev *pdev)
{
	dbg_io(DRV_NAME "call remove ");

	h2c_interrupts_disable();
	channel_interrupts_disable(~0);
	read_interrupts();
	irq_teardown();

	unmap_bars(pdev);
	pci_release_regions(pdev);
	pci_disable_msi(pdev);
	pci_disable_device(pdev);
	return;
}
static struct pci_driver xpcie_driver = {
	.name = DRV_NAME,
	.id_table = pci_ids,
	.probe = xpcie_probe,
	.remove = xpcie_remove
};

static int xpcie_user_open(struct inode *inode, struct file *file)
{
	dbg_io(DRV_NAME "call open ");
	return 0;

}
//ssize_t (*read) (struct file *, char __user *, size_t, loff_t *);}
static ssize_t xpcie_user_read(struct file *filp, char __user *buff, size_t count, loff_t *offp)
{
	return 0;
}

static ssize_t xpcie_user_write(struct file *file, const char __user *buf, size_t count, loff_t * ppos)
{
	return 0;
}
static int xpcie_user_close(struct inode *inode, struct file *file)
{
	dbg_io(DRV_NAME "call close ");
	return 0;
}

static int xpcie_user_mmap(struct file *file, struct vm_area_struct *vma)
{
	int rc;
	unsigned long off;
	unsigned long phys;
	unsigned long vsize;
	unsigned long psize;
	int bar = 0;
	dbg_io(DRV_NAME "call mmap ");


	off = vma->vm_pgoff << PAGE_SHIFT;

	/* BAR physical address */
	phys = pci_resource_start(xpcie_dev, bar) + off;
	vsize = vma->vm_end - vma->vm_start;

	/* complete resource */
	psize = pci_resource_end(xpcie_dev, bar) -
		pci_resource_start(xpcie_dev, bar) + 1 - off;



	dbg_sg("mmap(): lro_char->bar = %d\n", bar);
	dbg_sg("off = 0x%lx\n", off);
	dbg_sg("start = 0x%llx\n",
		(unsigned long long)pci_resource_start(xpcie_dev,
			bar));
	dbg_sg("phys = 0x%lx\n", phys);

	if (vsize > psize)
		return -EINVAL;

	/*
	* pages must not be cached as this would result in cache line sized
	* accesses to the end point
	*/
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	/*
	* prevent touching the pages (byte access) for swap-in,
	* and prevent the pages from being swapped out
	*/
	vma->vm_flags |= VMEM_FLAGS;
	/* make MMIO accessible to user space */
	rc = io_remap_pfn_range(vma, vma->vm_start, phys >> PAGE_SHIFT,
		vsize, vma->vm_page_prot);

	dbg_sg("vma=0x%p, vma->vm_start=0x%lx, phys=0x%lx, size=%lu = %d\n",
		vma, vma->vm_start, phys >> PAGE_SHIFT, vsize, rc);
	if (rc)
		return -EAGAIN;


	return 0;
}


static struct file_operations xpcie_user_fops = {
	.owner = THIS_MODULE,
	.open = xpcie_user_open,
	.read = xpcie_user_read,
	.write = xpcie_user_write,
	.release = xpcie_user_close,
	.mmap = xpcie_user_mmap
}; 


static int  xpcie_h2c_open(struct inode *inode, struct file *file)
{
	dbg_io(DRV_NAME "call xpcie_h2c_open ");
	return 0;
}


static ssize_t  xpcie_h2c_read(struct file *filp, char __user *buff, size_t count, loff_t *offp)
{

	return count;
}
/* xdma_desc_alloc() - Allocate cache-coherent array of N descriptors.
*
* Allocates an array of 'number' descriptors in contiguous PCI bus addressable
* memory. Chains the descriptors as a singly-linked list; the descriptor's
* next * pointer specifies the bus address of the next descriptor.
*
*
* @dev Pointer to pci_dev
* @number Number of descriptors to be allocated
* @desc_bus_p Pointer where to store the first descriptor bus address
* @desc_last_p Pointer where to store the last descriptor virtual address,
* or NULL.
*
* @return Virtual address of the first descriptor
*
*/
static struct xdma_desc *xdma_desc_alloc(struct pci_dev *dev, int number,
	dma_addr_t *desc_bus_p, struct xdma_desc **desc_last_p)
{
	struct xdma_desc *desc_virt;	/* virtual address */
	dma_addr_t desc_bus;		/* bus address */
	int i;
	int adj = number - 1;
	int extra_adj;
	u32 temp_control;

	BUG_ON(number < 1);

	/* allocate a set of cache-coherent contiguous pages */
	desc_virt = (struct xdma_desc *)pci_alloc_consistent(dev,
		number * sizeof(struct xdma_desc), desc_bus_p);
	if (!desc_virt)
		return NULL;

	dbg_sg("after pci_alloc_consistent\n");

	/* get bus address of the first descriptor */
	desc_bus = *desc_bus_p;

	/* create singly-linked list for SG DMA controller */
	for (i = 0; i < number - 1; i++) {
		/* increment bus address to next in array */
		desc_bus += sizeof(struct xdma_desc);

		/* singly-linked list uses bus addresses */
		desc_virt[i].next_lo = cpu_to_le32(PCI_DMA_L(desc_bus));
		desc_virt[i].next_hi = cpu_to_le32(PCI_DMA_H(desc_bus));
		desc_virt[i].bytes = cpu_to_le32(0);

		/* any adjacent descriptors? */
		if (adj > 0) {
			extra_adj = adj - 1;
			if (extra_adj > MAX_EXTRA_ADJ)
				extra_adj = MAX_EXTRA_ADJ;

			adj--;
		}
		else {
			extra_adj = 0;
		}

		temp_control = DESC_MAGIC | (extra_adj << 8);


		desc_virt[i].control = cpu_to_le32(temp_control);
	}
	/* { i = number - 1 } */
	/* zero the last descriptor next pointer */
	desc_virt[i].next_lo = cpu_to_le32(0);
	desc_virt[i].next_hi = cpu_to_le32(0);
	desc_virt[i].bytes = cpu_to_le32(0);

	temp_control = DESC_MAGIC;


	desc_virt[i].control = cpu_to_le32(temp_control);

	/* caller wants a pointer to last descriptor? */
	if (desc_last_p)
		*desc_last_p = desc_virt + i;

	/* return the virtual address of the first descriptor */
	return desc_virt;
}

/* xdma_desc_free - Free cache-coherent linked list of N descriptors.
*
* @dev Pointer to pci_dev
* @number Number of descriptors to be allocated
* @desc_virt Pointer to (i.e. virtual address of) first descriptor in list
* @desc_bus Bus address of first descriptor in list
*/
static void xdma_desc_free(struct pci_dev *dev, int number,
	struct xdma_desc *desc_virt, dma_addr_t desc_bus)
{
	BUG_ON(!desc_virt);
	BUG_ON(number < 0);
	/* free contiguous list */
	pci_free_consistent(dev, number * sizeof(struct xdma_desc), desc_virt,
		desc_bus);
}
/* xdma_desc() - Fill a descriptor with the transfer details
*
* @desc pointer to descriptor to be filled
* @addr root complex address
* @ep_addr end point address
* @len number of bytes, must be a (non-negative) multiple of 4.
* @dir_to_dev If non-zero, source is root complex address and destination
* is the end point address. If zero, vice versa.
*
* Does not modify the next pointer
*/
static void xdma_desc_set(struct xdma_desc *desc, dma_addr_t rc_bus_addr,
	u64 ep_addr, int len, int dir_to_dev)
{


	/* transfer length */
	desc->bytes = cpu_to_le32(len);
	if (dir_to_dev) {
		/* read from root complex memory (source address) */
		desc->src_addr_lo = cpu_to_le32(PCI_DMA_L(rc_bus_addr));
		desc->src_addr_hi = cpu_to_le32(PCI_DMA_H(rc_bus_addr));
		/* write to end point address (destination address) */
		desc->dst_addr_lo = cpu_to_le32(PCI_DMA_L(ep_addr));
		desc->dst_addr_hi = cpu_to_le32(PCI_DMA_H(ep_addr));
	}
	else {
		/* read from end point address (source address) */
		desc->src_addr_lo = cpu_to_le32(PCI_DMA_L(ep_addr));
		desc->src_addr_hi = cpu_to_le32(PCI_DMA_H(ep_addr));
		/* write to root complex memory (destination address) */
		desc->dst_addr_lo = cpu_to_le32(PCI_DMA_L(rc_bus_addr));
		desc->dst_addr_hi = cpu_to_le32(PCI_DMA_H(rc_bus_addr));
	}
}


static int transfer_build(struct xdma_transfer *transfer, u64 ep_addr,
	int dir_to_dev, int non_incr_addr, int force_new_desc,
	int userspace)
{
	int i = 0;
	int j = 0;
	int new_desc;
	dma_addr_t cont_addr;
	dma_addr_t addr;
	unsigned int cont_len;
	unsigned int len;
	unsigned int cont_max_len = 0;
	struct scatterlist *sgl;

	

	sgl = transfer->sgm->sgl;

	/* start first contiguous block */
	cont_addr = addr = sg_dma_address(&transfer->sgm->sgl[i]);
	cont_len = 0;

	/* iterate over all remaining entries but the last */
	for (i = 0; i < transfer->sgl_nents - 1; i++) {
		/* bus address of next entry i + 1 */
		dma_addr_t next = sg_dma_address(&sgl[i + 1]);
		/* length of this entry i */
		len = sg_dma_len(&sgl[i]);
		dbg_desc("SGLE %04d: addr=0x%016llx length=0x%08x\n", i,
			(u64)addr, len);

		/* add entry i to current contiguous block length */
		cont_len += len;

		new_desc = 0;

		/* entry i + 1 is non-contiguous with entry i? CONTIGUOUS接触的，临近的 */
		if (next != addr + len) {
			dbg_desc("NON-CONTIGUOUS WITH DESC %d\n", i + 1);
			new_desc = 1;
		}
		/* entry i reached maximum transfer size? */
		else if (cont_len > (XDMA_DESC_MAX_BYTES - PAGE_SIZE)) {
			dbg_desc("BREAK\n");
			new_desc = 1;
		}

		if ((force_new_desc) && !(userspace))
			new_desc = 1;

		if (new_desc) {
			/* fill in descriptor entry j with transfer details */
			xdma_desc_set(transfer->desc_virt + j, cont_addr,
				ep_addr, cont_len, dir_to_dev);


			if (cont_len > cont_max_len)
				cont_max_len = cont_len;

			dbg_desc("DESC %4d:cont_addr=0x%llx\n", j,
				(u64)cont_addr);
			dbg_desc("DESC %4d:cont_len=0x%08x\n", j, cont_len);
			dbg_desc("DESC %4d:ep_addr=0x%llx\n", j, (u64)ep_addr);
			/* proceed EP address for next contiguous block */

			/* for non-inc-add mode don't increment ep_addr */
			if (userspace) {
				if (non_incr_addr == 0)
					ep_addr += cont_len;
			}
			else {
				ep_addr += cont_len;
			}

			/* start new contiguous block */
			cont_addr = next;
			cont_len = 0;
			j++;
		}
		/* goto entry i + 1 */
		addr = next;
	}
	/* i is the last entry in the scatterlist, add it to the last block */
	len = sg_dma_len(&sgl[i]);
	cont_len += len;
	BUG_ON(j > transfer->sgl_nents);

	/* j is the index of the last descriptor */

	dbg_desc("SGLE %4d: addr=0x%016llx length=0x%08x\n", i, (u64)addr, len);
	dbg_desc("DESC %4d: cont_addr=0x%llx cont_len=0x%08x ep_addr=0x%llx\n",
		j, (u64)cont_addr, cont_len, (unsigned long long)ep_addr);

	/* XXX to test error condition, set cont_len = 0 */

	/* fill in last descriptor entry j with transfer details */
	xdma_desc_set(transfer->desc_virt + j, cont_addr, ep_addr, cont_len,
		dir_to_dev);

	return j;
}


/* xdma_desc_link() - Link two descriptors
*
* Link the first descriptor to a second descriptor, or terminate the first.
*
* @first first descriptor
* @second second descriptor, or NULL if first descriptor must be set as last.
* @second_bus bus address of second descriptor
*/
static void xdma_desc_link(struct xdma_desc *first, struct xdma_desc *second,
	dma_addr_t second_bus)
{
	/*
	* remember reserved control in first descriptor, but zero
	* extra_adjacent!
	*/
	/* RTO - what's this about?  Shouldn't it be 0x0000c0ffUL? */
	u32 control = le32_to_cpu(first->control) & 0x0000f0ffUL;
	/* second descriptor given? */
	if (second) {
		/*
		* link last descriptor of 1st array to first descriptor of
		* 2nd array
		*/
		first->next_lo = cpu_to_le32(PCI_DMA_L(second_bus));
		first->next_hi = cpu_to_le32(PCI_DMA_H(second_bus));
		WARN_ON(first->next_hi);
		/* no second descriptor given */
	}
	else {
		/* first descriptor is the last */
		first->next_lo = 0;
		first->next_hi = 0;
	}
	/* merge magic, extra_adjacent and control field */
	control |= DESC_MAGIC;

	/* write bytes and next_num */
	first->control = cpu_to_le32(control);
}

/* xdma_desc_control -- Set complete control field of a descriptor. */
static void xdma_desc_control(struct xdma_desc *first, u32 control_field)
{
	/* remember magic and adjacent number */
	u32 control = le32_to_cpu(first->control) & ~(LS_BYTE_MASK);

	BUG_ON(control_field & ~(LS_BYTE_MASK));
	/* merge adjacent and control field */
	control |= control_field;
	/* write control and next_adjacent */
	first->control = cpu_to_le32(control);
}
static void transfer_terminate(struct xdma_transfer *transfer, int last)
{
	u32 control;

	/* terminate last descriptor */
	xdma_desc_link(transfer->desc_virt + last, 0, 0);
	/* stop engine, EOP for AXI ST, req IRQ on last descriptor */
	control = XDMA_DESC_STOPPED;
	control |= XDMA_DESC_EOP;
	control |= XDMA_DESC_COMPLETED;
	xdma_desc_control(transfer->desc_virt + last, control);
}

/* xdma_desc_adjacent -- Set how many descriptors are adjacent to this one */
static void xdma_desc_adjacent(struct xdma_desc *desc, int next_adjacent)
{
	int extra_adj = 0;
	/* remember reserved and control bits */
	u32 control = le32_to_cpu(desc->control) & 0x0000f0ffUL;
	u32 max_adj_4k = 0;

	if (next_adjacent > 0) {
		extra_adj = next_adjacent - 1;
		if (extra_adj > MAX_EXTRA_ADJ) {
			extra_adj = MAX_EXTRA_ADJ;
		}
		max_adj_4k = (0x1000 - ((le32_to_cpu(desc->next_lo)) & 0xFFF)) / 32 - 1;
		if (extra_adj > max_adj_4k) {
			extra_adj = max_adj_4k;
		}
		if (extra_adj < 0) {
			printk("Warning: extra_adj<0, converting it to 0\n");
			extra_adj = 0;
		}
	}
	/* merge adjacent and control field */
	control |= 0xAD4B0000UL | (extra_adj << 8);
	/* write control and next_adjacent */
	desc->control = cpu_to_le32(control);
}

static void dump_desc(struct xdma_desc *desc_virt)
{
	int j;
	u32 *p = (u32 *)desc_virt;
	static char * const field_name[] = {
		"magic|extra_adjacent|control", 
		"bytes", 
		"src_addr_lo",
		"src_addr_hi", 
		"dst_addr_lo", 
		"dst_addr_hi", 
		"next_addr",
		"next_addr_pad" };
	char *dummy;

	/* remove warning about unused variable when debug printing is off */
	dummy = field_name[0];

	for (j = 0; j < 8; j += 1) {
		dbg_desc("0x%08lx/0x%02lx: 0x%08x 0x%08x %s\n",
			(uintptr_t)p, (uintptr_t)p & 15, (int)*p,
			le32_to_cpu(*p), field_name[j]);
		p++;
	}
	dbg_desc("\n");
}
static void transfer_dump(struct xdma_transfer *transfer)
{
	int i;
	struct xdma_desc *desc_virt = transfer->desc_virt;

	BUG_ON(!transfer->desc_num);
	dbg_desc("Descriptor Entry (Pre-Transfer)\n");
	for (i = 0; i < transfer->desc_num; i += 1)
		dump_desc(desc_virt + i);
}
static ssize_t  xpcie_h2c_write(struct file *file, const char __user *buf, size_t count, loff_t * ppos)
{
	int i;
	int userspace = 1;
	int force_new_desc = 0;
	int non_incr_addr=0;
	int last = 0; 
	int rc;
	unsigned long max_len = count;
	int dir_to_dev = 1;
	struct xdma_transfer transfer;
	u64 ep_addr = (u64)(*ppos);


	dbg_sg("xpcie_h2c_write\n");
	memset(&transfer, 0, sizeof(transfer));

	/* 为sgm分配空间 */
	transfer.sgm = sg_create_mapper(max_len);

	/* 获取用户空间的页, 存放在scatterlist中 */
	rc = sgm_get_user_pages(transfer.sgm, buf, count, dir_to_dev);
	dbg_sg("mapped_pages=%d.\n", transfer.sgm->mapped_pages);

	dbg_sg("sgl = 0x%p.\n", transfer.sgm->sgl);
	BUG_ON(!transfer.sgm->sgl);
	BUG_ON(!transfer.sgm->mapped_pages);

	/* map sgl */
	transfer.sgl_nents = pci_map_sg(
		xpcie_dev, transfer.sgm->sgl,
		transfer.sgm->mapped_pages,
		dir_to_dev ? DMA_TO_DEVICE : DMA_FROM_DEVICE 
	);
	dbg_sg("hwnents=%d.\n", transfer.sgl_nents);

	/* 分配desc */
	transfer.desc_virt = xdma_desc_alloc(xpcie_dev, transfer.sgl_nents, &transfer.desc_bus, NULL);
	dbg_sg("transfer_create():\n");
	dbg_sg("transfer->desc_bus = 0x%llx.\n", transfer.desc_bus);



	/* 填写desc a.建立链表 */
	last = transfer_build(&transfer, ep_addr, dir_to_dev, non_incr_addr,
		force_new_desc, userspace);


	/* 填写desc b.终止链表 */
	transfer_terminate(&transfer, last);

	/* 填写desc c.设置adjcent */
	last++;
	transfer.desc_adjacent = last;
	transfer.desc_num = last;
	dbg_sg("transfer 0x%p has %d descriptors\n", &transfer, transfer.desc_num);
	for (i = 0; i < transfer.desc_num; i++) {
		xdma_desc_adjacent(transfer.desc_virt + i,
			transfer.desc_num - i - 1);
	}

	/* 打印desc */
	transfer_dump(&transfer);

	/*  开始传输  */
	rc = transfer_start(xpcie_dev, &transfer, &h2c_irq);
	if (rc == 0)
	{
		rc = count;
	}


	/* 取消DMA流映射 */
	pci_unmap_sg(xpcie_dev, transfer.sgm->sgl,
		transfer.sgm->mapped_pages,
		dir_to_dev ? DMA_TO_DEVICE : DMA_FROM_DEVICE);

	/* 释放用户页 */
	sgm_put_user_pages(transfer.sgm, dir_to_dev);

 	sg_destroy_mapper(transfer.sgm);

	return rc;
}


static int  xpcie_h2c_mmap(struct file *file, struct vm_area_struct *vma)
{
	int rc;
	unsigned long off;
	unsigned long phys;
	unsigned long vsize;
	unsigned long psize;
	int bar = 1;
	dbg_io(DRV_NAME "call xpcie_h2c_mmap ");


	off = vma->vm_pgoff << PAGE_SHIFT;

	/* BAR physical address */
	phys = pci_resource_start(xpcie_dev, bar) + off;
	vsize = vma->vm_end - vma->vm_start;

	/* complete resource */
	psize = pci_resource_end(xpcie_dev, bar) -
		pci_resource_start(xpcie_dev, bar) + 1 - off;



	dbg_sg("mmap(): lro_char->bar = %d\n", bar);
	dbg_sg("off = 0x%lx\n", off);
	dbg_sg("start = 0x%llx\n",
		(unsigned long long)pci_resource_start(xpcie_dev,
			bar));
	dbg_sg("phys = 0x%lx\n", phys);

	if (vsize > psize)
		return -EINVAL;

	/*
	* pages must not be cached as this would result in cache line sized
	* accesses to the end point
	*/
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	/*
	* prevent touching the pages (byte access) for swap-in,
	* and prevent the pages from being swapped out
	*/
	vma->vm_flags |= VMEM_FLAGS;
	/* make MMIO accessible to user space */
	rc = io_remap_pfn_range(vma, vma->vm_start, phys >> PAGE_SHIFT,
		vsize, vma->vm_page_prot);

	dbg_sg("vma=0x%p, vma->vm_start=0x%lx, phys=0x%lx, size=%lu = %d\n",
		vma, vma->vm_start, phys >> PAGE_SHIFT, vsize, rc);
	if (rc)
		return -EAGAIN;


	return 0;
}
static int xpcie_h2c_close(struct inode *inode, struct file *file)
{
	dbg_io(DRV_NAME "xpcie_h2c_close ");
	return 0;
}

static struct file_operations xpcie_h2c_fops = {
	.owner = THIS_MODULE,
	.release = xpcie_h2c_close,
	.open = xpcie_h2c_open,
	.read = xpcie_h2c_read,
	.write = xpcie_h2c_write,
	.mmap = xpcie_h2c_mmap
};

static int xpcie_c2h_close(struct inode *inode, struct file *file)
{
	dbg_io(DRV_NAME "xpcie_c2h_close ");
	return 0;
}
static struct file_operations xpcie_c2h_fops = {
	.owner = THIS_MODULE,
	.release = xpcie_c2h_close
};

static int __init xpcie_init(void)
{
	int rc = 0;
	dbg_init("begin  init\n");

	/* 注册 pcie 驱动*/
	rc = pci_register_driver(&xpcie_driver);
	if (rc != 0) {
		dbg_init(DRV_NAME ": failed to pci_register_driver");
		rc = -1;
		goto err_class;
	}

	/* 注册字符设备user, 分配主设备号 */
	rc = register_chrdev(0, DRV_NAME_USER, &xpcie_user_fops);
	if (rc < 0) {
		dbg_init(DRV_NAME ": failed to register_chrdev " DRV_NAME_USER);
		rc = -1;
		goto err_class;
	}
	major_user = rc;

	/* 注册字符设备h2c，使用已经分配好的主设备号 */
	rc = register_chrdev(0, DRV_NAME_H2C, &xpcie_h2c_fops);
	if (rc < 0) {
		dbg_init(DRV_NAME ": failed to register_chrdev " DRV_NAME_H2C);
		rc = -1;
		goto err_class;
	}
	major_h2c = rc;
	rc = 0;

	/* 注册字符设备c2h，使用已经分配好的主设备号 */
	rc = register_chrdev(0, DRV_NAME_C2H, &xpcie_c2h_fops);
	if (rc < 0) {
		dbg_init(DRV_NAME ": failed to register_chrdev " DRV_NAME_C2H);
		rc = -1;
		goto err_class;
	}
	major_c2h = rc;
	rc = 0;

	/* 使用mdev/udev机制自动创建设备文件 */
	/* 创建class */
	xpcie_class = class_create(THIS_MODULE, DRV_NAME);
	if (IS_ERR(xpcie_class)) {
		dbg_init(DRV_NAME ": failed to create class");
		rc = -1;
		goto err_class;
	}
	/* 创建user */
	xpcie_class_user = device_create(xpcie_class, NULL, MKDEV(major_user, user_minor), NULL, DRV_NAME_USER);
	/* 创建h2c */
	xpcie_class_h2c = device_create(xpcie_class, NULL, MKDEV(major_h2c, h2c_minor), NULL, DRV_NAME_H2C);
	/* 创建c2h */
	xpcie_class_c2h = device_create(xpcie_class, NULL, MKDEV(major_c2h, c2h_minor), NULL, DRV_NAME_C2H);

err_class:

	return rc;
}

// xpcie_exit
static void __exit xpcie_exit(void)
{
	dbg_init("prepare to exit()\n");

	device_destroy(xpcie_class, MKDEV(major_user, user_minor));
	device_destroy(xpcie_class, MKDEV(major_h2c, h2c_minor));
	device_destroy(xpcie_class, MKDEV(major_c2h, c2h_minor));
	class_destroy(xpcie_class);
	unregister_chrdev(major_user, DRV_NAME_USER);
	unregister_chrdev(major_h2c, DRV_NAME_H2C);
	unregister_chrdev(major_c2h, DRV_NAME_C2H);
	pci_unregister_driver(&xpcie_driver);
}

module_init(xpcie_init);
module_exit(xpcie_exit);
MODULE_LICENSE("GPL v2");