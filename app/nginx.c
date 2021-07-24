

#include <stdio.h>
#include <unistd.h>

#include "ngx_func.h"  //头文件路径，已经使用gcc -I参数指定了
#include "ngx_signal.h"

int main(int argc, char *const *argv)
{             
    printf("非常高兴，我们大家一起学习《linux C++通信架构实战》\n");    
    myconf();
    mysignal();
    
    /*for(;;)
    {
        sleep(1); //休息1秒
        printf("休息1秒\n");
    }*/
    printf("程序退出，再见!\n");
    return 0;
}


