#ifndef __FS_INODE_H
#define __FS_INODE_H

#include "stdint.h"
#include "list.h"

// inode 结构
struct inode {
    // 用于表示此节点在 inode 数组中的位置
    uint32_t i_no;  // inode编号

    // 当此inode是文件时,i_size是指文件大小,
    // 若此inode是目录,i_size是指该目录下所有目录项大小之和
    uint32_t i_size;

    uint32_t i_open_cnts;   // 记录此文件被打开次数，回收时需要查看
    bool write_deny;    // 此时是否打开 写文件不能并行进行

    // 由于 HuSharp 系统采用 数据块大小 = 扇区大小
    // 因此直接用 sectors 来表示索引块
    uint32_t i_sectors[13];
    struct list_elem inode_tag;     // 记录到已打开文件中，用于缓存
};

#endif