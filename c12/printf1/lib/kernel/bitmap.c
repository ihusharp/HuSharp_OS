#include "bitmap.h"
#include "stdint.h"
#include "string.h"
#include "print.h"
#include "interrupt.h"
#include "debug.h"

// bitmap 的初始化
void bitmap_init(struct bitmap* btmp) {
    memset(btmp->bits, 0, btmp->btmp_bytes_len);
}

// 判断 btmp 为指针处，bit_idx 位是否为 1，为 1 就返回 true
bool bitmap_scan_test(struct bitmap* btmp, uint32_t bit_idx) {
    uint32_t byte_idx = bit_idx / 8;//用于标注字节
    uint32_t bit_odd = bit_idx % 8; //用于标注字节内的位
    return (btmp->bits[byte_idx] & (BITMAP_MASK << bit_odd));
}

// 在位图中申请连续cnt个位,成功则返回其起始位下标，失败返回-1
int bitmap_scan(struct bitmap* btmp, uint32_t cnt) {
    uint32_t idx_byte = 0; // 用于记录空闲位所在的字节
    // 先逐字节比较，蛮力法
    // 0 表示空闲，若停止，则说明至少有一个空闲位
    while(( 0xff == btmp->bits[idx_byte]) && (idx_byte < btmp->btmp_bytes_len)) {
        idx_byte++;
    }   
    ASSERT(idx_byte < btmp->btmp_bytes_len);// 断言此时在内部
    if(idx_byte == btmp->btmp_bytes_len) {
        return -1;// 访问失败，返回 -1
    }
    
    /* 若在位图数组范围内的某字节内找到了空闲位，
     * 在该字节内逐位比对,返回空闲位的索引。
     */
    int idx_bit = 0;//字节内部位数
    while((uint8_t)(BITMAP_MASK << idx_bit) & btmp->bits[idx_byte]) {
        idx_bit++;
    }// 找到空闲位
    int bit_idx_start = idx_byte * 8 + idx_bit; // 空闲位在bitmap 下标
    if(cnt == 1) {
        return bit_idx_start;
    }

    // 至此说明 cnt > 1
    // 因此首先进行剩余位数的判断，以免超过bitmap记录数
    uint32_t bit_left = (btmp->btmp_bytes_len * 8 - bit_idx_start);
    uint32_t idx_next_bit = bit_idx_start + 1;
    uint32_t free_bit_cnt = 1;// 一定要记得记录一下 之前判断后 还有个空闲位

    bit_idx_start = -1; // 先将其置为 -1，若找不到连续的位就直接返回
    while(bit_left-- > 0) {
        // 调用 scan_test 函数，为 0 则为 false  
        if(!(bitmap_scan_test(btmp, idx_next_bit))) {
            free_bit_cnt++;
        } else { // 由于必须是连续的，因此只要遇到 不空闲的 位 便将记录cnt置为 0
            free_bit_cnt = 0;
        }
        // 若是已经满足空闲数
        if(free_bit_cnt == cnt) {
            bit_idx_start = idx_next_bit - cnt + 1;
            break;
        }
        idx_next_bit++;// 继续判断下一位
    }

    return bit_idx_start;
}

// 将位图btmp的bit_idx位设置为value
void bitmap_set(struct bitmap* btmp, uint32_t bit_idx, int8_t value) {
    ASSERT((value ==0) || (value == 1));// 只能赋予 0 或 1
    uint32_t byte_idx = bit_idx / 8; //字节位
    uint32_t bit_odd = bit_idx % 8; //位数 位

    if(value) {
        btmp->bits[byte_idx] |= (BITMAP_MASK << bit_odd);
    } else {
        btmp->bits[byte_idx] &= ~(BITMAP_MASK << bit_odd);
    }
    
}
