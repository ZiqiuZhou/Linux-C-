//
// Created by dietrich on 8/7/21.
//
#include <stdarg.h>
#include <unistd.h>  //usleep

#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_threadpool.h"
#include "ngx_c_memory.h"
#include "ngx_macro.h"

std::mutex CThreadPool::m_pthreadMutex;
std::condition_variable CThreadPool::m_pthreadCond;
bool CThreadPool::m_shutdown = false; //刚开始标记整个线程池的线程是不退出的

CThreadPool::CThreadPool() {
    m_RunningThreadNum = 0; //正在运行的线程，开始给个0
    m_iLastEmgTime = 0;      //上次报告线程不够用了的时间；
    m_iRecvMsgQueueCount = 0;    //收消息队列
}

//析构函数
CThreadPool::~CThreadPool() {
    //资源释放在StopAll()里统一进行
    clearMsgRecvQueue();
}

//清理接收消息队列，注意这个函数的写法。
void CThreadPool::clearMsgRecvQueue()
{
    char * sTmpMempoint;
    CMemory *p_memory = CMemory::GetInstance();

    while(!m_MsgRecvQueue.empty()) {
        sTmpMempoint = m_MsgRecvQueue.front();
        m_MsgRecvQueue.pop_front();
        p_memory->FreeMemory(sTmpMempoint);
    }
}

//创建线程池中的线程
//返回值：所有线程都创建成功则返回true，出现错误则返回false
bool CThreadPool::Create(int threadNum) {
    std::shared_ptr<ThreadItem> thread_item(new ThreadItem(this));

    m_iThreadNum = threadNum;
    for (int i = 0; i < threadNum; ++i) {
        thread_item->_Handle = std::thread(ThreadFunc, std::ref(thread_item));
        m_threadVector.emplace_back(std::move(thread_item));
    }

    auto iter = m_threadVector.begin();
    for (iter; iter != m_threadVector.end(); ++iter) {
        while ((*iter)->ifrunning == false) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    return true;
}

//线程入口函数
void* CThreadPool::ThreadFunc(std::shared_ptr<ThreadItem>& threadData) {
    std::shared_ptr<CThreadPool> pThreadPool = threadData->_pThis;

    CMemory *p_memory = CMemory::GetInstance();

    std::thread::id tid = std::this_thread::get_id();
    while (1) {
        std::unique_lock<std::mutex> u_lock{m_pthreadMutex};

        if(threadData->ifrunning == false) {
            threadData->ifrunning = true;
        }
        ngx_log_stderr(0,"CThreadPool::ThreadFunc()pthread_mutex_lock()!");
        // wait until condition satisfied => lambda returns true
        m_pthreadCond.wait(u_lock, [&pThreadPool] {
            return m_shutdown == true ||
                   pThreadPool->m_MsgRecvQueue.size() > 0;
        });

        ngx_log_stderr(0,"CThreadPool::ThreadFunc()pthread_cond_wait()释放锁!");

        if(m_shutdown) {
            u_lock.unlock();
            break;
        }

        //走到这里，可以取得消息进行处理了【消息队列中必然有消息】
        char *jobbuf = pThreadPool->m_MsgRecvQueue.front();
        pThreadPool->m_MsgRecvQueue.pop_front();
        --pThreadPool->m_iRecvMsgQueueCount;

        u_lock.unlock();

        pThreadPool->m_RunningThreadNum++;
        g_socket.threadRecvProcFunc(jobbuf);     //处理消息队列中来的消息
        p_memory->FreeMemory(jobbuf);              //释放消息内存
        --pThreadPool->m_RunningThreadNum;     //原子-1
    }

    return (void*)0;
}

//停止所有线程
void CThreadPool::StopAll() {
    if (m_shutdown == true) {
        return ;
    }

    m_pthreadMutex.lock();
    m_shutdown = true;
    m_pthreadCond.notify_all();
    m_pthreadMutex.unlock();

    auto iter = m_threadVector.begin();
    for (iter; iter != m_threadVector.end(); ++iter) {
        ((*iter)->_Handle).join(); //等待一个线程终止
    }

    m_threadVector.clear();

    return ;
}

//来任务了，调一个线程池中的线程下来干活
void CThreadPool::Call() {
    m_pthreadCond.notify_one();

    if(m_iThreadNum == m_RunningThreadNum) {
        time_t currtime = time(NULL);
        if (currtime - m_iLastEmgTime > 10) {//最少间隔10秒钟才报一次线程池中线程不够用的问题；
            //两次报告之间的间隔必须超过10秒，不然如果一直出现当前工作线程全忙，但频繁报告日志也够烦的
            m_iLastEmgTime = currtime;  //更新时间
            //写日志，通知这种紧急情况给用户，用户要考虑增加线程池中线程数量了
            ngx_log_stderr(0, "CThreadPool::Call()中发现线程池中当前空闲线程数量为0，要考虑扩容线程池了!");
        }
    }

    return ;
}

//收到一个完整消息后，入消息队列，并触发线程池中线程来处理该消息
void CThreadPool::inMsgRecvQueueAndSignal(char *buf) {
    std::unique_lock<std::mutex> u_lock{m_pthreadMutex};
    m_MsgRecvQueue.emplace_back(std::move(buf));
    ++m_iRecvMsgQueueCount;

    Call();
    u_lock.unlock();

    return ;
}