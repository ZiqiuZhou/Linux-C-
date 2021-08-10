//
// Created by dietrich on 7/30/21.
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
#include <sys/socket.h>
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"
#include "ngx_c_memory.h"

CSocket::CSocket() {
    //epoll相关
    m_epollhandle = -1;          //epoll返回的句柄
    p_free_connections.clear();
    m_ListenSocketList.clear();

    //一些和网络通讯有关的常用变量值，供后续频繁使用时提高效率
    m_iLenPkgHeader = sizeof(COMM_PKG_HEADER);    //包头的sizeof值【占用的字节数】
    m_iLenMsgHeader = sizeof(STRUC_MSG_HEADER);  //消息头的sizeof值【占用的字节数】
}

CSocket::~CSocket() {
    p_free_connections.clear();
    m_ListenSocketList.clear();
}

//初始化函数【fork()子进程之前干这个事】
//成功返回true，失败返回false
bool CSocket::Initialize() {
    ReadConf();
    bool reco = ngx_open_listening_sockets();
    return reco;
}

//专门用于读各种配置项
void CSocket::ReadConf() {
    CConfig *p_config = CConfig::GetInstance();
    //取得要监听的端口数量
    m_ListenPortCount = p_config->GetIntDefault("ListenPortCount", 1);
    //epoll连接的最大项数
    m_worker_connections = p_config->GetIntDefault("worker_connections", 512);
    return ;
}

//监听端口【支持多个端口】，这里遵从nginx的函数命名
//在创建worker进程之前就要执行这个函数；
bool CSocket::ngx_open_listening_sockets() {
    int                sockfd;                //socket
    struct sockaddr_in serv_addr;            //服务器的地址结构体
    int                iport;                //端口
    char               strinfo[100];         //临时字符串

    //初始化相关
    memset(&serv_addr,0,sizeof(serv_addr));  //先初始化一下
    serv_addr.sin_family = AF_INET;                //选择协议族为IPV4
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    CConfig *p_config = CConfig::GetInstance(); // read port info
    for (int i = 0; i < m_ListenPortCount; ++i) {
        //参数1：AF_INET：使用ipv4协议
        //参数2：SOCK_STREAM：使用TCP
        //参数3：给0，表示使用默认协议
        sockfd = socket(AF_INET,SOCK_STREAM,0);
        if (sockfd == -1) {
            ngx_log_stderr(errno,"CSocekt::Initialize()中socket()失败,i=%d.",i);
            return false;
        }

        //setsockopt（）:设置一些套接字参数选项；
        //参数2：是表示级别，和参数3配套使用
        //参数3：允许重用本地地址
        //设置 SO_REUSEADDR，服务器可强制重用处于TIME_WAIT状态的socket地址
        int reuseaddr = 1;  //1:打开对应的设置项
        if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *) &reuseaddr, sizeof(reuseaddr)) == -1) {
            ngx_log_stderr(errno,"CSocekt::Initialize()中setsockopt(SO_REUSEADDR)失败,i=%d.",i);
            close(sockfd); //无需理会是否正常执行了
            return false;
        }

        //设置该socket为非阻塞
        if(setnonblocking(sockfd) == false)
        {
            ngx_log_stderr(errno,"CSocekt::Initialize()中setnonblocking()失败,i=%d.",i);
            close(sockfd);
            return false;
        }

        strinfo[0] = 0;
        sprintf(strinfo,"ListenPort%d",i);
        iport = p_config->GetIntDefault(strinfo,10000);
        serv_addr.sin_port = htons((in_port_t)iport);   //in_port_t其实就是uint16_t

        //绑定服务器地址结构体
        if (bind(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
            ngx_log_stderr(errno,"CSocekt::Initialize()中bind()失败,i=%d.",i);
            close(sockfd);
            return false;
        }

        //开始监听
        if (listen(sockfd, NGX_LISTEN_BACKLOG) == -1) {
            ngx_log_stderr(errno,"CSocekt::Initialize()中listen()失败,i=%d.",i);
            close(sockfd);
            return false;
        }

        auto socketItem = std::make_shared<ngx_listening_t>();
        socketItem->port = iport;
        socketItem->sockfd = sockfd;
        ngx_log_error_core(NGX_LOG_INFO, 0,"监听%d端口成功!",iport); //显示一些信息到日志中
        m_ListenSocketList.emplace_back(std::move(socketItem));
    }

    if (m_ListenSocketList.empty() || m_ListenSocketList.size() == 0) {
        return false;
    }
    return true;
}

