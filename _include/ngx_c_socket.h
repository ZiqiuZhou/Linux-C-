//
// Created by dietrich on 7/30/21.
//

#ifndef LINUX_CPP_COMM_ARCHITECTURE_NGX_C_SOCKET_H
#define LINUX_CPP_COMM_ARCHITECTURE_NGX_C_SOCKET_H

#include <vector>
#include <forward_list>
#include <list>
#include <memory>
#include <mutex>
#include <sys/epoll.h> //epoll
#include <sys/socket.h>

#include "ngx_comm.h"

#define NGX_LISTEN_BACKLOG 511 // 已完成连接队列，nginx给511
#define NGX_MX_EVENTS      512 // epoll_wait一次最多接收这么多个事件

struct ngx_listening_t;
struct ngx_connection_poll;
class CSocket;

typedef void (CSocket::*ngx_event_handler_pt)(std::shared_ptr<ngx_connection_poll>& conn); //定义成员函数指针

//和监听端口有关的结构
struct ngx_listening_t {
    int port;  //监听的端口号
    int sockfd; //套接字句柄socket
    std::shared_ptr<ngx_connection_poll> connnection; //连接池中的一个连接
};

//一个TCP连接【客户端主动发起的、Nginx服务器被动接受的TCP连接】
struct ngx_connection_poll {
    int connfd; //套接字句柄socket
    std::shared_ptr<ngx_listening_t> listening; //如果这个链接被分配给了一个监听套接字，那么这就指向监听套接字对应的ngx_listening_t*

    unsigned instance:1;     //【位域】失效标志位：0：有效，1：失效
    uint64_t iCurrsequence;  //序号，每次分配出去时+1，此法也有可能在一定程度上检测错包废包
    struct sockaddr s_sockaddr; //保存对方地址信息

    uint8_t write_ready; //写准备好标记
    ngx_event_handler_pt read_handler; //读事件的相关处理方法
    ngx_event_handler_pt write_handler; //写事件的相关处理方法

    //和收包有关
    unsigned char curStat;                 //当前收包的状态
    char  dataHeadInfo[_DATA_BUFSIZE_];   //用于保存收到的数据的包头信息
    char  *precvbuf;                      //接收数据的缓冲区的头指针，对收到不全的包非常有用
    unsigned int irecvlen;                //要收到多少数据，由这个变量指定，和precvbuf配套使用

    bool ifnewrecvMem;                   //如果我们成功的收到了包头，那么我们就要分配内存开始保存 包头+消息头+包体内容，这个标记用来标记是否我们new过内存，因为new过是需要释放的
    char *pnewMemPointer;                //new出来的用于收包的内存首地址，和ifnewrecvMem配对使用
};

//消息头，引入的目的是当收到数据包时，额外记录一些内容以备将来使用
struct STRUC_MSG_HEADER
{
    std::shared_ptr<ngx_connection_poll> pConn; //记录对应的链接，注意这是个指针
    uint64_t iCurrsequence; //收到数据包时记录对应连接的序号，将来能用于比较是否连接已经作废用
    //......其他以后扩展
};

class CSocket {
public:
    CSocket();
    virtual ~CSocket();
    virtual bool Initialize(); //初始化函数
    virtual void threadRecvProcFunc(char *pMsgBuf);

public:
    int ngx_epoll_init();
    //epoll增加事件(epoll_ctl)
    int ngx_epoll_add_event(int fd, int readevent, int writeevent,
                            uint32_t otherflag, uint32_t eventtype,
                            std::shared_ptr <ngx_connection_poll> &conn);

    int  ngx_epoll_process_events(int timer);

private:
    void ReadConf();
    bool ngx_open_listening_sockets();                    //监听必须的端口【支持多个端口】
    void ngx_close_listening_sockets();                   //关闭监听socket
    bool setnonblocking(int sockfd);                      //设置非阻塞socket

    //一些业务处理函数handler
    void ngx_event_accept(std::shared_ptr<ngx_connection_poll>& conn);
    void ngx_wait_request_handler(std::shared_ptr<ngx_connection_poll>& conn);
    void ngx_close_connection(std::shared_ptr<ngx_connection_poll>& conn);

    ssize_t recvproc(std::shared_ptr<ngx_connection_poll> &conn, char *buff, ssize_t buflen);  //接收从客户端来的数据专用函数
    void ngx_wait_request_handler_proc_p1(std::shared_ptr<ngx_connection_poll> &conn);
    void ngx_wait_request_handler_proc_plast(std::shared_ptr<ngx_connection_poll> &conn);

    //连接池 或 连接 相关
    std::shared_ptr<ngx_connection_poll> ngx_get_connection(int sockfd); //从连接池中获取一个空闲连接
    void ngx_free_connection(std::shared_ptr<ngx_connection_poll>& conn); //归还参数conn所代表的连接到到连接池中

private:
    int m_worker_connections = 1;  //epoll连接的最大项数
    int m_ListenPortCount = 1;  //所监听的端口数量
    int m_epollhandle;     //epoll_create返回的句柄

    int m_free_connection_n;
    size_t  m_iLenPkgHeader; //sizeof(COMM_PKG_HEADER);
    size_t  m_iLenMsgHeader; //sizeof(STRUC_MSG_HEADER);

    //和连接池有关的
    std::forward_list<std::shared_ptr<ngx_connection_poll> > p_free_connections; // free connection list
    std::vector<std::shared_ptr<ngx_listening_t> > m_ListenSocketList;  //监听套接字队列
    struct epoll_event m_events[NGX_MX_EVENTS]; // epoll_event

    //多线程相关
    std::mutex m_recvMessageQueueMutex;
};

#endif //LINUX_CPP_COMM_ARCHITECTURE_NGX_C_SOCKET_H
