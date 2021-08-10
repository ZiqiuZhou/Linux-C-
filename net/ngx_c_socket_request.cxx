//
// 和网络  中 客户端请求数据有关的代码
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
#include <thread>

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"
#include "ngx_c_memory.h"

//来数据时候的处理，当连接上有数据来的时候，本函数会被ngx_epoll_process_events()所调用  ,官方的类似函数为ngx_http_wait_request_handler();
void CSocket::ngx_wait_request_handler(std::shared_ptr<ngx_connection_poll>& conn) {
    ssize_t reco = recvproc(conn, conn->precvbuf, conn->irecvlen);
    if (reco < 0) {
        return ;
    }

    if (conn->curStat == _PKG_HD_INIT) {
        if (reco == m_iLenPkgHeader) { //正好收到完整包头，这里拆解包头
            ngx_wait_request_handler_proc_p1(conn); //调用专门针对包头处理完整的函数去处理
        } else {
            //收到的包头不完整--我们不能预料每个包的长度，也不能预料各种拆包/粘包情况，所以收到不完整包头【也算是缺包】是很可能的；
            conn->curStat = _PKG_HD_RECVING;
            conn->precvbuf = conn->precvbuf + reco;
            conn->irecvlen = conn->irecvlen - reco;
        }
    } else if (conn->curStat = _PKG_HD_RECVING) {
        if(conn->irecvlen == reco) {
            //包头收完整了
            ngx_wait_request_handler_proc_p1(conn); //调用专门针对包头处理完整的函数去处理
        } else {
            conn->precvbuf = conn->precvbuf + reco;
            conn->irecvlen = conn->irecvlen - reco;
        }
    } else if(conn->curStat == _PKG_BD_INIT) {
        if(conn->irecvlen == reco) {
            ngx_wait_request_handler_proc_plast(conn); //收到一个完整包后的处理
        } else {
            conn->curStat = _PKG_BD_RECVING;
            conn->precvbuf = conn->precvbuf + reco;
            conn->irecvlen = conn->irecvlen - reco;
        }
    } else if(conn->curStat == _PKG_BD_RECVING) {
        //接收包体中，包体不完整，继续接收中
        if(conn->irecvlen == reco) {
            //包体收完整了
            ngx_wait_request_handler_proc_plast(conn);
        } else {
            //包体没收完整，继续收
            conn->precvbuf = conn->precvbuf + reco;
            conn->irecvlen = conn->irecvlen - reco;
        }
    }  //end if(c->curStat == _PKG_HD_INIT)

    return;
}

//接收数据专用函数--引入这个函数是为了方便，如果断线，错误之类的，这里直接 释放连接池中连接，然后直接关闭socket，以免在其他函数中还要重复的干这些事
//参数conn：连接池中相关连接
//参数buff：接收数据的缓冲区
//参数buflen：要接收的数据大小
//返回值：返回-1，则是有问题发生并且在这里把问题处理完毕了，调用本函数的调用者一般是可以直接return
//        返回>0，则是表示实际收到的字节数
ssize_t CSocket::recvproc(std::shared_ptr <ngx_connection_poll> &conn, char *buff, ssize_t buflen) {
    ssize_t n;
    n  = recv(conn->connfd, buff, buflen, 0); //recv()系统函数
    if (n == 0) {
        //客户端关闭【应该是正常完成了4次挥手】
        ngx_log_stderr(0,"连接被客户端正常关闭[4路挥手关闭]！");
        ngx_close_connection(conn);
        return -1;
    }
    // 有错误发生
    if (n < 0) {
        // 表示没收到数据
        if(errno == EAGAIN || errno == EWOULDBLOCK) {
            ngx_log_stderr(errno,"CSocekt::recvproc()中errno == EAGAIN || errno == EWOULDBLOCK成立！");
            return -1; //不当做错误处理，只是简单返回
        }
        if(errno == EINTR) {
            //我认为LT模式不该出现这个errno，而且这个其实也不是错误，所以不当做错误处理
            ngx_log_stderr(errno,"CSocekt::recvproc()中errno == EINTR成立，出乎我意料！");//epoll为LT模式不应该出现这个返回值，所以直接打印出来瞧瞧
            return -1; //不当做错误处理，只是简单返回
        }

        //所有从这里走下来的错误，都认为异常：意味着我们要关闭客户端套接字要回收连接池中连接；
        if(errno == ECONNRESET) {
        } else {
            //能走到这里的，都表示错误，我打印一下日志，希望知道一下是啥错误，我准备打印到屏幕上
            ngx_log_stderr(errno,"CSocekt::recvproc()中发生错误，我打印出来看看是啥错误！");  //正式运营时可以考虑这些日志打印去掉
        }

        ngx_close_connection(conn);
        return -1;
    }

    return n;
}

