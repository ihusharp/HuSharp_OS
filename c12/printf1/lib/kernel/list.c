#include "list.h"
#include "interrupt.h"

// 初始化双向链表
void list_init (struct list* list) {
    list->head.prev = NULL;
    list->tail.next = NULL;
    
    list->head.next = &list->tail;
    list->tail.prev = &list->head;
}

// 将链表元素 before 插入在元素 elem 前 
void list_insert_before(struct list_elem* elem, struct list_elem* before) {
    // 由于队列是公共资源，因此要保证为原子操作
    enum intr_status old_status = intr_disable();//关中断

    // 更新节点
    elem->prev->next = before;
    before->prev = elem->prev;
    elem->prev = before;
    before->next = elem;

    intr_set_status(old_status);
}

void list_append(struct list* plist, struct list_elem* elem) {
    list_insert_before(&plist->tail, elem);// 队列的FIFO
}  

void list_remove(struct list_elem* pelem) {
    enum intr_status old_status = intr_disable();

    pelem->prev->next = pelem->next;
    pelem->next->prev = pelem->prev;

    intr_set_status(old_status);// 返回原状态
}

void list_push(struct list* plist, struct list_elem* elem) {
    list_insert_before(plist->head.next, elem);
}

// pop 操作，在唤醒时进行关中断
struct list_elem* list_pop(struct list* plist) {
    struct list_elem* elem = plist->head.next;
    list_remove(elem);
    return elem;
}

// 查找元素
bool elem_find(struct list* plist, struct list_elem* obj_elem) {
    struct list_elem* elem = plist->head.next;
    while(elem != &plist->tail) {
        if(elem == obj_elem) {
            return true;
        }
        elem = elem->next;
    }
    return false;
}

// 遍历列表的所有元素，判断是否有 elem 满足条件
// 判断方法采用 func 回调函数进行判断
struct list_elem* list_traversal(struct list* plist, function func, int arg) {
    struct list_elem* elem = plist->head.next;
    // 如果队列为空 直接 return
    if(list_empty(plist)) {
        return NULL;
    }
    while(elem != &plist->tail) {
        if(func(elem, arg)) {
            return elem;
        }
        elem = elem->next;
    }
    return NULL;
}

// 判断空
bool list_empty(struct list* plist) {
    return (plist->head.next == &plist->tail ? true : false);
}

uint32_t list_len(struct list* plist) {
    struct list_elem* elem = plist->head.next;
    uint32_t len = 0;
    while(elem != &plist->tail) {
        elem = elem->next;
        len++;
    }
    return len;
}


