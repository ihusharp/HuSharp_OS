#ifndef __FS_FS_H
#define __FS_FS_H
#include "stdint.h"
#include "ide.h"

#define MAX_FILES_PER_PART  4096    // 每个分区所支持最大创建的文件数
#define BITS_PER_SECTOR     4096    // 每扇区的 bitmap 位数
#define SECTOR_SIZE         512     // 扇区的字节大小
#define BLOCK_SIZE SECTOR_SIZE      // 块字节大小，与 sector 一致

// 文件类型
enum file_types {
    HS_FT_UNKNOWN,  // 不支持的文件类型
    HS_FT_REGULAR,  // 普通文件
    HS_FT_DIRECTORY // 目录
};

void filesys_init();// 在磁盘上搜索文件系统,若没有则格式化分区创建文件系统 

#endif