//设置socket连接为非阻塞模式【这种函数的写法很固定】
bool CSocket::setnonblocking(int sockfd) {
    int nb=1; //0：清除，1：设置
    if(ioctl(sockfd, FIONBIO, &nb) == -1) //FIONBIO：设置/清除非阻塞I/O标记：0：清除，1：设置
    {
        return false;
    }
    return true;
}

void CSocket::ngx_close_listening_sockets() {
    for(int i = 0; i < m_ListenPortCount; i++) {
        close(m_ListenSocketList[i]->sockfd);
        ngx_log_error_core(NGX_LOG_INFO,0,"关闭监听端口%d!",m_ListenSocketList[i]->port); //显示一些信息到日志中
    }
    return ;
}

//(1)epoll功能初始化，子进程中进行 ，本函数被ngx_worker_process_init()所调用
int CSocket::ngx_epoll_init() {
    //创建一个epoll对象，创建了一个红黑树，还创建了一个双向链表
    // (1) epoll_create
    m_epollhandle = epoll_create(m_worker_connections);
    if (m_epollhandle == -1) {
        ngx_log_stderr(errno,"CSocekt::ngx_epoll_init()中epoll_create()失败.");
        exit(2);
    }

    // (2) 创建连接池list
    m_free_connection_n = m_worker_connections; //记录当前连接池中连接总数
    for (int i = 0; i < m_free_connection_n; ++i) {
        auto ngx_connection_poll_ptr = std::make_shared<ngx_connection_poll>();
        ngx_connection_poll_ptr->connfd = -1;
        ngx_connection_poll_ptr->instance = 1;
        ngx_connection_poll_ptr->iCurrsequence = 0;

        p_free_connections.emplace_front(std::move(ngx_connection_poll_ptr));
    }

    // (3) 遍历所有监听socket【监听端口】，为每个监听socket增加一个连接池中的连接
    auto pos = m_ListenSocketList.begin();
    for (pos; pos != m_ListenSocketList.end(); ++pos) {
        //从连接池中获取一个空闲连接对象
        std::shared_ptr <ngx_connection_poll> conn = std::move(ngx_get_connection((*pos)->sockfd));

        if (conn == nullptr) {
            ngx_log_stderr(errno,"CSocekt::ngx_epoll_init()中ngx_get_connection()失败.");
            exit(2);
        }

        //连接对象 和监听对象关联
        conn->listening = (*pos);
        (*pos)->connnection = conn;

        //对监听端口的读事件(三次握手)设置处理方法
        conn->read_handler = &CSocket::ngx_event_accept;

        //往监听socket上增加监听事件
        if (ngx_epoll_add_event((*pos)->sockfd, //监听socekt句柄
                                1, 0, //读，写【只关心读事件，所以参数2：readevent=1,而参数3：writeevent=0】
                                0, //其他补充标记
                                EPOLL_CTL_ADD, //事件类型【增加，还有删除/修改】
                                conn) == -1) {
            exit(2);
        }
    }

    return 1;
}

//epoll增加事件
//fd:句柄，一个socket
//readevent：表示是否是个读事件，0是，1不是
//writeevent：表示是否是个写事件，0是，1不是
//otherflag：其他需要额外补充的标记
//eventtype：事件类型，一般就是用系统的枚举值，增加，删除，修改等;
//conn：对应的连接池中的连接的指针
//返回值：成功返回1，失败返回-1；
int CSocket::ngx_epoll_add_event(int sockfd, int readevent, int writeevent,
                        uint32_t otherflag, uint32_t eventtype,
                        std::shared_ptr<ngx_connection_poll> &conn) {
    struct epoll_event event;
    memset(&event, 0, sizeof(epoll_event));

    if (readevent == 0) {
        event.events = EPOLLIN | EPOLLRDHUP; //EPOLLIN读事件，也就是read ready【客户端三次握手连接进来，也属于一种可读事件】 EPOLLRDHUP 客户端关闭连接，断连
    } else {
        //其他事件类型待处理
        //.....
    }

    if(otherflag != 0)
    {
        event.events |= otherflag;
    }

    event.data.ptr = (void *)( (uintptr_t)(conn.get()) | conn->instance);

    if(epoll_ctl(m_epollhandle, eventtype, sockfd, &event) == -1) {
        ngx_log_stderr(errno,"CSocekt::ngx_epoll_add_event()中epoll_ctl(%d,%d,%d,%u,%u)失败.",sockfd,readevent,writeevent,otherflag,eventtype);
        return -1;
    }
    return 1;
}

