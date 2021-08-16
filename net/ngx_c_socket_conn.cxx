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

//分配出去一个连接的时候初始化一些内容
void ngx_connection_poll::GetOneToUse() {
    ++iCurrsequence;

    curStat = _PKG_HD_INIT;                           //收包状态处于 初始状态，准备接收数据包头【状态机】
    precvbuf = dataHeadInfo;                          //收包我要先收到这里来，因为我要先收包头，所以收数据的buff直接就是dataHeadInfo
    irecvlen = sizeof(COMM_PKG_HEADER);               //这里指定收数据的长度，这里先要求收包头这么长字节的数据

    precvMemPointer = nullptr;                           //既然没new内存，那自然指向的内存地址先给NULL
    iThrowsendCount = 0;                              //原子的
    psendMemPointer = nullptr;                           //发送数据头指针记录
    events          = 0;                              //epoll事件先给0
}

//回收一个连接的时候做一些事
void ngx_connection_poll::PutOneToFree() {
    ++iCurrsequence;

    if(precvMemPointer != nullptr) {//我们曾经给这个连接分配过接收数据的内存，则要释放内存
        CMemory::GetInstance()->FreeMemory(precvMemPointer);
        precvMemPointer = nullptr;
    }
    if(psendMemPointer != nullptr) { //如果发送数据的缓冲区里有内容，则要释放内存
        CMemory::GetInstance()->FreeMemory(psendMemPointer);
        psendMemPointer = nullptr;
    }

    iThrowsendCount = 0;  //设置不设置感觉都行
}

//初始化连接池
void CSocket::initConnection()
{
    for (int i = 0; i < m_free_connection_n; ++i) {
        auto p_Conn = std::make_shared<ngx_connection_poll>();
        p_Conn->iCurrsequence = 0;
        p_Conn->GetOneToUse();
        m_connectionList.emplace_front(std::move(p_Conn));     //所有链接【不管是否空闲】都放在这个list
        m_freeconnectionList.emplace_front(std::move(p_Conn));
    }

    m_free_connection_n = m_total_connection_n = m_connectionList.size(); //开始这两个列表一样大

    return;
}

//从连接池中获取一个空闲连接,当一个客户端连接TCP进入，把这个连接和连接池中的一个连接【对象】绑到一起
std::shared_ptr<ngx_connection_poll> CSocket::ngx_get_connection(int sockfd) {
    //因为可能有其他线程要访问m_freeconnectionList，m_connectionList【比如可能有专门的释放线程要释放/或者主线程要释放】之类的，所以应该临界一下
    std::unique_lock<std::mutex> u_lock(m_connectionMutex);

    //有空闲的，自然是从空闲的中摘取
    if (!m_freeconnectionList.empty()) {
        auto conn = m_freeconnectionList.front(); //空闲连接链表头
        m_freeconnectionList.pop_front();
        conn->GetOneToUse();
        --m_free_connection_n; //空闲连接少1
        conn->connfd = sockfd;
        return conn;
    }

    //走到这里，表示没空闲的连接了，那就考虑重新创建一个连接
    auto p_Conn = std::make_shared<ngx_connection_poll>();
    p_Conn->iCurrsequence = 0;
    p_Conn->GetOneToUse();
    m_connectionList.emplace_front(std::move(p_Conn));
    ++m_total_connection_n;
    p_Conn->connfd = sockfd;

    return p_Conn;
}

//最终回收连接池，释放内存
void CSocket::clearConnection() {
    m_connectionList.clear();
}

// 归还参数conn所代表的连接到到连接池中
void CSocket::ngx_free_connection(std::shared_ptr<ngx_connection_poll>& conn) {
    std::unique_lock<std::mutex> u_lock(m_connectionMutex);

    conn->PutOneToFree();
    m_freeconnectionList.push_front(std::move(conn));
    //空闲连接数+1
    ++m_free_connection_n;
    return ;
}

//将要回收的连接放到一个队列中来，后续有专门的线程会处理这个队列中的连接的回收
void CSocket::inRecyConnectQueue(std::shared_ptr<ngx_connection_poll>& conn) {
    std::unique_lock<std::mutex> u_lock(m_recyconnqueueMutex);

    conn->inRecyTime = time(NULL);        //记录回收时间
    ++conn->iCurrsequence;
    m_recyconnectionList.push_front(std::move(conn)); //等待ServerRecyConnectionThread线程自会处理
    ++m_totol_recyconnection_n;            //待释放连接队列大小+1
    return;
}

//用户连入，我们accept4()时，得到的socket在处理中产生失败，则资源用这个函数释放【因为这里涉及到好几个要释放的资源，所以写成函数】
void CSocket::ngx_close_connection(std::shared_ptr<ngx_connection_poll>& conn) {
    std::unique_lock<std::mutex> u_lock(m_connectionMutex);

    int fd = conn->connfd;
    ngx_free_connection(conn);
    if(close(fd) == -1) {
        ngx_log_error_core(NGX_LOG_ALERT,errno,"CSocket::ngx_close_accepted_connection()中close(%d)失败!",fd);
    }
    return ;
}

void* CSocket::ServerRecyConnectionThread(std::shared_ptr<ThreadItem>& threadData) {
    CSocket *pSocketObj = threadData->_pThis;

    time_t currtime;
    std::shared_ptr<ngx_connection_poll> p_Conn;

    while(1) {
        usleep(200 * 1000);
        if(pSocketObj->m_totol_recyconnection_n > 0) {
            currtime = time(NULL);
            std::unique_lock<std::mutex> u_lock(m_recyconnqueueMutex);

            auto iter = pSocketObj->m_recyconnectionList.begin();
            for (; iter != pSocketObj->m_recyconnectionList.end(); ++iter) {
                if ((*iter)->inRecyTime + pSocketObj->m_RecyConnectionWaitTime > currtime) { // < 80s
                    continue; //没到释放的时间
                }

                --pSocketObj->m_totol_recyconnection_n;
                pSocketObj->m_recyconnectionList.erase(iter);
                pSocketObj->ngx_free_connection(p_Conn); //归还参数pConn所代表的连接到到连接池中
            }
            u_lock.unlock();
        }

        //要退出整个程序，那么肯定要先退出这个循环
        if(g_stopEvent == 1) {
            if(pSocketObj->m_totol_recyconnection_n > 0) {
                std::unique_lock<std::mutex> u_lock(m_recyconnqueueMutex);

                auto iter = pSocketObj->m_recyconnectionList.begin();
                for (; iter != pSocketObj->m_recyconnectionList.end(); ++iter) {
                    if ((*iter)->inRecyTime + pSocketObj->m_RecyConnectionWaitTime > currtime) { // < 80s
                        continue; //没到释放的时间
                    }

                    --pSocketObj->m_totol_recyconnection_n;
                    pSocketObj->m_recyconnectionList.erase(iter);
                    pSocketObj->ngx_free_connection(p_Conn); //归还参数pConn所代表的连接到到连接池中
                }
                u_lock.unlock();
            }
            break;
        }
    }

    return (void*)0;
}