#!/bin/sh

echo "Creating disk.img..."
bximage -mode=create -hd=10M -q disk.img

echo "Compiling..."
nasm -I include/ -o mbr.bin mbr.S
nasm -I include/ -o loader.bin loader.S

echo "Writing mbr and loader to disk..."
dd if=mbr.bin of=/home/ahu/install/bochs/hd60M.img bs=512 count=1 conv=notrunc
dd if=loader.bin of=/home/ahu/install/bochs/hd60M.img bs=512 count=4 seek=2 conv=notrunc

echo "Now start bochs and have fun!"

cd /home/ahu/install/bochs/
bin/bochs -f bochsrc.disk

