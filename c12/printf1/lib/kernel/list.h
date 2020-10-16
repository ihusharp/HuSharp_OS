#ifndef __LIB_KERNEL_LIST_H
#define __LIB_KERNEL_LIST_H
#include "global.h"


// 令基址为 0 ，那么偏移量就等于 结构体中元素的偏移值
#define offset(struct_type, member) (int)(&((struct_type*)0)->member)
// 通过结构体内部指针 转换为 代表该结构体 的方法：
// 将 elem_ptr(结构体内部元素指针) 的地址减去 elem_ptr 在结构体内部的偏移量
// 从而获取所在结构体的地址，再将该地址转换为 struct 类型
// struct_member_name 为内部变量名，主要用于 offset 函数 获取结构体内偏移值
#define elem2entry(struct_type, struct_member_name, elem_ptr)   \
    (struct_type*)((int)elem_ptr - offset(struct_type, struct_member_name))
// 节点，不需要 data 域
struct list_elem {
    struct list_elem* prev;// 前驱
    struct list_elem* next;// 后继
};

// 链表结构，用来实现队列
struct list {
    struct list_elem head;// 头指针
    struct list_elem tail;// 尾指针
};

typedef bool (function)(struct list_elem*, int arg);

void list_init (struct list*);
void list_insert_before(struct list_elem* elem, struct list_elem* before);
void list_push(struct list* plist, struct list_elem* elem);
void list_iterate(struct list* plist);
void list_append(struct list* plist, struct list_elem* elem);  
void list_remove(struct list_elem* pelem);
struct list_elem* list_pop(struct list* plist);
bool list_empty(struct list* plist);
uint32_t list_len(struct list* plist);
struct list_elem* list_traversal(struct list* plist, function func, int arg);
bool elem_find(struct list* plist, struct list_elem* obj_elem);

#endif