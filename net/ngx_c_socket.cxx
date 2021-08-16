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
#include <thread>
#include <mutex>

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"
#include "ngx_c_memory.h"

std::mutex CSocket::m_connectionMutex;  //连接相关互斥量，互斥m_freeconnectionList，m_connectionList
std::mutex CSocket::m_recyconnqueueMutex;
std::mutex CSocket::m_sendMessageQueueMutex;

CSocket::CSocket() {
    //epoll相关
    m_epollhandle = -1;          //epoll返回的句柄

    //一些和网络通讯有关的常用变量值，供后续频繁使用时提高效率
    m_iLenPkgHeader = sizeof(COMM_PKG_HEADER);    //包头的sizeof值【占用的字节数】
    m_iLenMsgHeader = sizeof(STRUC_MSG_HEADER);  //消息头的sizeof值【占用的字节数】

    //各种队列相关
    m_iSendMsgQueueCount = 0;     //发消息队列大小
    m_totol_recyconnection_n = 0; //待释放连接队列大小
}

CSocket::~CSocket() {
    m_ListenSocketList.clear();
}

//初始化函数【fork()子进程之前干这个事】
//成功返回true，失败返回false
bool CSocket::Initialize() {
    ReadConf();
    bool reco = ngx_open_listening_sockets();
    return reco;
}

bool CSocket::Initialize_subproc() {
    //第二个参数=0，表示信号量在线程之间共享，确实如此 ，如果非0，表示在进程之间共享
    //第三个参数=0，表示信号量的初始值，为0时，调用sem_wait()就会卡在那里卡着
    if(sem_init(&m_semEventSendQueue, 0, 0) == -1) {
        ngx_log_stderr(0,"CSocket::Initialize()中sem_init(&m_semEventSendQueue,0,0)失败.");
        return false;
    }

    //创建线程
    std::shared_ptr<ThreadItem> pSendQueue(new ThreadItem(this)); //专门用来发送数据的线程
    pSendQueue->_Handle = std::thread(ServerSendQueueThread, std::ref(pSendQueue));
    m_threadVector.emplace_back(std::move(pSendQueue));

    //创建线程
    std::shared_ptr<ThreadItem> pRecyconn(new ThreadItem(this)); //专门用来回收连接的线程
    pRecyconn->_Handle = std::thread(ServerRecyConnectionThread, std::ref(pRecyconn));
    m_threadVector.emplace_back(std::move(pRecyconn));

    return true;
}

// 把干活的线程停止掉
void CSocket::Shutdown_subproc() {
    if(sem_post(&m_semEventSendQueue)==-1)  //让ServerSendQueueThread()流程走下来干活
    {
        ngx_log_stderr(0,"CSocekt::Shutdown_subproc()中sem_post(&m_semEventSendQueue)失败.");
    }

    auto iter = m_threadVector.begin();
    for(; iter != m_threadVector.end(); iter++)
    {
        ((*iter)->_Handle).join();
    }
    m_threadVector.clear();

    //(3)队列相关
    clearMsgSendQueue();
    clearConnection();

    //(4)多线程相关
    sem_destroy(&m_semEventSendQueue);                  //发消息相关线程信号量释放
}

//专门用于读各种配置项
void CSocket::ReadConf() {
    CConfig *p_config = CConfig::GetInstance();
    //取得要监听的端口数量
    m_ListenPortCount = p_config->GetIntDefault("ListenPortCount", 1);
    //epoll连接的最大项数
    m_worker_connections = p_config->GetIntDefault("worker_connections", 512);
    //等待这么些秒后才回收连接
    m_RecyConnectionWaitTime = p_config->GetIntDefault("Sock_RecyConnectionWaitTime", 60);
    return ;
}

