#!/bin/bash
./reg_rw /dev/accel0_user 0x00000000

./dma_to_device -d /dev/accel0_h2c_0 -f data/datafile0_4K.bin -s 128 -c 1 
./dma_from_device -d /dev/accel0_c2h_0 -f out.bin -s 128 

./dma_to_device -d /dev/accel0_h2c_0 -f data/datafile0_4K.bin -s 128 -a 0xc0000000
    
./dma_to_device -d /dev/accel0_h2c_1 -f command -s 16
./dma_to_device -d /dev/accel0_h2c_0 -f data/datafile0_4K.bin -s 128
./dma_from_device -d /dev/accel0_c2h_1 -f status -s 16 