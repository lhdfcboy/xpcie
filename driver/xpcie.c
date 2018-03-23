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


static int request_regions( struct pci_dev *pdev)
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
static int xpcie_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int rc;
	xpcie_dev = pdev;

	rc = pci_enable_device(pdev);
	if (rc) {
		dbg_init("pci_enable_device() failed, rc = %d.\n", rc);
		goto err_enable;
	}

	/* enable bus master capability */
	dbg_init("pci_set_master()\n");
	pci_set_master(pdev);

	rc = request_regions(pdev);
	if (rc)
		goto err_regions;

	map_single_bar(pdev, 0);
	map_single_bar(pdev, 1);


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

static int xpcie_h2c_close(struct inode *inode, struct file *file)
{
	dbg_io(DRV_NAME "xpcie_h2c_close ");
	return 0;
}
static int xpcie_c2h_close(struct inode *inode, struct file *file)
{
	dbg_io(DRV_NAME "xpcie_c2h_close ");
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
static struct file_operations xpcie_h2c_fops = {
	.owner = THIS_MODULE,
	.release = xpcie_h2c_close
};
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