//包头收完整后的处理
void CSocket::ngx_wait_request_handler_proc_p1(std::shared_ptr<ngx_connection_poll> &conn) {
    CMemory *p_memory = CMemory::GetInstance();

    COMM_PKG_HEADER* pPkgHeader;
    pPkgHeader = (COMM_PKG_HEADER*)conn->dataHeadInfo;
    unsigned short e_pkgLen;
    e_pkgLen = ntohs(pPkgHeader->pkgLen);

    //恶意包或者错误包的判断,整个包长比包头还小
    if(e_pkgLen < m_iLenPkgHeader) {
        conn->curStat = _PKG_HD_INIT;
        conn->precvbuf = conn->dataHeadInfo;
        conn->irecvlen = m_iLenPkgHeader;
    } else if (e_pkgLen > (_PKG_MAX_LENGTH - 1000)) {
        conn->curStat = _PKG_HD_INIT;
        conn->precvbuf = conn->dataHeadInfo;
        conn->irecvlen = m_iLenPkgHeader;
    } else { //合法的包头
        char *pTmpBuffer  = (char *)p_memory->AllocMemory(m_iLenMsgHeader + e_pkgLen,false); //分配内存【长度是 消息头长度  + 包头长度 + 包体长度】，最后参数先给false，表示内存不需要memset;
        conn->ifnewrecvMem   = true;        //标记我们new了内存，将来在ngx_free_connection()要回收的
        conn->pnewMemPointer = pTmpBuffer;  //内存开始指针

        //a)先填写消息头内容
        STRUC_MSG_HEADER* ptmpMsgHeader = (STRUC_MSG_HEADER*)pTmpBuffer;
        ptmpMsgHeader->pConn = conn;
        ptmpMsgHeader->iCurrsequence = conn->iCurrsequence;
        //b)再填写包头内容
        pTmpBuffer += m_iLenMsgHeader;                 //往后跳，跳过消息头，指向包头
        memcpy(pTmpBuffer,pPkgHeader,m_iLenPkgHeader); //直接把收到的包头拷贝进来
        if (e_pkgLen == m_iLenPkgHeader) {
            //该报文只有包头无包体
            ngx_wait_request_handler_proc_plast(conn);
        } else {
            conn->curStat = _PKG_BD_INIT;
            conn->precvbuf = pTmpBuffer + m_iLenPkgHeader;
            conn->irecvlen = e_pkgLen - m_iLenPkgHeader;
        }
    }
    return ;
}

void CSocket::ngx_wait_request_handler_proc_plast(std::shared_ptr<ngx_connection_poll> &conn) {
    //把这段内存放到消息队列中来；
    int irmqc = 0;  //消息队列当前信息数量
    g_threadpool.inMsgRecvQueueAndSignal(conn->pnewMemPointer); //入消息队列并触发线程处理消息

    conn->ifnewrecvMem    = false;            //内存不再需要释放，因为你收完整了包，这个包被上边调用inMsgRecvQueue()移入消息队列，那么释放内存就属于业务逻辑去干，不需要回收连接到连接池中干了
    conn->pnewMemPointer  = nullptr;
    conn->curStat         = _PKG_HD_INIT;     //收包状态机的状态恢复为原始态，为收下一个包做准备
    conn->precvbuf        = conn->dataHeadInfo;  //设置好收包的位置
    conn->irecvlen        = m_iLenPkgHeader;  //设置好要接收数据的大小
    return;
}

void CSocekt::threadRecvProcFunc(char *pMsgBuf) {
    return;
}