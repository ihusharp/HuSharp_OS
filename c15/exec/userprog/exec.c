#include "exec.h"
#include "fs.h"
#include "global.h"
#include "memory.h"
#include "stdio-kernel.h"
#include "string.h"
#include "thread.h"

int32_t sys_execv(const char* path, const char* argv[]);

typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;
#define EI_NIDENT 16 // 身份码标识
// 32 位的 ELF 头
struct Elf32_Ehdr {
    unsigned char e_ident[EI_NIDENT];
    Elf32_Half e_type;
    Elf32_Half e_machine;
    Elf32_Word e_version;
    Elf32_Addr e_entry;
    Elf32_Off e_phoff;
    Elf32_Off e_shoff;
    Elf32_Word e_flags;
    Elf32_Half e_ehsize;
    Elf32_Half e_phentsize;
    Elf32_Half e_phnum;
    Elf32_Half e_shentsize;
    Elf32_Half e_shnum;
    Elf32_Half e_shstrndx;
};

// 程序头表 
struct Elf32_Phdr {
    Elf32_Word p_type; // 见下面的enum segment_type
    Elf32_Off p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;// 本段在文件中的大小
    Elf32_Word p_memsz;// 本段在内存中的大小
    Elf32_Word p_flags;
    Elf32_Word p_align;
};


// 段类型 
enum segment_type {
   PT_NULL,            // 忽略
   PT_LOAD,            // 可加载程序段
   PT_DYNAMIC,         // 动态加载信息 
   PT_INTERP,          // 动态加载器名称
   PT_NOTE,            // 一些辅助信息
   PT_SHLIB,           // 保留
   PT_PHDR             // 程序头表
};

/***************** exec 函数的实现 ********************************/
// exec 是以一个可执行文件的绝对路径作为参数， 把当前正在运行的用户进程的进程体：代码段 数据段等
// 用该可执行文件的进程体进行替换，从而实现了新进程的执行

// 将文件描述符fd指向的文件中,偏移为offset,大小为filesz的段加载到虚拟地址为vaddr的内存 
static bool segment_load(int32_t fd, uint32_t offset, uint32_t filesz, uint32_t vaddr) {
    // 由于一般来说， 段的起始地址一般都不是 0xXXXXX000, 而是页框中某个中间地址
    uint32_t vaddr_first_page= vaddr & 0xfffff000;// vaddr地址所在的页框
    // 因此 需要记录 加载到内存后,文件在第一个页框中占用的字节大小
    uint32_t size_in_first_page = PG_SIZE - (vaddr & 0x00000fff);

    // 若第一个页框容不下该段， 那么申请新的段
    uint32_t add_pages = 0;
    if(filesz > size_in_first_page) {
        uint32_t left_size = filesz - size_in_first_page;
        // 需要分配页框
        add_pages = DIV_ROUND_UP(left_size / PG_SIZE); 
    } else {
        add_pages = 0;
    }

    // 为进程分配内存
    // 由于 exec 是在老进程基础上覆盖，那么对与老进程已经分配了的内存，并不需要重新分配，
    // 只需要分配新的
    // 因此 每次需要进行判断
    uint32_t page_idx = 0;
    uint32_t vaddr_page = vaddr_first_page;
    while(page_idx < add_pages + 1) {// +1 是指最开始的那个页框
        uint32_t* pde = pde_ptr(vaddr_page);
        uint32_t* pte = pte_ptr(vaddr_page);

        /* 如果 pde 不存在,或者 pte 不存在就分配内存.
         * pde 的判断要在 pte 之前,否则 pde 若不存在会导致
         * 判断 pte 时缺页异常 */
        if (!(*pde & 0x00000001) || !(*pte & 0x00000001)) {// 存在位
            // 说明还未分配
            if (get_a_page(PF_USER, vaddr_page) == NULL) {
                return false;
            }
        } // 如果原进程的页表已经分配了,利用现有的物理页,直接覆盖进程体
        vaddr_page += PG_SIZE;
        page_idx++;
    }// 至此 内存分配已经完成
    // 下面开始从文件系统中加载用户进程到刚刚分配好的内存中
    /* enum whence {
    SEEK_SET = 1,// 文件开始处
    SEEK_CUR,
    SEEK_END // 文件最后字节的下一个字节， 这就是 读完文件 显示 EOF 的原因
    }; */
    sys_lseek(fd, offset, SEEK_SET);
    sys_read(fd, (void*)vaddr, filesz);// 开始读入到 vaddr 中
    return true;
}


