#!/bin/sh

echo "Creating disk.img..."
# bximage -mode=create -hd=10M -q disk.img

echo "Compiling..."
nasm -I include/ -o mbr.bin mbr.S
nasm -I include/ -o loader.bin loader.S
# 编译 print.S
nasm -f elf -o lib/kernel/print.o lib/kernel/print.S
# 编译 main.c
gcc -I lib/kernel/ -m32 -c -o kernel/main.o kernel/main.c
#链接
ld -m elf_i386 -Ttext 0xc0001500 -e main -o kernel/kernel.bin kernel/main.o lib/kernel/print.o

echo "Writing mbr, loader and kernel to disk..."
dd if=mbr.bin of=/home/husharp/bochs_hu/bochs/hd60M.img bs=512 count=1 conv=notrunc
dd if=loader.bin of=/home/husharp/bochs_hu/bochs/hd60M.img bs=512 count=4 seek=2 conv=notrunc
dd if=kernel/kernel.bin of=/home/husharp/bochs_hu/bochs/hd60M.img bs=512 count=200 seek=9 conv=notrunc


echo "Now start bochs and have fun!"

cd /home/husharp/bochs_hu/bochs/
bin/bochs -f bochsrc.disk

