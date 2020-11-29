#include "string.h"
#include "global.h"
// #include "debug.h"
#include "assert.h"

/* 进行初始化  将dst_起始的size个字节置为value */
// 通常用于内存分配时的数据清 0 
void memset(void* dst_, uint8_t value, uint32_t size) {
    assert(dst_ != NULL);
    uint8_t* dst = (uint8_t*)dst_;
    while (size-- > 0){
        *dst++ = value;
    }
}

/* 将src_起始的size个字节复制到dst_ */
void memcpy(void* dst_, const void* src_, uint32_t size) {
    assert(dst_ != NULL && src_ != NULL);
    uint8_t* dst = dst_;
    const uint8_t* src = src_;
    while(size-- > 0) {
        *dst++ = *src++;
    }
}

/* 连续比较以地址a_和地址b_开头的size个字节,
    若相等则返回0,若a_大于b_返回+1,否则返回-1 */
int memcmp(const void* a_, const void* b_, uint32_t size) {
    const char* a = a_;
    const char* b = b_;
    assert(a != NULL && b != NULL);
    while(size-- > 0) {
        if(*a != *b) {
            return *a > *b ? 1 : -1;
        }
        a++;
        b++;
    }
    // 至此说明相等
    return 0;
}

/* 将字符串从src_复制到dst_ */
char* strcpy(char* dst_, const char* src_) {
    assert(dst_ != NULL && src_ != NULL);
    char* temp = dst_;  // 暂存地址
    // 以 字符串结尾 '0' 作为终止条件
    while((*dst_++ = *src_++));
    return temp;
}


/* 返回字符串长度 */
uint32_t strlen(const char* str) {
    assert(str != NULL);
    const char* end = str;// 标记结尾处位置
    // 和strcpy 函数一样，遇到 '0'停止
    while(*end++);
    return (end - str - 1);
}
/* 比较两个字符串,若a_中的字符大于b_中的字符返回1,相等时返回0,否则返回-1. */
int8_t strcmp (const char* a, const char* b) {
    assert(a != NULL && b != NULL);
    while(*a != 0 && *a == *b) {
        a++;
        b++;
    }
    // 如果 < 就返回 -1
    // 如果 >= 就前往 布尔式
    // 该布尔式表示，要是 > 就 返回 1, 否则只可能是 = ，布尔值刚好是 0
    return *a < *b ? -1 : *a > *b; 
}

/* 从左到右查找字符串str中首次出现字符ch的地址(不是下标,是地址) */
char* strchr(const char* str, const uint8_t ch) {
    assert(str != NULL);
    while(*str != 0) {
        if(*str == ch) {
            return (char*) str;// 强制转换
        }
        str++;
    }
    return NULL;
}

/* 从右往左查找字符串str中首次出现字符ch的地址(不是下标,是地址) */
char* strrchr(const char* str, const uint8_t ch) {
    assert(str != NULL);
    const char* last = NULL;
    // 从左往右遍历，将 last 更新即可，这样便不用手动判断 '0'
    while(*str != 0) {
        if(*str == ch) {
            last = str;
        }
        str++;
    }
    return (char*)last;
}

/* 将字符串src_拼接到dst_后,将回拼接的串地址 */
char* strcat(char* dst_, const char* src_) {
    assert(dst_ != NULL && src_ != NULL);
    char* str = dst_;
    while(*str++);// 循环到末尾
    --str;//得到拼接位置
    while((*str++ = *src_++));
    return dst_;
}

/* 在字符串str中查找指定字符ch出现的次数 */
uint32_t strchrs(const char* str, uint8_t ch) {
    assert(str != NULL);
    uint32_t cnt = 0;
    const char* temp = str;
    while(*temp != 0) {
        if(*temp == ch) {
            cnt++;
        }
        temp++;
    }
    return cnt;
}