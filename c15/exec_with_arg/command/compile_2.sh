
####  此脚本应该在command目录下执行

if [[ ! -d "../lib" || ! -d "../build" ]];then
   echo "dependent dir don\`t exist!"
   cwd=$(pwd)
   cwd=${cwd##*/}
   cwd=${cwd%/}
   if [[ $cwd != "command" ]];then
      echo -e "you\`d better in command dir\n"
   fi 
   exit
fi

BIN="prog_arg"
CFLAGS="-m32 -c -fno-builtin -fno-stack-protector"
LIBS="-I ../lib -I ../lib/user -I ../fs"
OBJS="../build/string.o ../build/syscall.o \
      ../build/stdio.o ../build/assert.o start.o"
DD_IN=$BIN
DD_OUT="/home/husharp/bochs_hu/bochs/hd60M.img"

nasm -f elf ./start.S -o ./start.o
# 此处便是打包为 CRT 即 C 运行库
ar rcs simple_crt.a $OBJS start.o
gcc $CFLAGS $LIBS -o $BIN".o" $BIN".c"
ld -m elf_i386 $BIN".o" simple_crt.a -o $BIN
SEC_CNT=$(ls -l $BIN|awk '{printf("%d", ($5+511)/512)}')

if [[ -f $BIN ]];then
   dd if=./$DD_IN of=$DD_OUT bs=512 \
   count=$SEC_CNT seek=300 conv=notrunc
fi


##########   以上核心就是下面这三条命令   ##########
#x86_64-elf-gcc -m32 -Wall -c -fno-builtin -W -Wstrict-prototypes -Wmissing-prototypes -Wsystem-headers -I ../lib -o prog_no_arg.o prog_no_arg.c
#x86_64-elf-ld -melf_i386 -e main prog_no_arg.o ../build/string.o ../build/syscall.o ../build/stdio.o ../build/assert.o -o prog_no_arg
#dd if=prog_no_arg of=../../../hd60M.img  bs=512 count=10 seek=300 conv=notrunc