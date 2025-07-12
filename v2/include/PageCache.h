#pragma once
#include <cstring>
#include <map>
#include <mutex>
#include <sys/mman.h>
#include "Common.h"

namespace RainMemoPool
{
    class PageCache
    {
    public:
        static const size_t PAGE_SIZE = 4096; // 4K页大小

        static PageCache &getInstance()
        {
            static PageCache instance;
            return instance;
        }

        // 分配指定页数的span
        void *allocateSpan(size_t num_pages);

        // 释放span
        void deallocateSpan(void *ptr, size_t num_pages);

    private:
        PageCache() = default;

        // 向系统申请内存
        void *systemAlloc(size_t num_pages);

    private:
        struct Span
        {
            void *page_addr;  // 页起始地址
            size_t num_pages; // 页数
            Span *next;       // 链表指针
        };

        // 按页数管理空闲span，不同页数对应不同Span链表
        std::map<size_t, Span *> free_spans;
        // 页号到span的映射，用于回收
        std::map<void *, Span *> span_map;
        std::mutex mutex_value;
    };

} // namespace RainMemoPool