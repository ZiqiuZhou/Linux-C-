
#include <iostream>
#include <cstdio>
#include <unistd.h>

#include "ngx_func.h"  // 各种函数声明
#include "ngx_c_conf.h"  //和配置文件处理相关的类,名字带c_表示和类有关
#include "ngx_signal.h"

//和设置标题有关的全局量
char **g_os_argv;            //原始命令行参数数组,在main中会被赋值
char *gp_envmem = nullptr;      //指向自己分配的env环境变量的内存
int  g_environlen = 0;       //环境变量所占内存大小

// functions defined in other .cxx files
extern void ngx_init_setproctitle();
extern void ngx_setproctitle(const char *title);

int main(int argc, char *const *argv)
{
    g_os_argv = (char **) argv;
    ngx_init_setproctitle();    //把环境变量搬家
    ngx_setproctitle("nginx: master process");

    CConfig* p_config = CConfig::GetInstance(); // 单例类
    if (p_config->Load("nginx.conf") == false) {
        std::cout << "Failed to load config file." << std::endl;
        exit(1);
    }


    if(gp_envmem)
    {
        delete []gp_envmem;
        gp_envmem = nullptr;
    }
    printf("程序退出，再见!\n");

    return 0;
}


