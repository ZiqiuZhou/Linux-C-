//
// Created by dietrich on 7/30/21.
//

#ifndef LINUX_CPP_COMM_ARCHITECTURE_NGX_C_SOCKET_H
#define LINUX_CPP_COMM_ARCHITECTURE_NGX_C_SOCKET_H

#include <vector>
#include <list>
#include <memory>
#include <mutex>
#include <semaphore.h>
#include <atomic>
#include <thread>
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
    ngx_connection_poll();   //构造函数
    virtual ~ngx_connection_poll(); //析构函数
    void GetOneToUse();  //分配出去的时候初始化一些内容
    void PutOneToFree();  //回收回来的时候做一些事情

    int connfd; //套接字句柄socket
    std::shared_ptr<ngx_listening_t> listening; //如果这个链接被分配给了一个监听套接字，那么这就指向监听套接字对应的ngx_listening_t*

    unsigned instance:1;     //【位域】失效标志位：0：有效，1：失效
    uint64_t iCurrsequence = 0;  //序号，每次分配出去时+1，此法也有可能在一定程度上检测错包废包
    struct sockaddr s_sockaddr; //保存对方地址信息

    ngx_event_handler_pt read_handler; //读事件的相关处理方法
    ngx_event_handler_pt write_handler; //写事件的相关处理方法

    //和epoll事件有关
    uint32_t events; //和epoll事件有关

    //和收包有关
    unsigned char curStat;                 //当前收包的状态
    char  dataHeadInfo[_DATA_BUFSIZE_];   //用于保存收到的数据的包头信息
    char  *precvbuf;                      //接收数据的缓冲区的头指针，对收到不全的包非常有用
    unsigned int irecvlen;                //要收到多少数据，由这个变量指定，和precvbuf配套使用
    char *precvMemPointer;                //new出来的用于收包的内存首地址，和ifnewrecvMem配对使用

    std::mutex logicPorcMutex;           //逻辑处理相关的互斥量

    //和发包有关
    std::atomic<int> iThrowsendCount; //发送消息，如果发送缓冲区满了，则需要通过epoll事件来驱动消息的继续发送，所以如果发送缓冲区满，则用这个变量标记
    char *psendMemPointer;  //发送完成后释放用的，整个数据的头指针，其实是 消息头 + 包头 + 包体
    char *psendbuf;          //发送数据的缓冲区的头指针，开始 其实是包头+包体
    unsigned int  isendlen;  //要发送多少数据

    //和回收有关
    time_t inRecyTime; //入到资源回收站里去的时间
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
    virtual bool Initialize_subproc();
    virtual void Shutdown_subproc();
    virtual void threadRecvProcFunc(char *pMsgBuf);

public:
    int ngx_epoll_init();

    int ngx_epoll_oper_event(int fd, uint32_t eventtype, uint32_t flag, int bcaction,
                             std::shared_ptr <ngx_connection_poll> &conn);

    int  ngx_epoll_process_events(int timer);

    //数据发送相关
    void msgSend(char *psendbuf);

private:
    struct ThreadItem {
        std::thread   _Handle;                                              //线程句柄
        CSocket     *_pThis;                                              //记录线程池的指针
        bool        ifrunning;                                            //标记是否正式启动起来，启动起来后，才允许调用StopAll()来释放

        //构造函数
        ThreadItem(CSocket *pthis):_pThis(pthis),ifrunning(false){}
        //析构函数
        ~ThreadItem(){}
    };

private:
    void ReadConf();
    bool ngx_open_listening_sockets();                    //监听必须的端口【支持多个端口】
    void ngx_close_listening_sockets();                   //关闭监听socket
    bool setnonblocking(int sockfd);                      //设置非阻塞socket

    //一些业务处理函数handler
    void ngx_event_accept(std::shared_ptr<ngx_connection_poll>& conn);
    void ngx_read_request_handler(std::shared_ptr<ngx_connection_poll>& conn);
    void ngx_write_request_handler(std::shared_ptr<ngx_connection_poll>& conn);
    void ngx_close_connection(std::shared_ptr<ngx_connection_poll>& conn);

    ssize_t recvproc(std::shared_ptr<ngx_connection_poll> &conn, char *buff, ssize_t buflen);  //接收从客户端来的数据专用函数
    void ngx_wait_request_handler_proc_p1(std::shared_ptr<ngx_connection_poll> &conn);
    void ngx_wait_request_handler_proc_plast(std::shared_ptr<ngx_connection_poll> &conn);
    void clearMsgSendQueue();

    ssize_t sendproc(std::shared_ptr<ngx_connection_poll> &conn, char *buff, ssize_t size); //将数据发送到客户端

    //连接池 或 连接 相关
    void initConnection();                                                //初始化连接池
    void clearConnection();
    std::shared_ptr<ngx_connection_poll> ngx_get_connection(int sockfd); //从连接池中获取一个空闲连接
    void ngx_free_connection(std::shared_ptr<ngx_connection_poll>& conn); //归还参数conn所代表的连接到到连接池中
    void inRecyConnectQueue(std::shared_ptr<ngx_connection_poll>& pConn);                    //将要回收的连接放到一个队列中来

    //线程相关函数
    static void* ServerSendQueueThread(std::shared_ptr<ThreadItem>& threadData);                 //专门用来发送数据的线程
    static void* ServerRecyConnectionThread(std::shared_ptr<ThreadItem>& threadData);            //专门用来回收连接的线程

protected:
    size_t  m_iLenPkgHeader; //sizeof(COMM_PKG_HEADER);
    size_t  m_iLenMsgHeader; //sizeof(STRUC_MSG_HEADER);

private:
    int m_worker_connections = 1;  //epoll连接的最大项数
    int m_ListenPortCount = 1;  //所监听的端口数量
    int m_epollhandle;     //epoll_create返回的句柄


    //和连接池有关的
    std::list<std::shared_ptr<ngx_connection_poll> > m_connectionList; //连接列表【连接池】
    std::list<std::shared_ptr<ngx_connection_poll>>  m_freeconnectionList;                  //空闲连接列表【这里边装的全是空闲的连接】

    int m_RecyConnectionWaitTime; //等待这么些秒后才回收连接
    std::list<std::shared_ptr<ngx_connection_poll>>  m_recyconnectionList; //将要释放的连接放这里
    std::atomic<int>  m_totol_recyconnection_n;              //待释放连接队列大小
    std::atomic<int>  m_total_connection_n;  //连接池总连接数
    std::atomic<int>  m_free_connection_n;   //连接池空闲连接数
    static std::mutex m_connectionMutex;  //连接相关互斥量，互斥m_freeconnectionList，m_connectionList
    static std::mutex m_recyconnqueueMutex; //连接回收队列相关的互斥量

    std::vector<std::shared_ptr<ngx_listening_t> > m_ListenSocketList;  //监听套接字队列
    struct epoll_event m_events[NGX_MX_EVENTS]; // epoll_event

    //消息队列
    std::list<char *> m_MsgSendQueue;                        //发送数据消息队列
    std::atomic<int>  m_iSendMsgQueueCount;                  //发消息队列大小
    //多线程相关
    std::vector<std::shared_ptr<ThreadItem>> m_threadVector;                        //线程 容器，容器里就是各个线程了
    static std::mutex  m_sendMessageQueueMutex;               //发消息队列互斥量
    sem_t m_semEventSendQueue;
};

#endif //LINUX_CPP_COMM_ARCHITECTURE_NGX_C_SOCKET_H
