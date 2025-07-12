#include "ThreadCache.h"

namespace RainMemoPool
{
    void *ThreadCache::allocate(size_t size)
    {
        // 处理0大小的分配请求
        if (size == 0)
        {
            size = ALIGNMENT; // 至少分配一个对齐大小
        }

        if (size > MAX_BYTES)
        {
            // 大对象直接从系统分配
            return malloc(size);
        }

        size_t index = SizeClass::getIndex(size);

        // 更新对应自由链表的长度计数
        free_list_size[index]--;

        // 检查线程本地自由链表
        // 如果 free_list[index] 不为空，表示该链表中有可用内存块
        if (void *ptr = free_list[index])
        {
            free_list[index] = *reinterpret_cast<void **>(ptr); // 将free_list[index]指向的内存块的下一个内存块地址（取决于内存块的实现）
            return ptr;
        }

        // 如果线程本地自由链表为空，则从中心缓存获取一批内存
        return fetchFromCentralCache(index);
    }

    void ThreadCache::deallocate(void *ptr, size_t size)
    {
        if (size > MAX_BYTES)
        {
            free(ptr);
            return;
        }

        size_t index = SizeClass::getIndex(size);

        // 插入到线程本地自由链表
        *reinterpret_cast<void **>(ptr) = free_list[index];
        free_list[index] = ptr;

        // 更新对应自由链表的长度计数
        free_list_size[index]++;

        // 判断是否需要将部分内存回收给中心缓存
        if (shouldReturnToCentralCache(index))
        {
            returnToCentralCache(free_list[index], size);
        }
    }

    // 判断是否需要将内存回收给中心缓存
    bool ThreadCache::shouldReturnToCentralCache(size_t index)
    {
        // 设定阈值，例如：当自由链表的大小超过一定数量时
        size_t threshold = 256;
        return (free_list_size[index] > threshold);
    }

    void *ThreadCache::fetchFromCentralCache(size_t index)
    {
        // 从中心缓存批量获取内存
        void *start = CentralCache::getInstance().fetchRange(index);
        if (!start)
            return nullptr;

        // 取一个返回，其余放入自由链表
        void *result = start;
        free_list[index] = *reinterpret_cast<void **>(start);

        // 更新自由链表大小
        size_t batch_num = 0;
        void *current = start; // 从start开始遍历

        // 计算从中心缓存获取的内存块数量
        while (current != nullptr)
        {
            batch_num++;
            current = *reinterpret_cast<void **>(current); // 遍历下一个内存块
        }

        // 更新free_list_size，增加获取的内存块数量
        free_list_size[index] += batch_num;

        return result;
    }

    void ThreadCache::returnToCentralCache(void *start, size_t size)
    {
        // 根据大小计算对应的索引
        size_t index = SizeClass::getIndex(size);

        // 获取对齐后的实际块大小
        size_t alignedSize = SizeClass::roundUp(size);

        // 计算要归还内存块数量
        size_t batch_num = free_list_size[index];
        if (batch_num <= 1)
            return; // 如果只有一个块，则不归还

        // 保留一部分在ThreadCache中（比如保留1/4）
        size_t keepNum = std::max(batch_num / 4, size_t(1));
        size_t returnNum = batch_num - keepNum;

        // 将内存块串成链表
        char *current = static_cast<char *>(start);
        // 使用对齐后的大小计算分割点
        char *splitNode = current;
        for (size_t i = 0; i < keepNum - 1; ++i)
        {
            splitNode = reinterpret_cast<char *>(*reinterpret_cast<void **>(splitNode));
            if (splitNode == nullptr)
            {
                // 如果链表提前结束，更新实际的返回数量
                returnNum = batch_num - (i + 1);
                break;
            }
        }

        if (splitNode != nullptr)
        {
            // 将要返回的部分和要保留的部分断开
            void *nextNode = *reinterpret_cast<void **>(splitNode);
            *reinterpret_cast<void **>(splitNode) = nullptr; // 断开连接

            // 更新ThreadCache的空闲链表
            free_list[index] = start;

            // 更新自由链表大小
            free_list_size[index] = keepNum;

            // 将剩余部分返回给CentralCache
            if (returnNum > 0 && nextNode != nullptr)
            {
                CentralCache::getInstance().returnRange(nextNode, returnNum * alignedSize, index);
            }
        }
    }

} // namespace RainMemoPool