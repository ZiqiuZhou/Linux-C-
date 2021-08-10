//
// Created by dietrich on 8/7/21.
//

#ifndef LINUX_CPP_COMM_ARCHITECTURE_NGX_C_THREADPOLL_H
#define LINUX_CPP_COMM_ARCHITECTURE_NGX_C_THREADPOLL_H

#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

class CThreadPool {
public:
    CThreadPool();
    ~CThreadPool();

public:
    bool Create(int threadNum); //创建该线程池中的所有线程
    void StopAll();             //使线程池中的所有线程退出
    void Call();       //来任务了，调一个线程池中的线程
    void inMsgRecvQueueAndSignal(char *buf); //收到一个完整消息后，入消息队列，并触发线程池中线程来处理该消息

private:
    struct ThreadItem {
        std::thread _Handle;
        std::shared_ptr<CThreadPool> _pThis; //记录线程池的指针
        bool ifrunning; //标记是否正式启动起来，启动起来后，才允许调用StopAll()来释放

        ThreadItem(CThreadPool* pthis) : _pThis(pthis), ifrunning(false) {}
        ~ThreadItem(){}
    };

    static void* ThreadFunc(std::shared_ptr<ThreadItem>& threadData);  //新线程的线程入口函数
    void clearMsgRecvQueue(); //清理接收消息队列

private:
    static std::mutex m_pthreadMutex;
    static std::condition_variable m_pthreadCond;
    static bool m_shutdown; //线程退出标志，false不退出，true退出

    int m_iThreadNum; //要创建的线程数量
    std::atomic<int> m_RunningThreadNum;
    time_t m_iLastEmgTime; //上次发生线程不够用【紧急事件】的时间

    std::vector<std::shared_ptr<ThreadItem>> m_threadVector;

    //消息队列
    std::list<char *> m_MsgRecvQueue; //接收数据消息队列
    int m_iRecvMsgQueueCount; //收消息队列大小
};

#endif //LINUX_CPP_COMM_ARCHITECTURE_NGX_C_THREADPOLL_H
