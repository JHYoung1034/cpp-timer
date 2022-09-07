#include "timer.h"
#include <thread>
#include <ratio>
#include <mutex>
#include <condition_variable>
#include <map>

class TimerSch
{
public:
    static TimerSch& GetInstance() {
        static TimerSch tsch;
        return tsch;
    }

    ~TimerSch() {
        timers_.clear();
        actived_ = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cv_.notify_one();
        }
        if (thread_.joinable())
            thread_.join();
    }

    void UpdateTimer(const TimerPtr &t, const steady_clock::time_point &tp);
    void AddTimer(const TimerPtr &t);
    void DelTimer(const TimerPtr &t);

private:
    TimerSch();
    std::atomic<bool>               actived_;
    std::thread                     thread_;
    std::mutex                      mutex_;
    std::condition_variable         cv_;
    std::multimap<steady_clock::time_point, TimerPtr>  timers_;
};

using namespace std::chrono;

Timer::Timer() : handleFunc_(nullptr), invalid_(false), addr_(this)
{
    timePoint_ = steady_clock::now();
}

Timer::~Timer()
{
    /** 要释放了，说明没有引用了，TimerSch中也没有了，不用重复释放 */
    // if (!invalid_)
    //     TimerSch::GetInstance().DelTimer(shared_from_this());
}

void Timer::Start(uint32_t timeo_ms, std::function<void(void *addr, bool release)> func)
{
    /** handleFunc_ 是空，说明还没有启动，此时可以启动 */
    if (handleFunc_ == nullptr && func) {
        timePoint_ = steady_clock::now() + duration<uint32_t, std::milli>(timeo_ms);
        handleFunc_ = func;
        TimerSch::GetInstance().AddTimer(shared_from_this());
    }
}

void Timer::UpdateTimeo(uint32_t timeo_ms)
{
    steady_clock::time_point tp = steady_clock::now() + duration<uint32_t, std::milli>(timeo_ms);
    TimerSch::GetInstance().UpdateTimer(shared_from_this(), tp);
}

void Timer::ForceTimeOut(bool release)
{
    if (!invalid_) {
        TimerSch::GetInstance().DelTimer(shared_from_this());
        DoHnadle(release);
    }
}

/** -------------------------------------------------------------------------------------------------------------------*/

/**
 * 使用条件变量超时等待去实现定时。在新加入一个定时器后，
 * 如果这个定时器需要等待的时间比现有的所有的定时器需要等待的时间都要短，那么就要先触发
 * 这个新加入的定时器任务，此时需要更改等待的时间，如果使用epoll或其他的方法，不好实现中途更改
 * 等待时间（能力有限，暂时没有想到使用epoll时有什么很好的办法处理这个情况）。
 * 使用条件变量等待则可以立刻触发条件，然后重新计算等待时间。
*/

TimerSch::TimerSch() : actived_(false)
{
    timers_.clear();
    thread_ = std::thread([&](){
        actived_ = true;
        while (actived_) {
            /** 触发信号量情况：
             * 1. 加入新的定时任务，触发信号量，重新等待
             * 2. 最小等待定时任务超时，触发信号量，处理超时
             */

            if (timers_.empty()) {
                //没有定时任务就一直等
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait_for(lock, duration<int, std::giga>(1));     //时间很大，相当于一直等
            } else {
                steady_clock::time_point now = steady_clock::now();
                //有定时任务，处理所有超时的任务，并从timers_中移除
                for (auto t = timers_.begin(); t != timers_.end();) {
                    if (t->first > now) {
                        break;
                    } else {
                        t->second->DoHnadle();
                        timers_.erase(t++);
                    }
                }

                //已经没有超时的任务了，根据最小定时任务进行等待
                if (!timers_.empty()) {
                    auto first_timer = timers_.begin();
                    duration<int, std::milli> timeo;
                    timeo = duration_cast<duration<int, std::milli>>(first_timer->first - now);

                    std::unique_lock<std::mutex> lock(mutex_);
                    cv_.wait_for(lock, timeo);
                }
            }
        }
    });
}

void TimerSch::UpdateTimer(const TimerPtr &t, const steady_clock::time_point &tp)
{

}

void TimerSch::AddTimer(const TimerPtr &t)
{
    std::lock_guard<std::mutex> lock(mutex_);
    timers_.emplace(t->GetTimePoint(), t);
    cv_.notify_one();
}

void TimerSch::DelTimer(const TimerPtr &t)
{
    std::lock_guard<std::mutex> lock(mutex_);
    using timer_it = std::multimap<steady_clock::time_point, TimerPtr>::iterator;
    std::pair<timer_it, timer_it> ts = timers_.equal_range(t->GetTimePoint());
    while (ts.first != ts.second) {
        if (ts.first->second->GetAddress() == t->GetAddress()) {
            timers_.erase(ts.first);
            break;
        }
        ts.first++;
    }
}

// #define TIMER_TEST
#ifdef TIMER_TEST
#include <iostream>
#include <ctime>
int main(int argc, char **argv)
{
    
    std::cout << "--------->> Start with Current time:" << duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() << std::endl;

    {
        TimerPtr t0 = Timer::CreateTimer();
        t0->Start(100, [](){
            std::cout << "--------->> timer t0 timeout, current time:" << \
                duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() << std::endl;
        });

        TimerPtr t1 = Timer::CreateTimer();
        t1->Start(1000, [](){
            std::cout << "--------->> timer t1 timeout, current time:" << \
                duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() << std::endl;
        });
    }

    TimerPtr t2 = Timer::CreateTimer();
    t2->Start(10, [](){
        std::cout << "--------->> timer t2 timeout, current time:" << \
                duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() << std::endl;
    });

    TimerPtr t3 = Timer::CreateTimer();
    t3->Start(100, [](){
        std::cout << "--------->> timer t3 timeout, current time:" << \
                duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() << std::endl;
    });

    while (getchar() != 'q')
        std::this_thread::sleep_for(std::chrono::seconds(1));

    return 0;
}
#endif