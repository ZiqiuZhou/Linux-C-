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

CSocket::CSocket() {
    m_ListenPortCount = 1;
}

CSocket::~CSocket() {
    auto pos = m_ListenSocketList.begin();
    for (pos; pos != m_ListenSocketList.end(); ++pos) {
        auto raw_ptr = (*pos).release();
        delete raw_ptr;
    }
    m_ListenSocketList.clear();
}

//初始化函数【fork()子进程之前干这个事】
//成功返回true，失败返回false
bool CSocket::Initialize() {
    bool reco = ngx_open_listening_sockets();
    return reco;
}

//监听端口【支持多个端口】，这里遵从nginx的函数命名
//在创建worker进程之前就要执行这个函数；
bool CSocket::ngx_open_listening_sockets() {
    CConfig *p_config = CConfig::GetInstance();
    m_ListenPortCount = p_config->GetIntDefault("ListenPortCount",m_ListenPortCount);

    int                sockfd;                //socket
    struct sockaddr_in serv_addr;            //服务器的地址结构体
    int                iport;                //端口
    char               strinfo[100];         //临时字符串

    //初始化相关
    memset(&serv_addr,0,sizeof(serv_addr));  //先初始化一下
    serv_addr.sin_family = AF_INET;                //选择协议族为IPV4
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

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

        auto socketItem = std::make_unique<ngx_listening_t>();
        socketItem->port = iport;
        socketItem->sockfd = sockfd;
        ngx_log_error_core(NGX_LOG_INFO, 0,"监听%d端口成功!",iport); //显示一些信息到日志中
        m_ListenSocketList.emplace_back(std::move(socketItem));
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