//清理TCP发送消息队列
void CSocket::clearMsgSendQueue() {
    char * sTmpMempoint;
    CMemory *p_memory = CMemory::GetInstance();

    while(!m_MsgSendQueue.empty())
    {
        sTmpMempoint = m_MsgSendQueue.front();
        m_MsgSendQueue.pop_front();
        p_memory->FreeMemory(sTmpMempoint);
    }
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

//将一个待发送消息入到发消息队列中
void CSocket::msgSend(char *psendbuf) {
    std::unique_lock<std::mutex> u_lock(m_sendMessageQueueMutex);
    m_MsgSendQueue.emplaec_back(psendbuf);

    ++m_iSendMsgQueueCount;   //原子操作
    //将信号量的值+1,这样其他卡在sem_wait的就可以走下去
    if(sem_post(&m_semEventSendQueue)==-1)  //让ServerSendQueueThread()流程走下来干活
    {
        ngx_log_stderr(0,"CSocekt::msgSend()中sem_post(&m_semEventSendQueue)失败.");
    }
    return;
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
    initConnection();

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
        if (ngx_epoll_oper_event((*pos)->sockfd, //监听socekt句柄
                                EPOLL_CTL_ADD, //读，写【只关心读事件，所以参数2：readevent=1,而参数3：writeevent=0】
                                EPOLLIN|EPOLLRDHUP, //其他补充标记
                                0, //事件类型【增加，还有删除/修改】
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
int CSocket::ngx_epoll_oper_event(
        int fd,               //句柄，一个socket
        uint32_t eventtype,        //事件类型，一般是EPOLL_CTL_ADD，EPOLL_CTL_MOD，EPOLL_CTL_DEL ，说白了就是操作epoll红黑树的节点(增加，修改，删除)
        uint32_t flag,             //标志，具体含义取决于eventtype
        int bcaction,         //补充动作，用于补充flag标记的不足  :  0：增加   1：去掉 2：完全覆盖 ,eventtype是EPOLL_CTL_MOD时这个参数就有用
        std::shared_ptr <ngx_connection_poll>& pConn             //pConn：一个指针【其实是一个连接】，EPOLL_CTL_ADD时增加到红黑树中去，将来epoll_wait时能取出来用
)
{
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));

    if(eventtype == EPOLL_CTL_ADD) {//往红黑树中增加节点；
        //红黑树从无到有增加节点
        //ev.data.ptr = (void *)pConn;
        ev.events = flag;      //既然是增加节点，则不管原来是啥标记
        pConn->events = flag;  //这个连接本身也记录这个标记
    } else if(eventtype == EPOLL_CTL_MOD) {
        //节点已经在红黑树中，修改节点的事件信息
        ev.events = pConn->events;  //先把标记恢复回来
        if(bcaction == 0) {
            //增加某个标记
            ev.events |= flag;
        } else if(bcaction == 1) {
            //去掉某个标记
            ev.events &= ~flag;
        } else {
            //完全覆盖某个标记
            ev.events = flag;      //完全覆盖
        }
        pConn->events = ev.events; //记录该标记
    } else {
        //删除红黑树中节点，目前没这个需求【socket关闭这项会自动从红黑树移除】，所以将来再扩展
        return  1;  //先直接返回1表示成功
    }

    //原来的理解中，绑定ptr这个事，只在EPOLL_CTL_ADD的时候做一次即可，但是发现EPOLL_CTL_MOD似乎会破坏掉.data.ptr，因此不管是EPOLL_CTL_ADD，还是EPOLL_CTL_MOD，都给进去
    //找了下内核源码SYSCALL_DEFINE4(epoll_ctl, int, epfd, int, op, int, fd,		struct epoll_event __user *, event)，感觉真的会覆盖掉：
    //copy_from_user(&epds, event, sizeof(struct epoll_event)))，感觉这个内核处理这个事情太粗暴了
    ev.data.ptr = (void *)pConn.get();

    if(epoll_ctl(m_epollhandle, eventtype, fd, &ev) == -1) {
        ngx_log_stderr(errno,"CSocekt::ngx_epoll_oper_event()中epoll_ctl(%d,%ud,%ud,%d)失败.",fd,eventtype,flag,bcaction);
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
    uint32_t revents;
    //遍历本次epoll_wait返回的所有事件
    for (int i = 0; i < events; ++i) {
        ngx_connection_poll* raw_ptr = new ngx_connection_poll;
        raw_ptr = (ngx_connection_poll*)(m_events[i].data.ptr);
        raw_ptr = (ngx_connection_poll*) ((uintptr_t)raw_ptr & (uintptr_t) ~1);
        std::shared_ptr<ngx_connection_poll> conn(std::move(raw_ptr));
        delete raw_ptr;

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
            if(revents & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                --conn->iThrowsendCount;
            } else {
                (this->* (conn->write_handler) )(conn);
            }
        }
    }
    return 1;
}

void* CSocket::ServerSendQueueThread(std::shared_ptr<ThreadItem>& threadData) {
    CSocekt *pSocketObj = pThread->_pThis;

    char *pMsgBuf;
    STRUC_MSG_HEADER*	pMsgHeader;
    COMM_PKG_HEADER*    pPkgHeader;
    lpngx_connection_t  p_Conn;
    unsigned short      itmp;
    ssize_t             sendsize;

    CMemory *p_memory = CMemory::GetInstance();
}