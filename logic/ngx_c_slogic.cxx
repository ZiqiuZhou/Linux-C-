//
// Created by dietrich on 8/8/21.
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
//#include <sys/socket.h>
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>
#include <thread>

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_memory.h"
#include "ngx_c_crc32.h"
#include "ngx_c_slogic.h"
#include "ngx_logiccomm.h"

//定义成员函数指针
typedef bool (CLogicSocket::*handler)(std::shared_ptr <ngx_connection_poll> pConn,      //连接池中连接的指针
                                      STRUC_MSG_HEADER* pMsgHeader,  //消息头指针
                                      char *pPkgBody,                 //包体指针
                                      unsigned short iBodyLength);

//用来保存 成员函数指针 的这么个数组
static const handler statusHandler[] =
        {
                //数组前5个元素，保留，以备将来增加一些基本服务器功能
                NULL,                                                   //【0】：下标从0开始
                NULL,                                                   //【1】：下标从0开始
                NULL,                                                   //【2】：下标从0开始
                NULL,                                                   //【3】：下标从0开始
                NULL,                                                   //【4】：下标从0开始

                //开始处理具体的业务逻辑
                &CLogicSocket::_HandleRegister,                         //【5】：实现具体的注册功能
                &CLogicSocket::_HandleLogIn,                            //【6】：实现具体的登录功能
                //......其他待扩展，比如实现攻击功能，实现加血功能等等；


        };
#define AUTH_TOTAL_COMMANDS sizeof(statusHandler)/sizeof(handler) //整个命令有多少个，编译时即可知道

//构造函数
CLogicSocket::CLogicSocket() {

}
//析构函数
CLogicSocket::~CLogicSocket() {

}

bool CLogicSocket::Initialize() {
    //做一些和本类相关的初始化工作
    //....日后根据需要扩展
    bool bParentInit = CSocket::Initialize();
    return bParentInit;
}

//处理收到的数据包
//pMsgBuf：消息头 + 包头 + 包体 ：自解释；
void CLogicSocket::threadRecvProcFunc(char *pMsgBuf) {
    STRUC_MSG_HEADER* pMsgHeader = (STRUC_MSG_HEADER*)pMsgBuf;                 //消息头
    COMM_PKG_HEADER*  pPkgHeader = (COMM_PKG_HEADER*)(pMsgBuf + m_iLenMsgHeader); //包头
    void  *pPkgBody = nullptr;                                                   //指向包体的指针
    unsigned short pkglen = ntohs(pPkgHeader->pkgLen);                           //客户端指明的包宽度【包头+包体】

    if(m_iLenPkgHeader == pkglen) {
        //没有包体，只有包头
        if(pPkgHeader->crc32 != 0) { //只有包头的crc值给0
            return; //crc错，直接丢弃
        }
        pPkgBody = nullptr;
    } else {
        pPkgHeader->crc32 = ntohl(pPkgHeader->crc32);		          //针对4字节的数据，网络序转主机序
        pPkgBody = (void *) (pMsgBuf + m_iLenMsgHeader + m_iLenPkgHeader);

        //计算crc值判断包的完整性
        int calccrc = CCRC32::GetInstance()->Get_CRC((unsigned char *) pPkgBody, pkglen - m_iLenPkgHeader); //计算纯包体的crc值
        if(calccrc != pPkgHeader->crc32) {//服务器端根据包体计算crc值，和客户端传递过来的包头中的crc32信息比较
            ngx_log_stderr(0,"CLogicSocket::threadRecvProcFunc()中CRC错误，丢弃数据!");    //正式代码中可以干掉这个信息
            return; //crc错，直接丢弃
        }
    }

    //包crc校验OK才能走到这里
    unsigned short imsgCode = ntohs(pPkgHeader->msgCode); //消息代码拿出来
    auto p_Conn = pMsgHeader->pConn;        //消息头中藏着连接池中连接的指针

    //我们要做一些判断
    //(1)如果从收到客户端发送来的包，到服务器释放一个线程池中的线程处理该包的过程中，客户端断开了，那显然，这种收到的包我们就不必处理了；
    if(p_Conn->iCurrsequence != pMsgHeader->iCurrsequence) {  //该连接池中连接以被其他tcp连接【其他socket】占用，这说明原来的 客户端和本服务器的连接断了，这种包直接丢弃不理
        return; //丢弃不理这种包了【客户端断开了】
    }

    //(2)判断消息码是正确的，防止客户端恶意侵害我们服务器，发送一个不在我们服务器处理范围内的消息码
    if(imsgCode >= AUTH_TOTAL_COMMANDS) //无符号数不可能<0
    {
        ngx_log_stderr(0,"CLogicSocket::threadRecvProcFunc()中imsgCode=%d消息码不对!",imsgCode); //这种有恶意倾向或者错误倾向的包，希望打印出来看看是谁干的
        return; //丢弃不理这种包【恶意包或者错误包】
    }

    //能走到这里的，包没过期，不恶意，那好继续判断是否有相应的处理函数
    //(3)有对应的消息处理函数吗
    if(statusHandler[imsgCode] == NULL) {//这种用imsgCode的方式可以使查找要执行的成员函数效率特别高
        ngx_log_stderr(0,"CLogicSocket::threadRecvProcFunc()中imsgCode=%d消息码找不到对应的处理函数!",imsgCode); //这种有恶意倾向或者错误倾向的包，希望打印出来看看是谁干的
        return;  //没有相关的处理函数
    }

    //(4)调用消息码对应的成员函数来处理
    (this->*statusHandler[imsgCode])(p_Conn, pMsgHeader, (char *) pPkgBody, pkglen - m_iLenPkgHeader);

    return;
}

