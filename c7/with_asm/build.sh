#!/bin/sh

# echo "Creating disk.img..."
# bximage -mode=create -hd=10M -q disk.img
rm -rf build
echo "Mkdir build..."
mkdir build

# 构建一个 build 目录用于放各种生成文件
echo "Compiling..."
nasm -I include/ -o build/mbr.bin boot/mbr.S
nasm -I include/ -o build/loader.bin boot/loader.S
# 编译 print.S 和 kernel.S
nasm -f elf -o build/print.o lib/kernel/print.S
nasm -f elf -o build/kernel.o kernel/kernel.S
# 编译 main.c interrupt.c init.c
gcc -I lib/ -I kernel/ -m32 -c -fno-builtin -o build/main.o kernel/main.c
gcc -I lib/ -I kernel/ -m32 -c -fno-builtin -o build/interrupt.o kernel/interrupt.c
gcc -I lib/ -I kernel/ -m32 -c -fno-builtin -o build/init.o kernel/init.c
#链接
ld -m elf_i386 -Ttext 0xc0001500 -e main -o build/kernel.bin build/main.o build/init.o build/interrupt.o build/print.o build/kernel.o

echo "Writing mbr, loader and kernel to disk..."
dd if=build/mbr.bin of=/home/ahu/install/bochs/hd60M.img bs=512 count=1 conv=notrunc
dd if=build/loader.bin of=/home/ahu/install/bochs/hd60M.img bs=512 count=4 seek=2 conv=notrunc
dd if=build/kernel.bin of=/home/ahu/install/bochs/hd60M.img bs=512 count=200 seek=9 conv=notrunc


echo "Now start bochs and have fun!"

cd /home/ahu/install/bochs/
bin/bochs -f bochsrc.disk

