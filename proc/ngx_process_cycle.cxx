//
// Created by dietrich on 7/28/21.
//
//和开启子进程相关

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>   //信号相关头文件
#include <errno.h>    //errno
#include <unistd.h>

#include "ngx_func.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_c_conf.h"

//函数声明
static void ngx_start_worker_processes(int threadnums);
static int ngx_spawn_process(int threadnums,const char *pprocname);
static void ngx_worker_process_cycle(int inum,const char *pprocname);
static void ngx_worker_process_init(int inum);

//变量声明
static u_char  master_process[] = "master process";

//描述：创建worker子进程
void ngx_master_process_cycle() {
    sigset_t set; //信号集
    sigemptyset(&set); //清空信号集

    //下列这些信号在执行本函数期间不希望收到
    //建议fork()子进程时学习这种写法，防止信号的干扰；
    sigaddset(&set, SIGCHLD);     //子进程状态改变
    sigaddset(&set, SIGALRM);     //定时器超时
    sigaddset(&set, SIGIO);       //异步I/O
    sigaddset(&set, SIGINT);      //终端中断符
    sigaddset(&set, SIGHUP);      //连接断开
    sigaddset(&set, SIGUSR1);     //用户定义信号
    sigaddset(&set, SIGUSR2);     //用户定义信号
    sigaddset(&set, SIGWINCH);    //终端窗口大小改变
    sigaddset(&set, SIGTERM);     //终止
    sigaddset(&set, SIGQUIT);     //终端退出符
    //.........可以根据开发的实际需要往其中添加其他要屏蔽的信号......

    //设置，此时无法接受的信号，阻塞，等放开信号屏蔽后才能收到这些信号。。。
    //第一个参数用了SIG_BLOCK表明设置进程新的信号屏蔽字为 “当前信号屏蔽字(当前无)和第二个参数指向的信号集（上面10个）的并集
    if (sigprocmask(SIG_BLOCK, &set, NULL) == -1) {
        ngx_log_error_core(NGX_LOG_ALERT,errno,"ngx_master_process_cycle()中sigprocmask()失败!");
    }

    //首先我设置主进程标题---------begin
    size_t size;
    int i;
    size = sizeof(master_process);  //这里用的是sizeof，所以字符串末尾的\0是被计算进来了的
    size += g_argvneedmem;          //argv参数长度加进来
    if(size < 1000) //长度小于这个，我才设置标题
    {
        char title[1000] = {0};
        strcpy(title,(const char *)master_process); //"master process"
        strcat(title," ");  //跟一个空格分开一些，清晰    //"master process "
        for (i = 0; i < g_os_argc; i++)         //"master process ./nginx"
        {
            strcat(title,g_os_argv[i]);
        }//end for
        ngx_setproctitle(title); //设置标题
        ngx_log_error_core(NGX_LOG_NOTICE,0,"%s %P 启动并开始运行......!",title,ngx_pid); //设置标题时顺便记录下来进程名，进程id等信息到日志
    }
    //首先我设置主进程标题---------end

    //从配置文件中读取要创建的worker进程数量
    CConfig *p_config = CConfig::GetInstance(); //单例类
    int work_process = p_config->GetIntDefault("WorkerProcesses",1); //从配置文件中得到要创建的worker进程数量
    ngx_start_worker_processes(work_process);  //这里要创建多个worker子进程

    //创建子进程后，父进程的执行流程会返回到这里，子进程不会走进来
    sigemptyset(&set); //信号屏蔽字为空，表示不屏蔽任何信号

    for (;;) {
//        ngx_log_error_core(0,0,"haha--这是父进程，pid为%P",ngx_pid);
//
//        //a)根据给定的参数设置新的信号屏蔽集并阻塞当前进程
//        //b)此时，一旦收到信号，便恢复原先的信号屏蔽【我们原来的mask在上边设置的，阻塞了多达10个信号】
//        //c)调用该信号对应的信号处理函数
//        //d)信号处理函数返回后，sigsuspend返回，使程序流程继续往下走
        sigsuspend(&set); //阻塞在这里，等待一个信号，此时进程是挂起的，不占用cpu时间，只有收到信号才会被唤醒（返回）；
        sleep(1); //休息1秒
//        printf("执行到sigsuspend()下边来了\n");
    }
    return;
}

//描述：根据给定的参数创建指定数量的子进程，因为以后可能要扩展功能，增加参数，所以单独写成一个函数
//threadnums:要创建的子进程数量
static void ngx_start_worker_processes(int threadnums)
{
    int i;
    for (i = 0; i < threadnums; i++)  //master进程在走这个循环，来创建若干个子进程
    {
        ngx_spawn_process(i,"worker process");
    } //end for
    return;
}

//描述：产生一个子进程
//inum：进程编号【0开始】
//pprocname：子进程名字"worker process"
static int ngx_spawn_process(int inum, const char* pprocname) {
    pid_t pid;
    pid = fork(); //fork()系统调用产生子进程

    switch (pid) {
        case -1: { //产生子进程失败
            ngx_log_error_core(NGX_LOG_ALERT,errno,"ngx_spawn_process()fork()产生子进程num=%d,procname=\"%s\"失败!", inum, pprocname);
            return -1;
        }
        case 0: { //子进程分支
            ngx_parent = ngx_pid;              //因为是子进程了，所有原来的pid变成了父pid
            ngx_pid = getpid();                //重新获取pid,即本子进程的pid
            ngx_worker_process_cycle(inum, pprocname);    //我希望所有worker子进程，在这个函数里不断循环着不出来
            break;
        }
        default: { //这个应该是父进程分支，直接break;，流程往switch之后走
            break;
        }
    } // end switch

    //父进程分支会走到这里，子进程流程不往下边走-------------------------
    return pid;
}

//描述：worker子进程的功能函数，每个woker子进程，就在这里循环着了（无限循环【处理网络事件和定时器事件以对外提供web服务】）
//     子进程分叉才会走到这里
//inum：进程编号【0开始】
static void ngx_worker_process_cycle(int inum,const char *pprocname) {
    //设置一下变量
    ngx_process = NGX_PROCESS_WORKER;  //设置进程的类型，是worker进程

    ngx_worker_process_init(inum);
    ngx_setproctitle(pprocname); //子进程设置进程名
    ngx_log_error_core(NGX_LOG_NOTICE,0,"%s %P 启动并开始运行......!",pprocname,ngx_pid); //设置标题时顺便记录下来进程名，进程id等信息到日志

    //暂时先放个死循环，我们在这个循环里一直不出来
    for (;;) {
        ngx_process_events_and_timers(); //处理网络事件和定时器事件
    }
    return ;
}

//描述：子进程创建时调用本函数进行一些初始化工作
static void ngx_worker_process_init(int inum) {
    sigset_t  set;      //信号集

    sigemptyset(&set);  //清空信号集
    // 第一个参数用SIG_SETMASK表明设置进程新的信号屏蔽字为第二个参数指向的信号集
    if (sigprocmask(SIG_SETMASK, &set, NULL) == -1)  //原来是屏蔽那10个信号，现在不再屏蔽任何信号【接收任何信号】
    {
        ngx_log_error_core(NGX_LOG_ALERT,errno,"ngx_worker_process_init()中sigprocmask()失败!");
    }

    g_socket.ngx_epoll_init(); //初始化epoll相关内容 epoll_create(), epoll_ctl()
    //....将来再扩充代码
    //....
    return;
}
