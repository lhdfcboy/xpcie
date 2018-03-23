#!/bin/sh
sudo ./dma_to_device -d /dev/xpcie_h2c -a 0x0000 -s 1024  -f data/datafile_32M.bin
