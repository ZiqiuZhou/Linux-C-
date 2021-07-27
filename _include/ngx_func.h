﻿#ifndef __NGX_FUNC_H__
#define __NGX_FUNC_H__

//字符串相关函数
void Trim(std::string& str);

//设置可执行程序标题相关函数
void  ngx_init_setproctitle();
void  ngx_setproctitle(const char *title);

//和日志，打印输出有关
void  ngx_log_init();
void  ngx_log_stderr(int err, const char *fmt, ...);
void  ngx_log_error_core(int level,  int err, const char *fmt, ...);

u_char *ngx_log_errno(u_char *buf, u_char *last, int err);
u_char *ngx_slprintf(u_char *buf, u_char *last, const char *fmt, ...);
u_char *ngx_vslprintf(u_char *buf, u_char *last,const char *fmt,va_list args);
#define  MYVER  "1.2"
#endif  