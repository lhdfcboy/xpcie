# xpcie
a simplified driver for Xilinx XDMA ip core

# 0. 前言 #
话说真的要感谢这个时代，感谢Jason Cong。
做一个基于FPGA的算法加速卡，不用写一句Verilog。

- PCIE方面，有xdma这个功能丰富的、高效的、易用的IP核。
- 框架方面，有Vivado的IPI，拖拽几下就搭好。
- 算法方面，有Vivado HLS，用C++写算法，用C++写测试，HLS直接生成。

本文主要分析驱动程序。PCIE的驱动是重头戏，传输数据是基本要求，更重要的是提高速率，发挥带宽优势，仅仅率高还不行，还得降低CPU占用。如果CPU一直忙着搬砖，就不用干其他事了。对于DMA传输，缓冲区都是应用程序分配的，位于用户空间，大小达到GB级别，怎样搬到加速卡才能高速、低消耗呢？如果使用kmalloc()开缓存一块一块拷，这纯是搬砖码农的思想，实际上LDD3已经给出了答案，使用“零拷贝”技术，在用户空间和卡直接传输数据，驱动程序只负责控制流程，不进行中转。

## 0.1 关于xdma ##
从2016.3开始，Vivado提供了一个强大的IP，全称是DMA/Bridge Subsystem for PCI Express，简称xdma。在此之前，对于7系列的FPGA，Xilinx官方提供的还有另外2个关于PCIE的IP核，都不好用，下面是这3个IP的对比：

- 7 Series Integrated Block for PCI Express —— 这个是底层的IP，无法直接使用，实际上xdma也是在这个IP上封装的。
- AXI Memory Mapped To PCI Express —— 具备DMA的功能，但是没有提供驱动程序。功能不够完善。
- DMA/Bridge Subsystem for PCI Express——具备AXI、AXI Lite、AXI Stream接口，功能丰富，附带驱动程序。

刚刚发布的时候，xdma还是Beta版。直到现在（2018-3-26），xdma的所有资料还都在AR# 65444这个问答区中，我们可以从这里下载到程序包。Xilinx_Answer_65444_Linux_Files.zip主要包含了2个东西，一个是驱动程序，一个是应用程序（用来测试），还有部分脚本和样例数据。对于驱动程序，xdma提供了非常强大的驱动，易用、高效、功能全。为了分析和理解，本文将从0开始重写这个驱动的关键部分。

对于应用程序。xdma的驱动提供了测试脚本和测试程序，搭环境阶段用来测试最方便不过。应用程序中，主要会用到：
reg_rw：寄存器读写。
dma_to_device/dma_from_device。DMA方式读写。
reg_rw通过mmap，以字节为单位访问PCIE的BAR寄存器。

在本设计中，BAR0寄存器映射到了xdma ip的AXI Lite主接口，该接口连接到BRAM；BAR1寄存器映射到xdma ip内部的寄存器，用来控制DMA流程，xdma ip的DMA控制器通过AXI主接口读写数据，该接口也连接到BRAM。因此，可以通过两种方式访问BRAM中的数据，两者都映射到了相同的BRAM地址，通过BAR0访问属于IO方式，通过BAR1操作寄存器实现传输属于DMA方式，IO方式可以用来验证DMA传输的数据是否正确。

## 0.2 硬件环境 ##
我使用的是zynq7030，自己定制的板子，其实PCIE加速卡，除了主芯片，其他硬件几乎一样，一端是PCIE金手指，一端是网口。大家可以用 ZC706评估板，zynq7045的芯片，用法一模一样。
如果用Ultrascale系列的高端片子，也是可以的。区别在于zynq的片子只支持PCIE GEN2 X4宽度，xdma能实现读写各2个逻辑通道。
Ultrascale支持PCIE GEN3 X8甚至X16宽度，xdma能实现读写各4个通道。

## 0.3 地址空间 ##
通过PCIE进行DMA操作会涉及各种地址，我画了一张图专门表示各个地址空间的区别和联系：

