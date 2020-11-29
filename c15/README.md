

## 实现系统调用

   syscall_table[SYS_GETPID] = sys_getpid;

   syscall_table[SYS_WRITE] = sys_write;

   syscall_table[SYS_MALLOC] = sys_malloc;

   syscall_table[SYS_FREE] = sys_free;

   syscall_table[SYS_FORK] = sys_fork;

   syscall_table[SYS_READ] = sys_read;

   syscall_table[SYS_PUTCHAR] = sys_putchar;

   syscall_table[SYS_CLEAR]   = cls_screen;*// 位于 print.S*

   syscall_table[SYS_GETCWD]     = sys_getcwd;

   syscall_table[SYS_OPEN]       = sys_open;

   syscall_table[SYS_CLOSE]      = sys_close;

   syscall_table[SYS_LSEEK]    = sys_lseek;

   syscall_table[SYS_UNLINK]   = sys_unlink;

   syscall_table[SYS_MKDIR]    = sys_mkdir;

   syscall_table[SYS_OPENDIR]  = sys_opendir;

   syscall_table[SYS_CLOSEDIR]   = sys_closedir;

   syscall_table[SYS_CHDIR]    = sys_chdir;

   syscall_table[SYS_RMDIR]    = sys_rmdir;

   syscall_table[SYS_READDIR]  = sys_readdir;

   syscall_table[SYS_REWINDDIR]   = sys_rewinddir;

   syscall_table[SYS_STAT]  = sys_stat;

   syscall_table[SYS_PS]    = sys_ps;

   syscall_table[SYS_EXECV]    = sys_execv;





## 实现函数

uint32_t getpid(void);

uint32_t write(int32_t fd, const void* buf, uint32_t count);

void* malloc(uint32_t size);

void free(void* ptr);

int16_t fork(void);

int32_t read(int32_t fd, void* buf, uint32_t count);

void putchar(char char_asci);

void clear(void);

char* getcwd(char* buf, uint32_t size);

int32_t open(char* pathname, uint8_t flag);

int32_t close(int32_t fd);

int32_t lseek(int32_t fd, int32_t offset, uint8_t whence);

int32_t unlink(const char* pathname);

int32_t mkdir(const char* pathname);

struct dir* opendir(const char* name);

int32_t closedir(struct dir* dir);

int32_t rmdir(const char* pathname);

struct dir_entry* readdir(struct dir* dir);

void rewinddir(struct dir* dir);

int32_t stat(const char* path, struct file_attr* buf);

int32_t chdir(const char* path);

void ps(void);

int execv(const char* pathname, char** argv);







Linux 执行命令， 是 bash（或 shell ）先 fork 一个子进程， 然后调用 exec 去执行命令。（更严格的说， 这是外部命令被执行方式）