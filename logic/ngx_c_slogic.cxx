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

//定义成员函数指针
typedef bool (CLogicSocket::*handler)(std::shared_ptr <ngx_connection_poll> pConn,      //连接池中连接的指针
                                      LPSTRUC_MSG_HEADER pMsgHeader,  //消息头指针
                                      char *pPkgBody,                 //包体指针
                                      unsigned short iBodyLength);

