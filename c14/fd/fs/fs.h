#ifndef __FS_FS_H
#define __FS_FS_H
#include "stdint.h"
#include "ide.h"

#define MAX_FILES_PER_PART  4096    // 每个分区所支持最大创建的文件数
#define BITS_PER_SECTOR     4096    // 每扇区的 bitmap 位数
#define SECTOR_SIZE         512     // 扇区的字节大小
#define BLOCK_SIZE SECTOR_SIZE      // 块字节大小，与 sector 一致

#define MAX_PATH_LEN 512	    // 路径最大长度

// 文件类型
enum file_types {
    HS_FT_UNKNOWN,  // 不支持的文件类型
    HS_FT_REGULAR,  // 普通文件
    HS_FT_DIRECTORY // 目录
};

// 打开文件的选项
enum open_flags {
    OP_RONLY, // 只读
    OP_WONLY, // 只写
    OP_RDWR,  // 读写
    OP_CREAT = 4  // 创建
};

// 用来记录查找文件过程中已找到的上级路径,也就是查找文件过程中"走过的地方"
struct path_search_record {
    // 若查找到 /a/b/c 的 c 时， 那么就存储 /a/b
    char searched_path[MAX_PATH_LEN];   // 查找过程中的父路径
    struct dir* parent_dir;             // 文件或目录所在的直接父目录
    enum file_types file_type;          // 判断查找到是普通文件还是目录， 找不到便置为 未知类
};


void filesys_init(void);// 在磁盘上搜索文件系统,若没有则格式化分区创建文件系统 
extern struct partition* cur_part;
int32_t path_depth_cnt(char* pathname);
int32_t sys_open(const char* pathname, uint8_t flags);

#endif