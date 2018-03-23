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


static struct class *xpcie_class;	/* sys filesystem */
static struct class_device *xpcie_class_user;	/* sys filesystem */
static struct class_device *xpcie_class_h2c;	/* sys filesystem */
static struct class_device *xpcie_class_c2h;	/* sys filesystem */
static int major_user;
static int major_h2c;
static int major_c2h;
static int user_minor = 1;
static int h2c_minor = 2;
static int c2h_minor = 3;
static struct pci_dev *xpcie_dev;

static const struct pci_device_id pci_ids[] = {
	{ PCI_DEVICE(0x10ee, 0x7024), },
	{ PCI_DEVICE(0x10ee, 0x7038), },
	{ 0, }
};

inline void write_register(u32 value, void *iomem)
{
	iowrite32(value, iomem);
}

inline u32 read_register(void *iomem)
{
	return ioread32(iomem);
}

#define XDMA_BAR_NUM 2
static void *__iomem bar[XDMA_BAR_NUM];	/* addresses for mapped BARs */
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

static void engine_start()
{
	u32 w;

	/* write control register of SG DMA engine */
	w = (u32)XDMA_CTRL_RUN_STOP;
	w |= (u32)XDMA_CTRL_IE_READ_ERROR;
	w |= (u32)XDMA_CTRL_IE_DESC_ERROR;
	w |= (u32)XDMA_CTRL_IE_DESC_ALIGN_MISMATCH;
	w |= (u32)XDMA_CTRL_IE_MAGIC_STOPPED;
	
	/* start the engine */
	write_register(w, &engine_regs_h2c->control);

	/* dummy read of status register to flush all previous writes */
	w = read_register(&engine_regs_h2c->status);
}

static void engine_stop()
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
	/* 分配desc */
	desc_size = sizeof(struct xdma_desc);
	desc_virt = pci_alloc_consistent(pdev, desc_size, &desc_bus);
	if (!desc_virt)
	{
		dbg_init("pci_alloc_consistent failed \n");
		goto out1;
	}
	memset(desc_virt, 0, desc_size);
	dbg_init("pci_alloc_consistent OK, phy=%016X, bus=%016X \n", desc_virt, desc_bus);

	/* 分配数据换缓冲区，伪造数据 */
	data_virt = pci_alloc_consistent(pdev, data_size, &data_bus);
	if (!data_virt)
	{
		dbg_init("pci_alloc_consistent failed \n");
		goto out2;
	}
	for (i = 0; i<data_size; i++)
	{
		data_virt[i] = i;
	}

	/* 填充desc */
	temp_control = DESC_MAGIC | (0x1);
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
static int test_dma_read_bar(void)
{
	u32 value;
	value = read_register(&engine_regs_h2c->identifier);
	dbg_init("engine_regs_h2c->identifier =  0x%08X\n", value);

	value = read_register(&sgdma_common_regs->identifier);
	dbg_init("sgdma_common_regs->identifier =  0x%08X\n", value);
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
	test_dma_read_bar();

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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
static void enable_pcie_relaxed_ordering(struct pci_dev *dev)
{
	pcie_capability_set_word(dev, PCI_EXP_DEVCTL, PCI_EXP_DEVCTL_RELAX_EN);
}
#else
static void __devinit enable_pcie_relaxed_ordering(struct pci_dev *dev)
{
	u16 v;
	int pos;

	pos = pci_pcie_cap(dev);
	if (pos > 0) {
		pci_read_config_word(dev, pos + PCI_EXP_DEVCTL, &v);
		v |= PCI_EXP_DEVCTL_RELAX_EN;
		pci_write_config_word(dev, pos + PCI_EXP_DEVCTL, v);
	}
}
#endif
static int xpcie_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int rc;
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

	rc = request_regions(pdev);
	if (rc)
		goto err_regions;

	map_bars(pdev);

	set_dma_mask(pdev);
	test_h2c(pdev);

err_sgdma_cdev:
err_interfaces:
	dbg_init("xpcie_probe() err_interfaces\n");
	//remove_engines(lro);
err_engines:
	dbg_init("xpcie_probe() err_engines\n");
	//irq_teardown(lro);
err_interrupts:
err_mask:
	dbg_init("xpcie_probe() err_mask\n");
	//unmap_bars(lro, pdev);
err_map:
	dbg_init("xpcie_probe() err_map\n");
	//if (lro->got_regions)
	//	pci_release_regions(pdev);
err_regions:
	dbg_init("xpcie_probe() err_regions\n");
	//if (lro->msi_enabled)
	//	pci_disable_msi(pdev);
err_scan_msi:
	//if (!lro->regions_in_use)
	//	pci_disable_device(pdev);*/
err_enable:
	//kfree(lro);
err_alloc:
end:

	dbg_init("probe() returning %d\n", rc);
	return rc;
}



static void xpcie_remove(struct pci_dev *pdev)
{
	/* 这里有点问题，要搞个标志位，表示内存是否被映射过 */
	int bar = 0;


	dbg_io(DRV_NAME "call remove ");

	unmap_bars(pdev);
	pci_release_regions(pdev);
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
static int xpcie_user_read(struct file *filp, char __user *buff, size_t count, loff_t *offp)
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
static int  xpcie_h2c_read(struct file *filp, char __user *buff, size_t count, loff_t *offp)
{
	return 0;
}

static ssize_t  xpcie_h2c_write(struct file *file, const char __user *buf, size_t count, loff_t * ppos)
{
	return 0;
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
	int i;
	dbg_init("begin  init\n");

	/* 注册 pcie 驱动*/
	rc = pci_register_driver(&xpcie_driver);
	if (IS_ERR(rc)) {
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