![AS](https://github.com/lhdfcboy/xpcie/raw/master/img/AddressSpace.png)

这张图中的虚线是主机和板卡的分界线。主机一侧只突出了内存，其他部分省略；板卡部分画出了FPGA内部的XDMA核和用户逻辑，XMDA内部包含了PCIE协议核以及DMA控制器。从左向右看，主机内存中的数据，通过真实的主机物理地址来访问。数据一旦到达PCIE总线，就需要用PCIE总线地址表示，最重要的是XDMA内部的BAR0和BAR1这两组配置寄存器，也是用总线地址访问的。数据从PCIE核出来后，有上下2路去处：

上路：IO访问。BAR0的地址加一个偏移量，偏移量和映射长度在IP核中都是可配置的，形成AXI Lite地址。通过AXI Lite直接操作用户寄存器。
下路：DMA访问。BAR1完全映射到DMA控制器，偏移量为0，映射长度固定为64kB。以h2c方向为例，DMA控制器根据从总线地址拿数据，再把数据交给用户数据宿。那么，配置DMA控制器时，源地址是总线地址，目的地址是AXI地址。

注意，根据《LDD3》的介绍，对于X86体系而言，PCIE总线地址和主机物理地址是相等的。但并不是说不经任何操作，DMA控制器就能任意访问主机物理地址。详见第4节讨论。

## 0.4 软件环境 ##
Vivado 2016.4<br>
VS2015+VisualGDB+VisualAssistX<br>
RHEL 6.5 (kernel 2.6.32)<br>
使用了0.45版的驱动和测试程序。

官方手册：pg195<br>
官方资料：AR# 65444<br>
官方页面：https://china.xilinx.com/support/answers/65444.html<br>

源码：https://github.com/lhdfcboy/xpcie.git<br>
每一节都对应一个tag（第一节没有）：<br>
lesson2<br>
lesson3<br>
lesson4<br>

切换tag的方法：<br>
git clone 下载源码<br>
git tag　列出所有版本号<br>
git checkout　+某版本号　<br>

最后，一起膜拜Jason Cong

![JC](https://github.com/lhdfcboy/xpcie/raw/master/img/JasonCong.png)

# 第1节 纯框架 #
  1. 搭建工程 && 测试效果<br>
./reg_rw /dev/xdma0_user 0x0008 w <br>
./dma_to_device -d /dev/xdma0_h2c_0 -a 0x0000 -s 1024  -f data/datafile_8K.bin<br>
在后面的步骤中，我们将使用reg_rw和dma_to_device两个应用程序。<br>
  2. 生成设备文件<br>
  3. 编译测试<br>

附件：
1. 好导出工程。
2. 导出pdf格式的原理图
3. 源码

# 第2节 mmap正常 #
需要做的事
  1. 初始化工作
  2. 映射bar到物理内存
  3. 实现mmap
  4. 测试程序
 
# 第3节 基本传输 #

这一节将实现DMA传输，我们的目的是通过操作寄存器，实现最基本的SG模式的DMA传输，然后验证数据是否正确。我们不使用中断，不使用用户空间缓冲区，只测试H2C0这一个通道，甚至不写应用程序。<br>
需要参考pg195第二章“DMA Operations"和"Register Space"两节内容。"DMA Operations"对DMA传输流程做了简要介绍，其中最重要的部分是传输描述符的构建和使用。"Register Space"是对寄存器的详细说明。别被那么多寄存器吓到了，好多是只读寄存器，很多代表出错，性能测试、还有一些是重复的W1C W1S寄存器。

## 3.1 读写寄存器 ## 
从pg195中可以看到，根据不同的目标（Target），xdma的寄存器一个分成9组。对于Target=0或Target=1的寄存器，每个通道都有一组，因此，寄存器的地址还要区分通道号。 因此，目标、通道、组内偏移量构成了一个寄存器的地址。为了方便操作，xmda.h中定义了一些结构体，每个结构体用来表示寄存器组，结构体成员表示寄存器，直接把它们拷贝过来。这些结构体与寄存器组的对应关系如下：

> H2C Channel Register Space (0x0)  对应struct engine_regs<br>
C2H Channel Registers (0x1) 对应struct engine_regs<br>
IRQ Block Registers (0x2)  这一节不搞中断。<br>
Config Block Registers (0x3) IP配置相关，几乎都是只读的，不理会<br>
H2C SGDMA Registers (0x4) 对应struct engine_sgdma_regs <br>
C2H SGDMA Registers (0x5) 对应struct engine_sgdma_regs <br>
SGDMA Common Registers (0x6) 对应struct sgdma_common_regs<br>
MSI-X Vector Table and PBA (0x8) 这一节不搞中断。<br>


其中，struct engine_regs是最重要的结构体，它能表示h2c或c2h的Channel Registers，这2组寄存器的结构是相同的。这个寄存器组用来控制DMA传输引擎启动或停止，还能读取DMA传输引擎的状态。engine_sgdma_regs用来存储第一个描述符的地址。在本节中，只会用到这2组寄存器。我们为这2个结构体定义全局的指针：<br>
```
static struct engine_regs *engine_regs_h2c;
static struct engine_sgdma_regs *engine_sgdma_regs;
```
在map_bars()函数中，对指针进行初始化：<br>
```
engine_regs_h2c = (struct engine_regs *) (bar[DMA_BAR_INDEX] + 0 * TARGET_SPACING);
engine_sgdma_regs = (struct engine_sgdma_regs *) (bar[DMA_BAR_INDEX] + 4 * TARGET_SPACING);
```
bar[]数组存储了PCIE BAR寄存器的物理地址，根据在上一节的内容，map_bars()的开始部分对bar[]数组进行了设置。在本例中，DMA_BAR_INDEX=1，也就是说BAR1寄存器空间是用来操作DMA的。<br>

有了这个结构体指针，我们需要读写寄存器时，只需这样调用：<br>
```
u32 value = read_register(&engine_regs_h2c->identifier);
write_register(value, &engine_regs_h2c->control);
read_register()和write_register()这两个函数直接从xdma-core.c中拷贝过来即可。
```


## 3.2 probe函数中做一些准备工作 ##
在上一节中，在probe函数中我们只调用了map_bars()函数。要想实现DMA传输，还需要对PCIE作一些设置。这些函数直接从xdma-core.c中拷贝过来即可。我们再增加一个函数test_h2c()，当所有准备工作完成后，立即发起一次DMA传输，数据是伪造的，这样就不需要实现read()和write()系统调用，也不需要写应用程序了。最后，probe函数看起来像这样子：<br>
```
probe()
{
pci_enable_device()
pci_set_master()
map_bars()
set_dma_mask()
test_h2c()
}
```
我们在test_h2c()函数中，把engine_regs_h2c->identifier这个寄存器的值打印出来，我的打印结果是这样<br>
test_dma_bar():engine_regs_h2c->identifier =  0x1FC00005<br>
只要1FC开头就对了，最后的05表示Vivado版本号，我的是2016.4，不一样没关系。<br>

## 3.3 描述符 ## 
实现SG传输，需要构建一个描述符，这个描述符是存在于主机内存中的，每个描述符描述了一段主机内存区域。当DMA引擎启动时，会读取第一个描述符，获取第一个内存区域的长度、源地址、目的地址，并进行传输，传输完毕后，根据第一个描述符的 Nxt_adr域，获取第二个描述符，直到处理完最后一个。<br>
描述符的第一个字由一下几个域构成：

> Magic[15:0] Rsv[1:0] Nxt_adj[5:0] Control[7:0]<br>
Magic[15:0] 固定值0xAD4B<br>
Control[7:0] 能够表明是否为最后一个。<br>
Nxt_adj[5:0] 暂时不管，把它设为0。<br>

Magic的作用是表明这是一个描述符，这个功能很重要。DMA引擎通过PCIE总线地址访问主机内存，总线地址可能没有设置正确，DMA引擎第一次访问主机内存就是访问描述符，如果发现magic不对，能够很快定位错误并停止传输。<br>
xdma-core.h中定义了一个结构体struct xdma_desc，用来表示描述符，拿来用。<br>

## 3.4 传输 ##
既然没有应用程序，我们直接申请一块空间并填充数据。在PCIE中，应当使用pci_alloc_consistent来分配数据，而不是kmalloc，这里面的原理，可以参考《Linux设备驱动开发详解》的“11.6 DMA”节内容。<br>
分配之前，我们先定义2个变量用来存放地址，data_virt是虚拟地址，驱动程序中可以直接使用，data_bus是总线地址，我们将把它填写入描述符，给DMA引擎使用：<br>
```
char* data_virt;<br>
dma_addr_t data_bus;<br>
```
分配空间并填充数据：<br>
```
desc_virt = pci_alloc_consistent(pdev, desc_size, &desc_bus);
for (i = 0; i<data_size; i++)
{
	data_virt[i] = i;
}
```
注意，驱动程序使用data_virt指针来操作这块缓冲区。<br>

除了数据缓冲区，描述符也是在主机内存中的，我们使用同样的方法定义、分配、并初始化：<br>
```
struct xdma_desc *desc_virt;
dma_addr_t desc_bus;
desc_virt = pci_alloc_consistent(pdev, desc_size, &desc_bus);
memset(desc_virt, 0, desc_size);
```
接下来最关键的一步是填充描述符：<br>
```
/* 填充desc */
u64 ep_addr = 0;
u32 temp_control = DESC_MAGIC | (0x1);
desc_virt->bytes = data_size;
desc_virt->control = cpu_to_le32(temp_control);
desc_virt->dst_addr_lo = cpu_to_le32(PCI_DMA_L(ep_addr));
desc_virt->dst_addr_hi = cpu_to_le32(PCI_DMA_H(ep_addr));
desc_virt->next_hi = 0;
desc_virt->next_lo = 0;
desc_virt->src_addr_lo = cpu_to_le32(PCI_DMA_L(data_bus));
desc_virt->src_addr_hi = cpu_to_le32(PCI_DMA_H(data_bus));
```
我们同样使用内核虚拟地址desc_virt来操作描述符缓冲区。DESC_MAGIC 是描述符的表示号， (0x1)这是最后一个描述符。 ep_addr 表示End Point地址，也就是目的地址，也就是从xdma出去后AXI总线的地址。源地址使用前面分配的数据缓冲区的总线地址data_bus。cpu_to_le32这函数用来把数据转成小端，由于X86_64本身是小端的，不用也可以。

接下来，我们要把描述符的总线地址告诉DMA引擎：
```
write_register(cpu_to_le32(PCI_DMA_L(desc_bus)), &engine_sgdma_regs->first_desc_lo);
write_register(cpu_to_le32(PCI_DMA_H(desc_bus)), &engine_sgdma_regs->first_desc_hi);
write_register(cpu_to_le32(0), &engine_sgdma_regs->first_desc_adjacent);
```
其中，first_desc_adjacent这个寄存器暂时不管，设成0即可。

一切准备就绪，启动引擎
```
u32 w;
w = (u32)XDMA_CTRL_RUN_STOP;
w |= (u32)XDMA_CTRL_IE_READ_ERROR;
w |= (u32)XDMA_CTRL_IE_DESC_ERROR;
w |= (u32)XDMA_CTRL_IE_DESC_ALIGN_MISMATCH;
w |= (u32)XDMA_CTRL_IE_MAGIC_STOPPED;
write_register(w, &engine_regs_h2c->control);
w = read_register(&engine_regs_h2c->status);/* dummy read of status register to flush all previous writes */
```
启动引擎其实很简单，我们只需要对engine_regs_h2c->control寄存器的第0位写1即可，上面的代码设置了一些报错的标志，可以参考手册查看其功能。这里在最后调用了一下read_register，从注释可以看书，是为了刷新主机的IO缓冲区（我也是头一次听说），使写入生效。

等待传输完成，传输是需要一定时间的，这里用了偷懒的方法等它1秒，O(∩_∩)O
```
mdelay(1000);
value = read_register(&engine_regs_h2c->status);
dbg_init("engine_regs_h2c->status =  0x%08X\n", value);
```
最后，很重要的一步，不要忘记停止引擎，否则下次就不能用啦。
```
write_register(0, &engine_regs_h2c->control);
```
## 3.5 测试和验证 ##
编译并加载该内核模块。然后用dmesg命令查看输出：
如果engine_regs_h2c->status==0，说明传输完成，状态正常。
```
xpcie_init():begin  init
xpcie 0000:01:00.0: PCI INT A -> GSI 16 (level, low) -> IRQ 16
xpcie_probe():pci_set_master()
xpcie 0000:01:00.0: setting latency timer to 64
request_regions():pci_request_regions()
map_single_bar():BAR0: 524288 bytes to be mapped.
map_single_bar():BAR0 at 0xf7d00000 mapped at 0xffffc90015e00000, length=524288(/524288)
map_single_bar():BAR1: 65536 bytes to be mapped.
map_single_bar():BAR1 at 0xf7d80000 mapped at 0xffffc90005200000, length=65536(/65536)
test_dma_read_bar():engine_regs_h2c->identifier =  0x1FC00005
test_dma_read_bar():sgdma_common_regs->identifier =  0x1FC60005
set_dma_mask():sizeof(dma_addr_t) == 8
set_dma_mask():pci_set_dma_mask()
set_dma_mask():Using a 64-bit DMA mask.
test_h2c():pci_alloc_consistent OK, phy=000000001F870000, bus=000000001F870000 
test_h2c():engine_regs_h2c->status =  0x00000000
xpcie_probe():probe() returning 0
```
同时，我们用应用程序验证一下：
```
$ sudo ./reg_rw /dev/xpcie_user 0x00
argc = 3
device: /dev/xpcie_user
address: 0x00000000
access type: read
access width: word (32-bits)
character device /dev/xpcie_user opened.
Memory mapped at address 0x7fe48e3ce000.
Read 32-bit value at address 0x00000000 (0x7fe48e3ce000): 0x03020100
```
可以看到，通过AXI-Lite接口，从BRAM中读取到了正确的值。这个值正是由AXI接口通过DMA方式写入BRAM的。

xdma-core.c 的 is_config_bar()函数展示了如何访问寄存器，lro->bar[idx]是已经映射过BAR地址，内核可以通过ioread32 iowrite32来访问。

# 第4节  传输来自用户空间的数据 #

上一节实现了基本的DMA传输，传输的数据位于内核的一致性缓冲区，这个一致性保证了DMA读写缓冲区与CPU高速缓存不会冲突。在实际场景中，对于H2C方
向，数据是来自用户空间的，如果希望传输到加速卡，有2种方式：<br>
方式一：在加速卡申请一块一致性缓冲区，调用copy_from_user()从用户空间拿数据到这个缓冲区，然后使用上一节的方法，将缓冲区数据传输到加速卡。很显然，数据多复制了一次，延迟会增加，CPU占用会增加。<br>
方式二：从用户空间的缓冲区直接DMA到加速卡。<br>

显然，方式二减少了不必要的大量数据的搬移。但是实现起来稍微复杂，首先要获取用户空间缓冲区的物理页，使用get_user_pages()函数，因为物理页不一定是连续的，需要构建scatterlist把他们连起来，然后再把scatterlist填写到xdma_desc结构中，剩下的工作与上一节相似，就可以启动DMA引擎进行传输了。
为了简单起见，我们仍然不使用中断，相比上一节，我们直接从用户空间获取数据，而不是在内核中分配缓冲区。这里只是不分配大量数据（载荷）的缓冲区，一些辅助的数据结构，还是在内核中分配的。<br>
用户空间页操作相关的函数和结构体在xdma-sgm.c xdma-sgm.h中定义，这个文件是与具体驱动无关的，可以直接拿来用，需要的时候，也可以移植到其他驱动上去。<br>
结构体说明：<br>
1. struct sg_mapping_t 
xdma-sgm.h中最主要的结构体是sg_mapping_t，它的定义如下：
```
struct sg_mapping_t {
	struct scatterlist *sgl;
	struct page **pages;
	int max_pages;
	int mapped_pages;
};
```
scatterlist 是内核定义的专门用来存放DMA分散聚集表的结构；max_pages表示需要映射的页数。pages是个指针数组，存放了max_pages个页，sgl也存放了max_pages个页。 mapped_pages表示已经映射的页数。<br>
2. struct xdma_transfer
```
struct xdma_transfer {
	struct xdma_desc *desc_virt;	/* virt addr of the 1st descriptor */
	dma_addr_t desc_bus;		/* bus addr of the first descriptor */
	int desc_adjacent;		/* adjacent descriptors at desc_bus */
	int desc_num;			/* number of descriptors in transfer */
	int dir_to_dev;			/* specify transfer direction */
	int sgl_nents;			/* adjacent descriptors at desc_virt */
	struct sg_mapping_t *sgm;	/* user space scatter-gather mapper */
...
};
```
这里只列举了主要的成员。这个结构体存储了一次传输所必须的数据。在xdma-core.c中，每次应用程序调用read()/write()，就相当于发起了一次传输。xdma-core.c把每个传输进行抽象并提交到一个内核工作队列去执行。为了简单起见，我们仍然保留struct xdma_transfer，但是不搞工作队列，也不使用中断，而是直接发起一次传输。<br>

实现步骤：<br>
1.写一个函数xpcie_h2c_write()作为fops的write成员，这个函数中实现用户空间数据DMA到加速卡。<br>
首先定义一个transfer变量，一次传输过程会用的的信息都保存在这个变量中，主要是为了能够直接调用xdma-core.c中的函数。<br>
```
struct xdma_transfer transfer;
```
2.为sg_mapping_t 分配空间。<br>
分配后的空间存放在指针transfer.sgm 中。<br>
```
transfer.sgm = sg_create_mapper(max_len);
```
使用完要释放，这两行添加到函数的最后。<br>
```
sg_destroy_mapper(transfer.sgm);
```
3.获取用户空间的页。<br>
```
rc = sgm_get_user_pages(transfer.sgm, buf, count, dir_to_dev);
```
使用完要释放，这两行添加到函数的最后。<br>
```
sgm_put_user_pages(transfer.sgm, dir_to_dev);
```
 4.映射scatterlist<br>
获取到用户空间的物理页之后，要映射成DMA流式缓冲区才能使用。<br>
```
transfer.sgl_nents = pci_map_sg(transfer.sgm->sgl, ...)
```
用完后要取消映射<br>
```
pci_unmap_sg()
```

5.分配描述符<br>
```
transfer.desc_virt = xdma_desc_alloc()
```
返回的是描述符在内核中的虚拟地址。对应的总线地址是transfer.desc_bus。<br>
稍后配置寄存器时，要使用总线地址。<br>
注意，使用完后清理<br>
```	
xdma_desc_free();
```
6.填写描述符<br>
```
/* 填写desc a.建立链表 */
last = transfer_build();

/* 填写desc b.终止链表 */
transfer_terminate(&transfer, last);

/* 填写desc c.设置adjcent */
transfer.desc_adjacent = last;

/* 打印desc */
transfer_dump(&transfer);
 ```
7.发起传输<br>
```
transfer_start(xpcie_dev, &transfer);
```
测试方案：<br>
首先使用自己的驱动传输256K的数据到卡，然后把数据读出来进行比较。<br>
由于本文只实现了h2c的方向，对于c2h可以用xdma的驱动。<br>
写入数据：<br>
```
sudo ./dma_to_device -d /dev/xpcie_h2c -a 0x0000 -s 263183  -f data/datafile_256K.bin
```
更换驱动：
```
sudo rmmod xpcie.ko
sudo  ./load_driver.sh
```
读出数据：
```
./dma_from_device -d /dev/xdma0_c2h_0 -a 0x0000 -s 263183  -f data/datafile_256K_out.bin
```
比较数据：
```
diff data/datafile_256K.bin data/datafile_256K_out.bin
```
如果diff命令没有任何输出，说明输入的和输出的2个文件是一致的，传输成功。<br>

# 第5节  使用中断 #
对于PCIE总线，有三种中断，可以在xdma ip中设置：<br>
Legacy：传统的中断，为了兼容PCI。<br>
MSI：<br>
MSIX：<br>

MSI中断机制最多只能使用32个中断向量，而MSI-X可以使用更多的中断向量。目前Intel的许多PCIe设备支持MSI-X中断机制。与MSI中断机制相比，MSI-X机制更为合理。首先MSI-X可以支持更多的中断请求，但是这并不是引入MSI-X中断机制最重要的原因。因为对于多数PCIe设备，32种中断请求已经足够了。而引入MSI-X中断机制的主要原因是，使用该机制不需要中断控制器分配给该设备的中断向量号连续。

我们使用MSI，这也是xdma的默认选项。我们需要做几件事：<br>
  1. 在probe函数中申请判断硬件支持的中断类型<br>
  2. 使能MSI中断/关闭MSI中断<br>
  3. 申请中断资源/释放中断资源<br>
  4. 配置寄存器。使能中断。<br>
  5. 实现中断处理函数。<br>
  6. 启动传输后，挂起进程。<br>
首先，引入结构体 xdma_irq，<br>
```
struct xdma_irq {
	u8 events_irq;			/* accumulated IRQs */
	spinlock_t events_lock;		/* lock to safely update events_irq */
	wait_queue_head_t events_wq;	/* wait queue to sync waiting threads */
};
```
events_irq是一个标志寄存器，表示中断已经发生，用于wati_events_interruptibel()函数。<br>
events_lock是自旋锁。<br>
events_wq是等待队列。<br>

下文中提到的pdev是struct pci_dev* 类型。<br>
1. 在probe函数中申请判断硬件支持的中断类型<br>
使用这个函数判断是否支持MSI中断<br>
```
pci_find_capability(pdev, PCI_CAP_ID_MSI)
```
2. 使能关闭MSI中断<br>
```
pci_enable_msi(pdev)/pci_disable_msi(pdev)
```
一定要成对操作，在remove函数中将中断关闭。<br>
否则，重新安装内核模块时，会出错。<br>
3.申请中断资源/释放中断资源<br>
使用这个函数来申请IRQ资源：<br>
```
rc = request_irq(pdev->irq, xdma_isr, irq_flag, DRV_NAME, NULL); 
```
第一个参数：中断号，自动分配的，保存在struct pci_dev* 结构体中。<br>
第二个参数：中断服务程序函数指针<br>
第三个参数：标志位。对于MSI-X中断，需要设置成IRQF_SHARED，表示多个设备共享中断。对于MSI中断设置为0即可。<br>
第四个参数：中断程序名字。<br>
第五个参数：上下文指针，不用就设为NULL吧。延伸：对于回调函数、信号处理函数、中断函数等等，都需要一个上下文指针context，以便中断服务程序能够获取一些中断申请者的资源。如果没有上下文指针，只能通过全局变量的方式来共享资源。对于一些函数库，如果提供的回调函数没有上下文指针，是不负责任的做法。<br>
释放中断资源：<br>
在probe函数中申请中断资源后，需要在remove函数中释放。<br>
free_irq(pdev->irq, NULL);<br>
注意：对于申请XXX之类的操作，要记得释放，最好将这类功能成对实现，以免疏忽和遗漏。顺序也很重要，释放/关闭的操作要和创建/打开的操作完全相反。<br>




