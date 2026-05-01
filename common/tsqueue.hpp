// https://www.geeksforgeeks.org/implement-thread-safe-queue-in-c/
#ifndef __TSQUEUE_HPP__
#define __TSQUEUE_HPP__

#define USE_CIRCULAR_BUFFER (1) // 0: stl queue(memory leak but fast), 1: boost circular buffer

#include <condition_variable>
#include <mutex>
#if USE_CIRCULAR_BUFFER == 0
    #include <queue>
#else
    // sudo apt install libboost-all-dev
    #include <boost/circular_buffer.hpp>
#endif

// Thread-safe queue 
template <typename T> 
class TSQueue { 
private: 
    // Underlying queue
    #if USE_CIRCULAR_BUFFER == 0
    std::queue<T> m_queue; 
    #else
    boost::circular_buffer<T> m_queue;
    #endif
  
    // mutex for thread synchronization 
    std::mutex m_mutex; 
  
    // Condition variable for signaling 
    std::condition_variable m_cond; 
  
public:
    TSQueue()
    {
        #if USE_CIRCULAR_BUFFER == 0
        #else
        m_queue.set_capacity(1024);
        #endif
    }

    ~TSQueue()
    {

    }

    // Pushes an element to the queue 
    void push(T item) 
    {
        // Acquire lock 
        std::unique_lock<std::mutex> lock(m_mutex); 
  
        // Add item
        #if USE_CIRCULAR_BUFFER == 0
        m_queue.push(item); 
        #else
        m_queue.push_back(item);
        #endif
  
        // Notify one thread that 
        // is waiting 
        m_cond.notify_one(); 
    } 
  
    // Pops an element off the queue 
    T pop() 
    { 
        // acquire lock 
        std::unique_lock<std::mutex> lock(m_mutex); 
  
        // wait until queue is not empty 
        m_cond.wait(lock, 
                    [this]() { return !m_queue.empty(); }); 
  
        // retrieve item
        T item = m_queue.front(); 
        #if USE_CIRCULAR_BUFFER == 0
        m_queue.pop();
        #else
        m_queue.pop_front();
        #endif
  
        // return item 
        return item; 
    }

    int size()
    {
        return m_queue.size();
    }
};

#endif // __TSQUEUE_HPP__