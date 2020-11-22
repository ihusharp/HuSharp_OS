#include "keyboard.h"
#include "global.h"
#include "interrupt.h"
#include "io.h"
#include "print.h"
#include "ioqueue.h"

#define KBD_BUF_PORT 0x60 // 键盘 buffer 寄存器端口号为 0x60

// 字符分为操作控制键 和 字符控制键
// 用转义字符定义部分 字符控制键
#define esc '\033' // 采用 8 进制表示，c89 后 才出现 16 进制
#define backspace '\b'
#define tab '\t'
#define enter '\r'
#define delete '\177'

// 以下字符 的 0 为占位
#define char_invisible 0
#define ctrl_l_char char_invisible
#define ctrl_r_char char_invisible
#define shift_l_char char_invisible
#define shift_r_char char_invisible
#define alt_l_char char_invisible
#define alt_r_char char_invisible
#define caps_lock_char char_invisible

// 操作控制键的 make
// 操作控制键 与 其他键一起构成组合键
#define shift_l_make 0x2a
#define shift_r_make 0x36
#define alt_l_make 0x38
#define alt_r_make 0xe038
#define alt_r_break 0xe0b8
#define ctrl_l_make 0x1d
#define ctrl_r_make 0xe01d
#define ctrl_r_break 0xe09d
#define caps_lock_make 0x3a

struct ioqueue kbd_buf;	   // 定义键盘缓冲区

/* 定义以下变量记录相应键是否按下的状态,作为全局变量
 * ext_scancode用于记录 makecode 是否以0xe0开头 */
static bool ctrl_status, shift_status, alt_status, caps_lock_status,
    ext_scancode;

// makecode 作为数组索引
static char keymap[][2] = {
    /* 扫描码  未与shift组合  与shift组合*/
    /* ---------------------------------- */
    /* 0x00 */ { 0, 0 }, // 没有 makecode 为 0 的键
    /* 0x01 */ { esc, esc },
    /* 0x02 */ { '1', '!' },
    /* 0x03 */ { '2', '@' },
    /* 0x04 */ { '3', '#' },
    /* 0x05 */ { '4', '$' },
    /* 0x06 */ { '5', '%' },
    /* 0x07 */ { '6', '^' },
    /* 0x08 */ { '7', '&' },
    /* 0x09 */ { '8', '*' },
    /* 0x0A */ { '9', '(' },
    /* 0x0B */ { '0', ')' },
    /* 0x0C */ { '-', '_' },
    /* 0x0D */ { '=', '+' },
    /* 0x0E */ { backspace, backspace },
    /* 0x0F */ { tab, tab },
    /* 0x10 */ { 'q', 'Q' },
    /* 0x11 */ { 'w', 'W' },
    /* 0x12 */ { 'e', 'E' },
    /* 0x13 */ { 'r', 'R' },
    /* 0x14 */ { 't', 'T' },
    /* 0x15 */ { 'y', 'Y' },
    /* 0x16 */ { 'u', 'U' },
    /* 0x17 */ { 'i', 'I' },
    /* 0x18 */ { 'o', 'O' },
    /* 0x19 */ { 'p', 'P' },
    /* 0x1A */ { '[', '{' },
    /* 0x1B */ { ']', '}' },
    /* 0x1C */ { enter, enter },
    /* 0x1D */ { ctrl_l_char, ctrl_l_char },
    /* 0x1E */ { 'a', 'A' },
    /* 0x1F */ { 's', 'S' },
    /* 0x20 */ { 'd', 'D' },
    /* 0x21 */ { 'f', 'F' },
    /* 0x22 */ { 'g', 'G' },
    /* 0x23 */ { 'h', 'H' },
    /* 0x24 */ { 'j', 'J' },
    /* 0x25 */ { 'k', 'K' },
    /* 0x26 */ { 'l', 'L' },
    /* 0x27 */ { ';', ':' },
    /* 0x28 */ { '\'', '"' },
    /* 0x29 */ { '`', '~' },
    /* 0x2A */ { shift_l_char, shift_l_char },
    /* 0x2B */ { '\\', '|' },
    /* 0x2C */ { 'z', 'Z' },
    /* 0x2D */ { 'x', 'X' },
    /* 0x2E */ { 'c', 'C' },
    /* 0x2F */ { 'v', 'V' },
    /* 0x30 */ { 'b', 'B' },
    /* 0x31 */ { 'n', 'N' },
    /* 0x32 */ { 'm', 'M' },
    /* 0x33 */ { ',', '<' },
    /* 0x34 */ { '.', '>' },
    /* 0x35 */ { '/', '?' },
    /* 0x36	*/ { shift_r_char, shift_r_char },
    /* 0x37 */ { '*', '*' },
    /* 0x38 */ { alt_l_char, alt_l_char },
    /* 0x39 */ { ' ', ' ' },
    /* 0x3A */ { caps_lock_char, caps_lock_char }
    /*其它按键暂不处理*/
};

