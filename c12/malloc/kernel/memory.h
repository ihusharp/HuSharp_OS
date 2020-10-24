#ifndef __KERNEL_MEMORY_H
#define __KERNEL_MEMORY_H

#include "stdint.h"
#include "bitmap.h"
#include "list.h"

// 内存池的标记，用来判断使用哪个内存池
enum pool_flags {
    PF_KERNEL = 1,  // 内核内存池
    PF_USER = 2     // 用户内存池
};

#define	 PG_P_1	  1	// 页表项或页目录项存在属性位
#define	 PG_P_0	  0	// 页表项或页目录项存在属性位
#define	 PG_RW_R  0	// R/W 属性位值, 读/执行
#define	 PG_RW_W  2	// R/W 属性位值, 读/写/执行
#define	 PG_US_S  0	// U/S 属性位值, 系统级
#define	 PG_US_U  4	// U/S 属性位值, 用户级

// 虚拟地址池，用于虚拟地址管理
struct virtual_addr{
    struct bitmap vaddr_bitmap;  // 虚拟地址用到的 bitmap 结构, 页 为单位
    uint32_t vaddr_start;        // 虚拟地址起始地址
};

// 内存块
struct mem_block {
    struct list_elem free_elem;
};

// 内存块描述符
struct mem_block_desc {
    uint32_t block_size;        // 内存块规模大小
    uint32_t blocks_per_arena;  // 本 arena 可容纳的内存块数目
    struct list free_list;      // 目前可用的 mem_vlock 采用链表
    // 注意 per_arena 表示的每一个 arena 所能容纳个数
    // 而 free_list 表示多个 arena 的内存块
};

// 16 32 64 128 256 512 1024
#define DESC_CNT    7   //内存描述符个数

extern struct pool kernel_pool, user_pool;
void mem_init(void);
void* get_kernel_pages(uint32_t pg_cnt);
void* malloc_page(enum pool_flags pf, uint32_t pg_cnt);
void malloc_init(void);
uint32_t* pte_ptr(uint32_t vaddr);
uint32_t* pde_ptr(uint32_t vaddr);
uint32_t addr_v2p(uint32_t vaddr);
void* get_a_page(enum pool_flags pf, uint32_t vaddr);
void* get_user_pages(uint32_t pg_cnt);
void block_desc_init(struct mem_block_desc* desc_array);
void* sys_malloc(uint32_t size);

#endif