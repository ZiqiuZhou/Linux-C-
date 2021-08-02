
#ifndef __NGX_GBLDEF_H__
#define __NGX_GBLDEF_H__

#include <system_error>
#include <cassert>
#include <stdexcept>  // for runtime_error
#include <string>
#include <signal.h>

//和运行日志相关
struct ngx_log_t{
    int log_level = 3; //日志级别 或者日志类型，ngx_macro.h里分0-8共9个级别
    int fd; //日志文件描述符
};

//外部全局量声明
extern size_t      g_argvneedmem;
extern size_t      g_envneedmem;
extern int         g_os_argc;
extern char        **g_os_argv;
extern char        *gp_envmem;

extern pid_t       ngx_pid;
extern pid_t       ngx_parent;
extern ngx_log_t   ngx_log;
extern int           ngx_process;
extern sig_atomic_t  ngx_reap;
#endif
