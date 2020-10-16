#include "stdio.h"
#include "interrupt.h"
#include "global.h"
#include "string.h"
#include "syscall.h"
#include "print.h"
#include "console.h"

// 参数 ap 是指向可变参数的指针变量
// 将 ap 指向第一个固定参数 v ---->eg printf 指向 format参数
// typedef char* va_list;
#define va_start(ap, v) ap = (va_list)&v
// ap 指向下一个参数，并返回其值  t 为参数类型
// 参数存储在 栈中，因此需要 +4 ，由于是返回其值，因此为 *
#define va_arg(ap, t)   *((t*)(ap += 4))   
// 清空 ap
#define va_end(ap)      ap = NULL


// 将整型转换成字符(integer to ascii)
// value：待转换整数   
// buf_ptr_addr:保存转换结果的缓冲区指针的地址
// base：进制 如 0x 0d
// 作用：数制转换
static void itoa(uint32_t value, char** buf_ptr_addr, uint8_t base) {
    uint32_t m = value % base;  // 求模，最先求出为 最低位
    uint32_t i = value / base;  // 取整
    if (i) {// 递归
        // 采用递归是由于：m 虽然为最先求出的最低位，但是按照我们的读写习惯
        // 最低位应该放置最右端，因此将最高位放到递归最上层，先输出
        itoa(i, buf_ptr_addr, base);
    }
    if (m < 10) {   // 如果余数为 0-9
        *((*buf_ptr_addr)++) = m + '0';// 转换 0-9 为 '0'-'9'
    } else {// 余数为 A-F
        *((*buf_ptr_addr)++) = m - 10 + 'A';
    }
}


// 将参数ap按照格式 format 输出到字符串 str,并返回替换后 str 长度 
uint32_t vsprintf(char* str, const char* format, va_list ap) {
    char* buf_ptr = str;// 备份原 str 指针
    const char* index_ptr = format;// format指针的备份
    char index_char = *index_ptr;// 用于遍历 format 字符串的值
    int32_t arg_int;// 存储 % 代指的参数 
    char* arg_str;// 指示 %s 的字符串

    while (index_char) {//结束条件为 '/0'，即遍历到结尾
        if (index_char != '%') {
            *(buf_ptr++) = index_char;
            index_char = *(++index_ptr);
            continue;
        }
        // 至此 说明出现替代 %
        index_char = *(++index_ptr);// 首先先得到后面的字符
        switch (index_char)
        {
        case 'x':// 16位
            arg_int = va_arg(ap, int);
            itoa(arg_int, &buf_ptr, 16);
            index_char = *(++index_ptr);//指向下一个
            break;
        case 's':
            arg_str = va_arg(ap, char*);
            // 复制到缓存区
            strcpy(buf_ptr, arg_str);
            // 跨过打印字符串
            buf_ptr += strlen(arg_str);
            index_char = *(++index_ptr);// 跳过格式字符并更新index_char
            break;
        case 'c':
            *(buf_ptr++) = va_arg(ap, char);
            index_char = *(++index_ptr);
            break;
        case 'd':
            arg_int = va_arg(ap, int);
            // 若为负数，那么转换为正数后，将其前面加一个 '-'号
            if (arg_int < 0) {
                // -1 : 0 - 1 = 11111111
                arg_int = 0 - arg_int;//换为相反数
                *(buf_ptr++) = '-';
            }
            itoa(arg_int, &buf_ptr, 10);
            index_char = *(++index_ptr);
            break;
        }
    }
    return strlen(str);

}

// 格式化输出字符串format
uint32_t printf(const char* format, ...) {// ... 表示可变参数
    va_list args;//即 ap
    va_start(args, format);// 使 args 指向 format
    char buf[1024] = {0};   // 用于存储 vsprintf 返回结果
    vsprintf(buf, format, args);
    va_end(args);
    // console_put_str("aaaa!");
    return write(buf);
}
