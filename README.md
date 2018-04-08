# xpcie
a simplified driver for Xilinx XDMA ip core

# 0. 前言 #
话说真的要感谢这个时代，感谢Jason Cong。
做一个基于FPGA的算法加速卡，不用写一句Verilog。

- PCIE方面，有xdma这个功能丰富的、高效的、易用的IP核。
- 框架方面，有Vivado的IPI，拖拽几下就搭好。
- 算法方面，有Vivado HLS，用C++写算法，用C++写测试，HLS直接生成。

本文主要分析驱动程序。PCIE的驱动是重头戏，传输数据是基本要求，更重要的是提高速率，发挥带宽优势，仅仅率高还不行，还得降低CPU占用。如果CPU一直忙着搬砖，就不用干其他事了。对于DMA传输，缓冲区都是应用程序分配的，位于用户空间，大小达到GB级别，怎样搬到加速卡才能高速、低消耗呢？如果使用kmalloc()开缓存一块一块拷，这纯是搬砖码农的思想，实际上LDD3已经给出了答案，使用“零拷贝”技术，在用户空间和卡直接传输数据，驱动程序只负责控制流程，不进行中转。

## 关于xdma ##
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

## 硬件环境 ##
我使用的是zynq7030，自己定制的板子，其实PCIE加速卡，除了主芯片，其他硬件几乎一样，一端是PCIE金手指，一端是网口。大家可以用 ZC706评估板，zynq7045的芯片，用法一模一样。
如果用Ultrascale系列的高端片子，也是可以的。区别在于zynq的片子只支持PCIE GEN2 X4宽度，xdma能实现读写各2个逻辑通道。
Ultrascale支持PCIE GEN3 X8甚至X16宽度，xdma能实现读写各4个通道。

## 地址空间 ##
通过PCIE进行DMA操作会涉及各种地址，我画了一张图专门表示各个地址空间的区别和联系：

![AS](https://github.com/lhdfcboy/xpcie/raw/master/img/AddressSpace.png)

这张图中的虚线是主机和板卡的分界线。主机一侧只突出了内存，其他部分省略；板卡部分画出了FPGA内部的XDMA核和用户逻辑，XMDA内部包含了PCIE协议核以及DMA控制器。从左向右看，主机内存中的数据，通过真实的主机物理地址来访问。数据一旦到达PCIE总线，就需要用PCIE总线地址表示，最重要的是XDMA内部的BAR0和BAR1这两组配置寄存器，也是用总线地址访问的。数据从PCIE核出来后，有上下2路去处：

上路：IO访问。BAR0的地址加一个偏移量，偏移量和映射长度在IP核中都是可配置的，形成AXI Lite地址。通过AXI Lite直接操作用户寄存器。
下路：DMA访问。BAR1完全映射到DMA控制器，偏移量为0，映射长度固定为64kB。以h2c方向为例，DMA控制器根据从总线地址拿数据，再把数据交给用户数据宿。那么，配置DMA控制器时，源地址是总线地址，目的地址是AXI地址。

注意，根据《LDD3》的介绍，对于X86体系而言，PCIE总线地址和主机物理地址是相等的。但并不是说不经任何操作，DMA控制器就能任意访问主机物理地址。详见第4节讨论。

## 软件环境 ##
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
