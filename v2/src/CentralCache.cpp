#include "CentralCache.h"

namespace RainMemoPool
{
    const std::chrono::milliseconds CentralCache::DELAY_INTERVAL{1000};

    // 每次从PageCache获取span大小（以页为单位）
    static const size_t SPAN_PAGES = 8;

    CentralCache::CentralCache()
    {
        for (auto &ptr : central_free_list)
        {
            ptr.store(nullptr, std::memory_order_relaxed);
        }
        for (auto &lock : locks)
        {
            lock.clear();
        }
        // 初始化延迟归还相关的成员变量
        for (auto &count : delay_counts)
        {
            count.store(0, std::memory_order_relaxed);
        }
        for (auto &time : last_return_times)
        {
            time = std::chrono::steady_clock::now();
        }
        span_count.store(0, std::memory_order_relaxed);
    }

    void *CentralCache::fetchRange(size_t index)
    {
        // 索引检查，当索引大于等于FREE_LIST_SIZE时，说明申请内存过大应直接向系统申请
        if (index >= FREE_LIST_SIZE)
            return nullptr;

        // 自旋锁保护
        while (locks[index].test_and_set(std::memory_order_acquire))
        {
            std::this_thread::yield(); // 添加线程让步，避免忙等待，避免过度消耗CPU
        }

        void *result = nullptr;
        try
        {
            // 尝试从中心缓存获取内存块
            result = central_free_list[index].load(std::memory_order_relaxed);

            if (!result)
            {
                // 如果中心缓存为空，从页缓存获取新的内存块
                size_t size = (index + 1) * ALIGNMENT;
                result = fetchFromPageCache(size);

                if (!result)
                {
                    locks[index].clear(std::memory_order_release);
                    return nullptr;
                }

                // 将获取的内存块切分成小块
                char *start = static_cast<char *>(result);

                // 计算实际分配的页数
                size_t num_pages = (size <= SPAN_PAGES * PageCache::PAGE_SIZE) ? SPAN_PAGES : (size + PageCache::PAGE_SIZE - 1) / PageCache::PAGE_SIZE;
                // 使用实际页数计算块数
                size_t block_num = (num_pages * PageCache::PAGE_SIZE) / size;

                if (block_num > 1)
                { // 确保至少有两个块才构建链表
                    for (size_t i = 1; i < block_num; ++i)
                    {
                        void *current = start + (i - 1) * size;
                        void *next = start + i * size;
                        *reinterpret_cast<void **>(current) = next;
                    }
                    *reinterpret_cast<void **>(start + (block_num - 1) * size) = nullptr;

                    // 保存result的下一个节点
                    void *next = *reinterpret_cast<void **>(result);
                    // 将result与链表断开
                    *reinterpret_cast<void **>(result) = nullptr;
                    // 更新中心缓存
                    central_free_list[index].store(
                        next,
                        std::memory_order_release);

                    // 使用无锁方式记录span信息
                    // 做记录是为了将中心缓存多余内存块归还给页缓存做准备。考虑点：
                    // 1.CentralCache 管理的是小块内存，这些内存可能不连续
                    // 2.PageCache 的 deallocateSpan 要求归还连续的内存
                    size_t tracker_index = span_count++;
                    if (tracker_index < span_trackers.size())
                    {
                        span_trackers[tracker_index].span_addr.store(start, std::memory_order_release);
                        span_trackers[tracker_index].num_pages.store(num_pages, std::memory_order_release);
                        span_trackers[tracker_index].block_count.store(block_num, std::memory_order_release);    // 共分配了block_num个内存块
                        span_trackers[tracker_index].free_count.store(block_num - 1, std::memory_order_release); // 第一个块result已被分配出去，所以初始空闲块数为blockNum - 1
                    }
                }
            }
            else
            {
                // 保存result的下一个节点
                void *next = *reinterpret_cast<void **>(result);
                // 将result与链表断开
                *reinterpret_cast<void **>(result) = nullptr;

                // 更新中心缓存
                central_free_list[index].store(next, std::memory_order_release);

                // 更新span的空闲计数
                SpanTracker *tracker = getSpanTracker(result);
                if (tracker)
                {
                    // 减少一个空闲块
                    tracker->free_count.fetch_sub(1, std::memory_order_release);
                }
            }
        }
        catch (...)
        {
            locks[index].clear(std::memory_order_release);
            throw;
        }

        // 释放锁
        locks[index].clear(std::memory_order_release);
        return result;
    }

