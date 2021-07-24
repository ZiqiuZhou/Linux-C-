一. Nginx简介
Nginx是一个轻量级Web服务器，支持高并发，可靠性高。
Nginx的进程模型：
  1. ./nginx后输入ps -ef | grep nginx可以看到master进程（root用户）和worker进程（系统用户），多个worker进程是master进程的子进程
  2. 支持不中断服务的情况下重新加载配置（热启动）./nginx -s reload
  3. ./nginx -s stop和./nginx -s quit都可以关闭nginx,前者立刻关闭，后者不接收新的连接，已连接部分服务完关闭
  
二.服务器架构项目目录
主目录nginx
1. _include目录，存放所有头文件
2. app目录，放.c应用程序
  (1). link_obj目录，存放编译后产生的.o文件
  (2). dep目录
  (3). nginx.c主文件main函数
  (4). ngx_conf.c，普通文件
3. signal目录。存放和信号处理有关的.c文件
4. proc目录，存放和进程处理有关的
5. misc目录，杂合的.c文件
6. net目录，存放和网络处理有关的.c文件
