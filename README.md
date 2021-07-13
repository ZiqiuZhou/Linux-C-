一. Nginx简介
Nginx是一个轻量级Web服务器，支持高并发，可靠性高。
Nginx的进程模型：
  1. ./nginx后输入ps -ef | grep nginx可以看到master进程（root用户）和worker进程（系统用户），多个worker进程是master进程的子进程
  2. 支持不中断服务的情况下重新加载配置（热启动）./nginx -s reload
  3. ./nginx -s stop和./nginx -s quit都可以关闭nginx,前者立刻关闭，后者不接收新的连接，已连接部分服务完关闭
