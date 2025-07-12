#pragma once

#include <cassert>
#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>
#include <unordered_map>
#include "Common.h"
#include "PageCache.h"

namespace RainMemoPool
{

    // 使用无锁的span信息存储
    struct SpanTracker
    {
        std::atomic<void *> span_addr{nullptr};
        std::atomic<size_t> num_pages{0};
        std::atomic<size_t> block_count{0};
        std::atomic<size_t> free_count{0}; // 用于追踪spn中还有多少块是空闲的，如果所有块都空闲，则归还span给PageCache
    };

    class CentralCache
    {
    public:
        static CentralCache &getInstance()
        {
            static CentralCache instance;
            return instance;
        }

        void *fetchRange(size_t index);
        void returnRange(void *start, size_t size, size_t index);

    private:
        // 相互是还所有原子指针为nullptr
        CentralCache();
        // 从页缓存获取内存
        void *fetchFromPageCache(size_t size);

        // 获取span信息
        SpanTracker *getSpanTracker(void *block_addr);

        // 更新span的空闲计数并检查是否可以归还
        void updateSpanFreeCount(SpanTracker *tracker, size_t new_free_blocks, size_t index);

    private:
        // 中心缓存的自由链表
        std::array<std::atomic<void *>, FREE_LIST_SIZE> central_free_list;

        // 用于同步的自旋锁
        std::array<std::atomic_flag, FREE_LIST_SIZE> locks;

        // 使用数组存储span信息，避免map的开销
        std::array<SpanTracker, 1024> span_trackers;
        std::atomic<size_t> span_count{0};

        // 延迟归还相关的成员变量
        static const size_t MAX_DELAY_COUNT = 48;                                            // 最大延迟计数
        std::array<std::atomic<size_t>, FREE_LIST_SIZE> delay_counts;                        // 每个大小类的延迟计数
        std::array<std::chrono::steady_clock::time_point, FREE_LIST_SIZE> last_return_times; // 上次归还时间
        static const std::chrono::milliseconds DELAY_INTERVAL;                               // 延迟间隔

        bool shouldPerformDelayedReturn(size_t index, size_t current_count, std::chrono::steady_clock::time_point current_time);
        void performDelayedReturn(size_t index);
    };

} // namespace memoryPool