// 键盘中断程序
static void intr_keyboard_handler(void)
{
    // 中断处理程序始终只能处理一个字节，因此对于组合键需要记录下来
    // 判断这次中断发生前的上一次中断，以下任意三个键是否有按下的
    bool ctrl_down_last = ctrl_status; // true 表示被按下且还未被松开
    bool shift_down_last = shift_status;
    bool caps_lock_last = caps_lock_status;

    bool break_code; // 断码
    uint16_t scancode = inb(KBD_BUF_PORT); // 从端口中获取扫描码

    // 由于目前只支持主键盘区的键，因此 只有一个 / 为 0xe0, 还有 backspace
    // 表示此键长按后，会产生多个扫描码
    if (scancode == 0xe0) {
        ext_scancode = true; // 打开 e0 标记
        return;
    }

    // 进行 0xe0 的多个扫描码合并
    if (ext_scancode) {
        scancode = ((0xe000) | scancode);
        ext_scancode = false; // 合并后就关闭
    }

    break_code = ((scancode & 0X0080) != 0);
    // 现在需要判断是 make 还是 break, 即 此时是按键 还是键弹起
    if (break_code) {
        // 进入断码处理
        // 由于现在是断码 即 scancode 为 断码，而断码和通码的区别在于第 8 位
        uint16_t make_code = (scancode &= 0xff7f); // 通码第 8 位 为0

        // 为什么需要得到该断码的通码？
        // 可以通过该得到的通码判断若是任意以下三个键弹起了,将状态置为false
        if (make_code == ctrl_l_make || make_code == ctrl_r_make) {
            ctrl_status = false;
        } else if (make_code == shift_l_make || make_code == shift_r_make) {
            shift_status = false;
        } else if (make_code == alt_l_make || make_code == alt_r_make) {
            alt_status = false;
        }
        // 置为 false 是为了之后的组合键
        return;
    } // 至此 则表示为通码，主要作用为判断 shift 和 caps 组合 
    else if((scancode > 0x00 && scancode < 0x3b) || \
                (scancode == alt_r_make) || \
                (scancode == ctrl_r_make) ) {// 这是由于将来要实现 ctrl 和 alt 键
                                            // ctrl-r alt-r 并不位于 0-3b中
            bool shift = false; //组合键
            // 开始将扫描码转化为字符
            // 先处理数字键(代指双字符键)
            /****** 代表两个字母的键 ********
            0x0e 数字'0'~'9',字符'-',字符'='
            0x29 字符'`'
            0x1a 字符'['
            0x1b 字符']'
            0x2b 字符'\\'
            0x27 字符';'
            0x28 字符'\''
            0x33 字符','
            0x34 字符'.'
            0x35 字符'/'
            */
            // caps 对数字键 无效
            if ((scancode < 0x0e) || (scancode == 0x29) || \
                (scancode == 0x1a) || (scancode == 0x1b) || \
                (scancode == 0x2b) || (scancode == 0x27) || \
                (scancode == 0x28) || (scancode == 0x33) || \
                (scancode == 0x34) || (scancode == 0x35)) {
                // 主要探讨 数字键 和 字母键，在面对 shift 或 caps 时，两种键不同
                // 由于之前的映射数组中，0 为 变化前， 1 为变化后，实则就是选择哪个元素
                if(shift_down_last) {//按下了 shift 需要讨论字母键和数字键
                    shift = true;
                } 
            } else {// 为字母键  若shift 和 caps 同时按住 
                    // --->将两个键转化为 shift 是否按下
                if(shift_down_last && caps_lock_last) {
                    // 那么结果相当于抵消
                    shift = false;
                } else if(shift_down_last || caps_lock_last) {
                    shift = true;
                } else {
                    shift = false;
                }
            }
            uint8_t index = (scancode &= 0x00ff);//将高字节置为 0，主要针对 0xe0
            char cur_char = keymap[index][shift];// false 表示 0
            // 只输出 ascii 码 不为 0 的字符
            if(cur_char != 0) {

            /*****************  快捷键ctrl+l和ctrl+u的处理 *********************
             * 下面是把ctrl+l和ctrl+u这两种组合键产生的字符置为:
             * cur_char的asc码-字符a的asc码, 此差值比较小,
             * 属于asc码表中不可见的字符部分.故不会产生可见字符.
             * 我们在shell中将ascii值为l-a和u-a的分别处理为清屏和删除输入的快捷键*/
                if ((ctrl_down_last && cur_char == 'l') || (ctrl_down_last && cur_char == 'u')) {
                    cur_char -= 'a';
                }

            /* 若 kbd_buf 中未满并且待加入的cur_char不为0,
             * 则将其加入到缓冲区kbd_buf中 */
                if(!ioq_full(&kbd_buf)) {
                    //!!!!! 在实现 shell 之后将此处取消
                    // put_char(cur_char);// 临时存放
                    ioq_putchar(&kbd_buf, cur_char);
                }
                return;
            }

            // 至此表示按键 ascii 为 0 ，而此时我们实现的字符中，ascii为0 的只有如下几种情况
            /* 记录本次是否按下了下面几类控制键之一,供下次键入时判断组合键 */
            if (scancode == ctrl_l_make || scancode == ctrl_r_make) {
                ctrl_status = true;
            } else if (scancode == shift_l_make || scancode == shift_r_make) {
                shift_status = true;
            } else if (scancode == alt_l_make || scancode == alt_r_make) {
                alt_status = true;
            } else if (scancode == caps_lock_make) {
            /* caps 和 ctrl alt shift不同，他的终止需要一开一关，而不是按下弹出即可
             * 不管之前是否有按下caps_lock键,当再次按下时则状态取反,
             * 即:已经开启时,再按下同样的键是关闭。关闭时按下表示开启。*/
                caps_lock_status = !caps_lock_status;
            } else {
                put_str("unknown key!\n");
            }
    } 
    return;
    /*     // 输出 a
      // put_char('a');
      // 必须读取输出缓冲区寄存器，否则 8042 不再响应键盘中断
      // inb(KBD_BUF_PORT);
    uint8_t scancode = inb(KBD_BUF_PORT);
    put_int(scancode);
      return; */
}

// 键盘初始化
void keyboard_init()
{
    put_str("keyboard init start\n");
    ioqueue_init(&kbd_buf); //初始化键盘缓冲区
    register_handler(0x21, intr_keyboard_handler);
    put_str("keyboard init done\n");
}