//----------------------------------------------------------------------------------------------------------
//处理各种业务逻辑
bool CLogicSocket::_HandleRegister(std::shared_ptr <ngx_connection_poll> pConn, STRUC_MSG_HEADER* pMsgHeader,
                                   char *pPkgBody, unsigned short iBodyLength) {
    //(1)首先判断包体的合法性
    if(pPkgBody == nullptr) {
        return false;
    }

    int iRecvLen = sizeof(_STRUCT_REGISTER);

    if(iRecvLen != iBodyLength) { //发送过来的结构大小不对，认为是恶意包，直接不处理
        return false;
    }

    std::unique_lock<std::mutex> u_lock(pConn->logicPorcMutex);

    //(3)取得了整个发送过来的数据
    _STRUCT_REGISTER* p_RecvInfo = (_STRUCT_REGISTER*)pPkgBody;

    //(5)给客户端返回数据时，一般也是返回一个结构，这个结构内容具体由客户端/服务器协商，这里我们就以给客户端也返回同样的 STRUCT_REGISTER 结构来举例
    COMM_PKG_HEADER* pPkgHeader;
    CMemory  *p_memory = CMemory::GetInstance();
    CCRC32   *p_crc32 = CCRC32::GetInstance();
    int iSendLen = sizeof(_STRUCT_REGISTER);
    //a)分配要发送出去的包的内存
    char *p_sendbuf = (char *) p_memory->AllocMemory(m_iLenMsgHeader + m_iLenPkgHeader + iSendLen,
                                                     false);//准备发送的格式，这里是 消息头+包头+包体
    //b)填充消息头
    memcpy(p_sendbuf,pMsgHeader,m_iLenMsgHeader);                   //消息头直接拷贝到这里来
    //c)填充包头
    pPkgHeader = (COMM_PKG_HEADER*)(p_sendbuf + m_iLenMsgHeader);    //指向包头
    pPkgHeader->msgCode = _CMD_REGISTER;	                        //消息代码，可以统一在ngx_logiccomm.h中定义
    pPkgHeader->msgCode = htons(pPkgHeader->msgCode);	            //htons主机序转网络序
    pPkgHeader->pkgLen  = htons(m_iLenPkgHeader + iSendLen);        //整个包的尺寸【包头+包体尺寸】
    //d)填充包体
    _STRUCT_REGISTER* p_sendInfo = (_STRUCT_REGISTER*)(p_sendbuf + m_iLenMsgHeader + m_iLenPkgHeader);	//跳过消息头，跳过包头，就是包体了
    //。。。。。这里根据需要，填充要发回给客户端的内容,int类型要使用htonl()转，short类型要使用htons()转；

    //e)包体内容全部确定好后，计算包体的crc32值
    pPkgHeader->crc32   = p_crc32->Get_CRC((unsigned char *)p_sendInfo,iSendLen);
    pPkgHeader->crc32   = htonl(pPkgHeader->crc32);

    //f)发送数据包
    msgSend(p_sendbuf);
    ngx_log_stderr(0,"执行了CLogicSocket::_HandleRegister()!");
    return true;
}

bool CLogicSocket::_HandleLogIn(std::shared_ptr <ngx_connection_poll> pConn, STRUC_MSG_HEADER* pMsgHeader,
                                char *pPkgBody, unsigned short iBodyLength) {
    ngx_log_stderr(0, "执行了CLogicSocket::_HandleLogIn()!");
    return true;
}