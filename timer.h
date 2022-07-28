#ifndef __MGW_UTIL_TIMER_H__
#define __MGW_UTIL_TIMER_H__

#include <stdint.h>
#include <functional>
#include <chrono>
#include <memory>
#include <atomic>

using namespace std::chrono;

/** 可以从Timer类内返回智能指针 shread_frome_this(), 
 * 在这个对象内调用其他处理，需要把该对象的智能指针传递过去，
 * 此时只能使用shread_frome_this()才能确保智能指针引用计数正确安全。
 * */
class Timer;
using TimerPtr = std::shared_ptr<Timer>;

class Timer : public std::enable_shared_from_this<Timer>
{
public:
    static TimerPtr CreateTimer() {
        return std::shared_ptr<Timer>(new Timer);
    }

    ~Timer();

    void Start(uint32_t timeo_ms, std::function<void(void *addr)> func);
    void UpdateTimeo(uint32_t timeo);
    void ForceTimeOut(void);
    steady_clock::time_point GetTimePoint(void) const {
        return timePoint_;
    }
    bool Invalid(void) const { return invalid_; }

    /** Do not call this function by Timer */
    inline void DoHnadle(void) { 
        handleFunc_(addr_);
        invalid_ = true;
    }
    void *GetAddress(void) const { return addr_; }

    /** Do not copy, otherwise delete timer from TimerSch by Accidentally 
     *  You are better use shared_ptr than object to copy
    */
    Timer(const Timer &t) = delete;
    Timer operator=(const Timer &t) = delete;

private:
    Timer();

    steady_clock::time_point    timePoint_;
    std::function<void(void *addr)>       handleFunc_;
    std::atomic<bool>           invalid_;

    void                        *addr_;
};

#endif  //__MGW_UTIL_TIMER_H__