
#ifndef __NGX_GBLDEF_H__
#define __NGX_GBLDEF_H__

#include <system_error>
#include <cassert>
#include <stdexcept>  // for runtime_error
#include <string>

//一些比较通用的定义放在这里


//结构定义
struct CConfItem
{
	std::string ItemName;
	std::string ItemContent;

	CConfItem(){}
};

//和运行日志相关
struct ngx_log_t{
    int log_level = 3; //日志级别 或者日志类型，ngx_macro.h里分0-8共9个级别
    int fd; //日志文件描述符
};

//外部全局量声明
extern char  **g_os_argv;
extern char  *gp_envmem; 
extern int   g_environlen;

extern pid_t       ngx_pid;
extern ngx_log_t   ngx_log;

// check a POSIX error code
static void posixCheck(int errorCode) {
    if (errorCode != 0) {
        throw std::system_error(std::error_code(errorCode, std::generic_category()));
    }
}

// check the status code of functions that return an error code in `errno`.
static void posixAssert(bool success) {
    if (!success) {
        posixCheck(errno);
    }
}

#endif
