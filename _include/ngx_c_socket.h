//
// Created by dietrich on 7/30/21.
//

#ifndef LINUX_CPP_COMM_ARCHITECTURE_NGX_C_SOCKET_H
#define LINUX_CPP_COMM_ARCHITECTURE_NGX_C_SOCKET_H

#include <vector>
#include <memory>

#define NGX_LISTEN_BACKLOG 511 //已完成连接队列，nginx给511

//和监听端口有关的结构
struct ngx_listening_t {
    int port;  //监听的端口号
    int sockfd; //套接字句柄socket
};

class CSocket {
public:
    CSocket();
    virtual ~CSocket();

    virtual bool Initialize(); //初始化函数

private:
    bool ngx_open_listening_sockets();                    //监听必须的端口【支持多个端口】
    void ngx_close_listening_sockets();                   //关闭监听socket
    bool setnonblocking(int sockfd);                      //设置非阻塞socket

private:
    int m_ListenPortCount; //所监听的端口数量
    std::vector<std::unique_ptr<ngx_listening_t> > m_ListenSocketList;  //监听套接字队列
};

#endif //LINUX_CPP_COMM_ARCHITECTURE_NGX_C_SOCKET_H
