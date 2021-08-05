//
// Created by dietrich on 7/30/21.
//

#ifndef LINUX_CPP_COMM_ARCHITECTURE_NGX_C_SOCKET_H
#define LINUX_CPP_COMM_ARCHITECTURE_NGX_C_SOCKET_H

#include <vector>
#include <forward_list>
#include <memory>
#include <sys/epoll.h> //epoll
#include <sys/socket.h>

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
};

class CSocket {
public:
    CSocket();
    virtual ~CSocket();

    virtual bool Initialize(); //初始化函数

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
    void ngx_close_accepted_connection(std::shared_ptr<ngx_connection_poll>& conn);

    //连接池 或 连接 相关
    std::shared_ptr<ngx_connection_poll> ngx_get_connection(int sockfd); //从连接池中获取一个空闲连接
    void ngx_free_connection(std::shared_ptr<ngx_connection_poll>& conn); //归还参数conn所代表的连接到到连接池中

private:
    int m_worker_connections = 1;  //epoll连接的最大项数
    int m_ListenPortCount = 1;  //所监听的端口数量
    int m_epollhandle;     //epoll_create返回的句柄

    int m_free_connection_n;
    //和连接池有关的
    std::forward_list<std::shared_ptr<ngx_connection_poll> > p_free_connections; // free connection list

    std::vector<std::shared_ptr<ngx_listening_t> > m_ListenSocketList;  //监听套接字队列
    struct epoll_event m_events[NGX_MX_EVENTS]; // epoll_event
};

#endif //LINUX_CPP_COMM_ARCHITECTURE_NGX_C_SOCKET_H