// 从文件系统上加载用户程序pathname,成功则返回程序的起始地址,否则返回-1
static int32_t load(const char* pathname) {
    int32_t ret = -1;
    struct Elf32_Ehdr elf_header;
    struct Elf32_Phdr prog_header;
    memset(&elf_header, 0, sizeof(struct Elf32_Ehdr));

    // 首先打开文件
    int32_t fd = sys_open(pathname, OP_RONLY);
    if (fd == -1) {
        return -1;
    }

    // 读取 elf 头
    if(sys_read(fd, &elf_header, sizeof(struct Elf32_Ehdr) != sizeof(struct Elf32_Ehdr)) {
        ret = -1;
        return ret;
    }

    // 校验 elf 头
    // e_ident 为 0x7f ELF  由于 E 属于 16 进制，因此不用进行 /x 标识
    // 因为 会识别为 \x7fE 'LF'， 因此采用 \177 即 8 进制的方式
    if (memcmp(elf_header.e_ident, "\177ELF\1\1\1", 7)
        || elf_header.e_type != 2// type 2 = 64 位
        || elf_header.e_machine != 3// LSB 还是 MSB
        || elf_header.e_version != 1// 版本号
        || elf_header.e_phnum > 1024// 条目数量，即段的数目
        || elf_header.e_phentsize != sizeof(struct Elf32_Phdr)) {// 段大小
        ret = -1;
        goto done;
    }

    // 下面对所有 段进行处理
    uint32_t prog_idx = 0;
    Elf32_Half prog_header_size = elf_header.e_phentsize;// 段大小
    Elf32_Off prog_header_offset = elf_header.e_phoff;// 段头
    while(prog_idx < elf_header.e_phnum) {
        memset(&prog_header, 0, prog_header_size);

        // 将文件的指针定位到程序头
        sys_lseek(fd, prog_header_offset, SEEK_SET);

        if(sys_read(fd, &prog_header, prog_header_size) != prog_header_size) {
            ret = -1;
            goto done;
        }

        // 判断是否为可加载段
        if(PT_LOAD == prog_header.p_type) {
            if(!segment_load(fd, prog_header.p_offset, prog_header.p_filesz)) {
                ret = -1;
                goto done;
            }
        }// 至此 说明加载成功
        // 继续下一个
        prog_header_offset += elf_header.e_phentsize;
        prog_idx++;
    }
    ret = elf_header.e_entry;

done:
    sys_close(fd);
    return ret;
}



//  用path指向的程序替换当前进程 
int32_t sys_execv(const char* path, const char* argv[]) {
    uint32_t argc = 0;
    // 统计参数
    while(argv[argc]) {
        argc++;
    }

    int32_t entry_point = load(path);
    if(entry_point == -1) {//加载失败
        return -1;
    }

    struct task_struct* cur = running_thread();// 先更改目前进程属性，方便 ps 看到
    memcpy(cur->name, path, TASK_NAME_LEN);// #define TASK_NAME_LEN 16 即文件名
    cur->name[TASK_NAME_LEN - 1] = 0;// 终止

    // 由于要利用 内核栈进行 intr_exit 中断返回， 因此需要修改内核栈中数据
    struct intr_stack* intr_0_stack = (struct intr_stack*)((uint32_t)cur + PG_SIZE - sizeof(struct intr_stack));
    // 参数传递给用户进程
    intr_0_stack->ebx = (int32_t)argv;// 放在哪都可以， 自己记得调用就行
    intr_0_stack->ecx = argc;
    intr_0_stack->eip = (void*)entry_point;
    /* 使新用户进程的栈地址为最高用户空间地址 */
    // 因为老进程的数据没用了， 直接忽略即可
    intr_0_stack->esp = (void*)0xc0000000;

    // 假装中断返回
    // exec不同于fork,为使新进程更快被执行,直接从中断返回
    asm volatile("movl %0, %%esp; jmp intr_exit"
                :
                : "g"(intr_0_stack)
                : "memory");

    return 0;// 并不会执行到此处， 但是为了代码规范
}