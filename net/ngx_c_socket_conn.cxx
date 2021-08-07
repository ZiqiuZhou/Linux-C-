//
// 和网络 中 连接/连接池 有关的函数放这里
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>    //uintptr_t
#include <stdarg.h>    //va_start....
#include <unistd.h>    //STDERR_FILENO等
#include <sys/time.h>  //gettimeofday
#include <time.h>      //localtime_r
#include <fcntl.h>     //open
#include <errno.h>
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"
#include "ngx_c_memory.h"

//从连接池中获取一个空闲连接,当一个客户端连接TCP进入，把这个连接和连接池中的一个连接【对象】绑到一起
std::shared_ptr<ngx_connection_poll> CSocket::ngx_get_connection(int sockfd) {
    if (p_free_connections.empty()) {
        //系统应该控制连接数量，防止空闲连接被耗尽，能走到这里，都不正常
        ngx_log_stderr(0,"CSocekt::ngx_get_connection()中空闲链表为空,这不应该!");
        return nullptr;
    }

    auto conn = p_free_connections.front(); //空闲连接链表头
    p_free_connections.pop_front();
    --m_free_connection_n; //空闲连接少1

    auto instance_temp = conn->instance;
    auto iCurrsequence_temp = conn->iCurrsequence;

    //(2)把以往有用的数据搞出来后，清空并给适当值
    memset(conn.get(), 0, sizeof(ngx_connection_poll));
    conn->connfd = sockfd;
    conn->curStat = _PKG_HD_INIT;  //收包状态处于 初始状态，准备接收数据包头
    conn->precvbuf = conn->dataHeadInfo; //收包我要先收到这里来，因为我要先收包头，所以收数据的buff直接就是dataHeadInfo
    conn->irecvlen = sizeof(COMM_PKG_HEADER); //这里指定收数据的长度，这里先要求收包头这么长字节的数据
    conn->ifnewrecvMem = false;                  //标记并没有new内存，所以不用释放
    conn->pnewMemPointer = nullptr;                 //既然没new内存，那自然指向的内存地址先给NULL

    conn->instance = !instance_temp;
    conn->iCurrsequence = ++iCurrsequence_temp; //每次取用该值都增加1

    return conn;
}

// 归还参数conn所代表的连接到到连接池中
void CSocket::ngx_free_connection(std::shared_ptr<ngx_connection_poll>& conn) {
    if(conn->ifnewrecvMem == true) {
        //我们曾经给这个连接分配过内存，则要释放内存
        CMemory::GetInstance()->FreeMemory(conn->pnewMemPointer);
        conn->pnewMemPointer = nullptr;
        conn->ifnewrecvMem = false;  //这行有用？
    }
    ++conn->iCurrsequence;
    ++m_free_connection_n;
    conn->connfd = -1;
    p_free_connections.push_front(std::move(conn));
    return ;
}

//用户连入，我们accept4()时，得到的socket在处理中产生失败，则资源用这个函数释放【因为这里涉及到好几个要释放的资源，所以写成函数】
void CSocket::ngx_close_connection(std::shared_ptr<ngx_connection_poll>& conn) {
    int fd = conn->connfd;
    ngx_free_connection(conn);
    if(close(fd) == -1) {
        ngx_log_error_core(NGX_LOG_ALERT,errno,"CSocekt::ngx_close_accepted_connection()中close(%d)失败!",fd);
    }
    return ;
}