//开始获取发生的事件消息
//参数unsigned int timer：epoll_wait()阻塞的时长，单位是毫秒；
//返回值，1：正常返回  ,0：有问题返回，一般不管是正常还是问题返回，都应该保持进程继续运行
int CSocket::ngx_epoll_process_events(int timer) {
    //等待事件，事件会返回到m_events里，最多返回NGX_MAX_EVENTS个事件
    //如果timer为-1则一直阻塞，如果timer为0则立即返回，即便没有任何事件
    //如果返回>0则表示成功捕获到这么多个事件
    int events = epoll_wait(m_epollhandle, m_events, NGX_MX_EVENTS, timer);

    if (events == -1) {
        if(errno == EINTR) {
            //信号所致，直接返回，一般认为这不是毛病，但还是打印下日志记录一下
            ngx_log_error_core(NGX_LOG_INFO,errno,"CSocekt::ngx_epoll_process_events()中epoll_wait()失败!");
            return 1;  //正常返回
        } else {
            //这被认为应该是有问题，记录日志
            ngx_log_error_core(NGX_LOG_ALERT,errno,"CSocekt::ngx_epoll_process_events()中epoll_wait()失败!");
            return 0;  //非正常返回
        }
    }

    // 超时，但没事件来
    if (events == 0) {
        if (timer != -1) { // 阻塞到时间了(timer != -1)，正常返回
            return 1;
        } else {
            //timer == 1 无限等待【所以不存在超时】，但却没返回任何事件，这应该不正常有问题
            ngx_log_error_core(NGX_LOG_ALERT,0,"CSocekt::ngx_epoll_process_events()中epoll_wait()没超时却没返回任何事件!");
            return 0; //非正常返回
        }
    }

    //走到这里，就是属于有事件收到了
    uintptr_t instance;
    uint32_t revents;
    //遍历本次epoll_wait返回的所有事件
    for (int i = 0; i < events; ++i) {
        ngx_connection_poll* raw_ptr = new ngx_connection_poll();
        raw_ptr = (ngx_connection_poll*)(m_events[i].data.ptr);
        instance = (uintptr_t) raw_ptr & 1;
        raw_ptr = (ngx_connection_poll*) ((uintptr_t)raw_ptr & (uintptr_t) ~1);
        std::shared_ptr<ngx_connection_poll> conn(std::move(raw_ptr));
        delete raw_ptr;

        if (conn->connfd == -1) {
            //过来的事件对应一个之前被关闭的连接,属于过期事件，不该处理
            ngx_log_error_core(NGX_LOG_DEBUG,0,"CSocekt::ngx_epoll_process_events()中遇到了fd=-1的过期事件:%p.",conn);
            continue;
        }
        if (conn->instance != instance) {
            ngx_log_error_core(NGX_LOG_DEBUG,0,"CSocekt::ngx_epoll_process_events()中遇到了instance值改变的过期事件:%p.",conn);
            continue; //这种事件就不处理即可
        }

        // 正常开始处理
        revents = m_events[i].events;//取出事件类型
        if (revents & (EPOLLERR|EPOLLHUP)) { //例如对方close掉套接字
            //EPOLLIN：表示对应的链接上有数据可以读出（TCP链接的远端主动关闭连接，也相当于可读事件，因为本服务器处理发送来的FIN包）
            //EPOLLOUT：表示对应的连接上可以写入数据发送【写准备好】
            revents |= EPOLLIN|EPOLLOUT;
        }
        if (revents & EPOLLIN) { // 读事件
            (this->* (conn->read_handler) )(conn); // 新连接进入，这里执行的应该是CSocket::ngx_event_accept(conn)
        }
        if (revents & EPOLLOUT) { // 写事件

        }
    }
    return 1;
}