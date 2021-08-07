//
// 和网络 中 接受连接【accept】 有关的函数放这里
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
#include <errno.h>     //errno
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>
#include <memory>

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"

//建立新连接专用函数，当新连接进入时，本函数会被ngx_epoll_process_events()所调用
void CSocket::ngx_event_accept(std::shared_ptr<ngx_connection_poll>& conn) {
    struct sockaddr    mysockaddr;        //远端服务器的socket地址
    socklen_t          socklen;
    int                err;
    int                level;
    int                connfd; // accept返回socket用来与客户端通信
    static int         use_accept4 = 1;
    std::shared_ptr<ngx_connection_poll> new_conn;

    socklen = sizeof(mysockaddr);

    // accept为非阻塞，accept会立即返回，所以需要while循环不断调用
    while (1) {
        if(use_accept4) {
            connfd = accept4(conn->connfd, &mysockaddr, &socklen, SOCK_NONBLOCK); //从内核获取一个用户端连接，最后一个参数SOCK_NONBLOCK表示返回一个非阻塞的socket，节省一次ioctl【设置为非阻塞】调用
        } else {
            connfd = accept(conn->connfd, &mysockaddr, &socklen);
        }

        if (connfd == -1) {
            err = errno;

            //对accept、send和recv而言，事件未发生时errno通常被设置成EAGAIN（意为“再来一次”）或者EWOULDBLOCK（意为“期待阻塞”）
            if(err == EAGAIN) {
                return ;
            }
            level = NGX_LOG_ALERT;
            if (err == ECONNABORTED) { //ECONNRESET错误则发生在对方意外关闭套接字后
                level = NGX_LOG_ERR;
            } else if (err == EMFILE || err == ENFILE) {//EMFILE:进程的fd已用尽
                level = NGX_LOG_CRIT;
            }
            ngx_log_error_core(level,errno,"CSocekt::ngx_event_accept()中accept4()失败!");

            if(use_accept4 && err == ENOSYS) {//accept4()函数没实现
                use_accept4 = 0;  //标记不使用accept4()函数，改用accept()函数
                continue;         //回去重新用accept()函数搞
            }

            if (err == ECONNABORTED) {//对方关闭套接字
                //这个错误因为可以忽略，所以不用干啥
                //do nothing
            }

            if (err == EMFILE || err == ENFILE) {
                //do nothing，这个官方做法是先把读事件从listen socket上移除，然后再弄个定时器，定时器到了则继续执行该函数，但是定时器到了有个标记，会把读事件增加到listen socket上去；
            }
            return;
        }

        //走到这里的，表示accept4() or accept()成功了
        new_conn = std::move(ngx_get_connection(connfd));
        if (new_conn == nullptr) {
            // 空闲连接被耗尽,把这个socekt直接关闭并返回
            if(close(connfd) == -1) {
                ngx_log_error_core(NGX_LOG_ALERT,errno,"CSocekt::ngx_event_accept()中close(%d)失败!",connfd);
            }
            return;
        }
        //成功的拿到了连接池中的一个连接
        memcpy(&new_conn->s_sockaddr, &mysockaddr, socklen); //拷贝客户端地址到连接对象

        if(!use_accept4) {
            //如果不是用accept4()取得的socket，那么就要设置为非阻塞【因为用accept4()的已经被accept4()设置为非阻塞了】
            if(setnonblocking(connfd) == false) {
                //设置非阻塞居然失败
                ngx_close_connection(new_conn); //回收连接池中的连接（千万不能忘记），并关闭socket
                return; //直接返回
            }
        }

        new_conn->listening = conn->listening; //连接对象和监听对象关联，方便通过连接对象找监听对象【关联到监听端口】
        new_conn->write_ready = 1; //标记可以写，新连接写事件肯定是ready的；【从连接池拿出一个连接时这个连接的所有成员都是0】
        new_conn->read_handler = &CSocket::ngx_wait_request_handler;  //设置数据来时的读处理函数

        //客户端应该主动发送第一次的数据，这里将读事件加入epoll监控
        if(ngx_epoll_add_event(connfd,                 //socket句柄
                               1, 0,              //读，写 ,这里读为1，表示客户端应该主动给我服务器发送消息，我服务器需要首先收到客户端的消息；
                               EPOLLET,          //其他补充标记【EPOLLET(高速模式，边缘触发ET)】
                               EPOLL_CTL_ADD,    //事件类型【增加，还有删除/修改】
                               new_conn) == -1)   //连接池中的连接
        {
            //增加事件失败，失败日志在ngx_epoll_add_event中写过了，因此这里不多写啥；
            ngx_close_connection(new_conn);//回收连接池中的连接（千万不能忘记），并关闭socket
            return; //直接返回
        }

        break;  //一般就是循环一次就跳出去
    }
    return ;
}
