#include "PageCache.h"

namespace RainMemoPool
{

    void *PageCache::allocateSpan(size_t num_pages)
    {
        std::lock_guard<std::mutex> lock(mutex_value);

        // 查找合适的空闲span
        // lower_bound函数返回第一个大于等于num_pages的元素的迭代器
        auto it = free_spans.lower_bound(num_pages);
        if (it != free_spans.end())
        {
            Span *span = it->second;

            // 将取出的span从原有的空闲链表free_spans[it->first]中移除
            if (span->next)
            {
                free_spans[it->first] = span->next;
            }
            else
            {
                free_spans.erase(it);
            }

            // 如果span大于需要的num_pages则进行分割
            if (span->num_pages > num_pages)
            {
                Span *new_span = new Span;
                new_span->page_addr = static_cast<char *>(span->page_addr) +
                                      num_pages * PAGE_SIZE;
                new_span->num_pages = span->num_pages - num_pages;
                new_span->next = nullptr;

                // 将超出部分放回空闲Span*列表头部
                auto &list = free_spans[new_span->num_pages];
                new_span->next = list;
                list = new_span;

                span->num_pages = num_pages;
            }

            // 记录span信息用于回收
            span_map[span->page_addr] = span;
            return span->page_addr;
        }

        // 没有合适的span，向系统申请
        void *memory = systemAlloc(num_pages);
        if (!memory)
            return nullptr;

        // 创建新的span
        Span *span = new Span;
        span->page_addr = memory;
        span->num_pages = num_pages;
        span->next = nullptr;

        // 记录span信息用于回收
        span_map[memory] = span;
        return memory;
    }

    void PageCache::deallocateSpan(void *ptr, size_t num_pages)
    {
        std::lock_guard<std::mutex> lock(mutex_value);

        // 查找对应的span，没找到代表不是PageCache分配的内存，直接返回
        auto it = span_map.find(ptr);
        if (it == span_map.end())
            return;

        Span *span = it->second;

        // 尝试合并相邻的span
        void *next_addr = static_cast<char *>(ptr) + num_pages * PAGE_SIZE;
        auto next_it = span_map.find(next_addr);

        if (next_it != span_map.end())
        {
            Span *next_span = next_it->second;

            // 1. 首先检查next_span是否在空闲链表中
            bool found = false;
            auto &next_list = free_spans[next_span->num_pages];

            // 检查是否是头节点
            if (next_list == next_span)
            {
                next_list = next_span->next;
                found = true;
            }
            else if (next_list) // 只有在链表非空时才遍历
            {
                Span *prev = next_list;
                while (prev->next)
                {
                    if (prev->next == next_span)
                    {
                        // 将next_span从空闲链表中移除
                        prev->next = next_span->next;
                        found = true;
                        break;
                    }
                    prev = prev->next;
                }
            }

            // 2. 只有在找到next_span的情况下才进行合并
            if (found)
            {
                // 合并span
                span->num_pages += next_span->num_pages;
                span_map.erase(next_addr);
                delete next_span;
            }
        }

        // 将合并后的span通过头插法插入空闲列表
        auto &list = free_spans[span->num_pages];
        span->next = list;
        list = span;
    }

    void *PageCache::systemAlloc(size_t num_pages)
    {
        size_t size = num_pages * PAGE_SIZE;

        // 使用mmap分配内存
        void *ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (ptr == MAP_FAILED)
            return nullptr;

        // 清零内存
        memset(ptr, 0, size);
        return ptr;
    }

} // namespace RainMemoPool