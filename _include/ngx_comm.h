//
// Created by dietrich on 8/5/21.
//

#ifndef LINUX_CPP_COMM_ARCHITECTURE_NGX_COMM_H
#define LINUX_CPP_COMM_ARCHITECTURE_NGX_COMM_H

//宏定义------------------------------------
#define _PKG_MAX_LENGTH     30000  //每个包的最大长度【包头+包体】不超过这个数字

//通信 收包状态定义
#define _PKG_HD_INIT         0  //初始状态，准备接收数据包头
#define _PKG_HD_RECVING      1  //接收包头中，包头不完整，继续接收中
#define _PKG_BD_INIT         2  //包头刚好收完，准备接收包体
#define _PKG_BD_RECVING      3  //接收包体中，包体不完整，继续接收中，处理后直接回到_PKG_HD_INIT状态

#define _DATA_BUFSIZE_       20  //因为我要先收包头，我希望定义一个固定大小的数组专门用来收包头，这个数字大小一定要 >sizeof(COMM_PKG_HEADER) ,所以我这里定义为20绰绰有余；
                                 //如果日后COMM_PKG_HEADER大小变动，这个数字也要做出调整以满足 >sizeof(COMM_PKG_HEADER) 的要求

struct COMM_PKG_HEADER {
    unsigned short pkgLen;    //报文总长度【包头+包体】--2字节，2字节可以表示的最大数字为6万多，我们定义_PKG_MAX_LENGTH 30000，所以用pkgLen足够保存下
    //包头中记录着整个包【包头—+包体】的长度

    unsigned short msgCode;   //消息类型代码--2字节，用于区别每个不同的命令【不同的消息】
    int            crc32;     //CRC32效验--4字节，为了防止收发数据中出现收到内容和发送内容不一致的情况，引入这个字段做一个基本的校验用
};

#pragma pack() //取消指定对齐，恢复缺省对齐

#endif //LINUX_CPP_COMM_ARCHITECTURE_NGX_COMM_H