    void CentralCache::returnRange(void *start, size_t size, size_t index)
    {
        if (!start || index >= FREE_LIST_SIZE)
            return;

        size_t block_size = (index + 1) * ALIGNMENT;
        size_t block_count = size / block_size;

        while (locks[index].test_and_set(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }

        try
        {
            // 1. 将归还的链表连接到中心缓存
            void *end = start;
            size_t count = 1;
            while (*reinterpret_cast<void **>(end) != nullptr && count < block_count)
            {
                end = *reinterpret_cast<void **>(end);
                count++;
            }
            void *current = central_free_list[index].load(std::memory_order_relaxed);
            *reinterpret_cast<void **>(end) = current; // 头插法（将原有链表接在归还链表后边）
            central_free_list[index].store(start, std::memory_order_release);

            // 2. 更新延迟计数
            size_t current_count = delay_counts[index].fetch_add(1, std::memory_order_relaxed) + 1;
            auto current_time = std::chrono::steady_clock::now();

            // 3. 检查是否需要执行延迟归还
            if (shouldPerformDelayedReturn(index, current_count, current_time))
            {
                performDelayedReturn(index);
            }
        }
        catch (...)
        {
            locks[index].clear(std::memory_order_release);
            throw;
        }

        locks[index].clear(std::memory_order_release);
    }

    // 检查是否需要执行延迟归还
    bool CentralCache::shouldPerformDelayedReturn(size_t index, size_t current_count,
                                                  std::chrono::steady_clock::time_point current_time)
    {
        // 基于计数和时间的双重检查
        if (current_count >= MAX_DELAY_COUNT)
        {
            return true;
        }

        auto last_time = last_return_times[index];
        return (current_time - last_time) >= DELAY_INTERVAL;
    }

    // 执行延迟归还
    void CentralCache::performDelayedReturn(size_t index)
    {
        // 重置延迟计数
        delay_counts[index].store(0, std::memory_order_relaxed);
        // 更新最后归还时间
        last_return_times[index] = std::chrono::steady_clock::now();

        // 统计每个span的空闲块数
        std::unordered_map<SpanTracker *, size_t> span_free_counts;
        void *current_block = central_free_list[index].load(std::memory_order_relaxed);

        while (current_block)
        {
            SpanTracker *tracker = getSpanTracker(current_block);
            if (tracker)
            {
                span_free_counts[tracker]++;
            }
            current_block = *reinterpret_cast<void **>(current_block);
        }

        // 更新每个span的空闲计数并检查是否可以归还
        for (const auto &[tracker, new_free_blocks] : span_free_counts)
        {
            updateSpanFreeCount(tracker, new_free_blocks, index);
        }
    }

    void CentralCache::updateSpanFreeCount(SpanTracker *tracker, size_t new_free_blocks, size_t index)
    {
        size_t old_free_count = tracker->free_count.load(std::memory_order_relaxed);
        size_t new_free_count = old_free_count + new_free_blocks;
        tracker->free_count.store(new_free_count, std::memory_order_release);

        // 如果所有块都空闲，归还span
        if (new_free_count == tracker->block_count.load(std::memory_order_relaxed))
        {
            void *span_addr = tracker->span_addr.load(std::memory_order_relaxed);
            size_t num_pages = tracker->num_pages.load(std::memory_order_relaxed);

            // 从自由链表中移除这些块
            void *head = central_free_list[index].load(std::memory_order_relaxed);
            void *new_head = nullptr;
            void *prev = nullptr;
            void *current = head;

            while (current)
            {
                void *next = *reinterpret_cast<void **>(current);
                if (current >= span_addr &&
                    current < static_cast<char *>(span_addr) + num_pages * PageCache::PAGE_SIZE)
                {
                    if (prev)
                    {
                        *reinterpret_cast<void **>(prev) = next;
                    }
                    else
                    {
                        new_head = next;
                    }
                }
                else
                {
                    prev = current;
                }
                current = next;
            }

            central_free_list[index].store(new_head, std::memory_order_release);
            PageCache::getInstance().deallocateSpan(span_addr, num_pages);
        }
    }

    void *CentralCache::fetchFromPageCache(size_t size)
    {
        // 1. 计算实际需要的页数
        size_t numPages = (size + PageCache::PAGE_SIZE - 1) / PageCache::PAGE_SIZE;

        // 2. 根据大小决定分配策略
        if (size <= SPAN_PAGES * PageCache::PAGE_SIZE)
        {
            // 小于等于32KB的请求，使用固定8页
            return PageCache::getInstance().allocateSpan(SPAN_PAGES);
        }
        else
        {
            // 大于32KB的请求，按实际需求分配
            return PageCache::getInstance().allocateSpan(numPages);
        }
    }

    SpanTracker *CentralCache::getSpanTracker(void *block_addr)
    {
        // 遍历span_trackers数组，找到block_addr所属的span
        for (size_t i = 0; i < span_count.load(std::memory_order_relaxed); ++i)
        {
            void *spanAddr = span_trackers[i].span_addr.load(std::memory_order_relaxed);
            size_t numPages = span_trackers[i].num_pages.load(std::memory_order_relaxed);

            if (block_addr >= spanAddr &&
                block_addr < static_cast<char *>(spanAddr) + numPages * PageCache::PAGE_SIZE)
            {
                return &span_trackers[i];
            }
        }
        return nullptr;
    }

} // namespace memoryPool