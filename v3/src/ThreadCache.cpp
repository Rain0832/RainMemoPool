#include "ThreadCache.h"

namespace Kama_memoryPool
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

        // 更新自由链表大小
        freeListSize_[index]--;

        // 检查线程本地自由链表
        // 如果 freeList_[index] 不为空，表示该链表中有可用内存块
        if (void *ptr = freeList_[index])
        {
            freeList_[index] = *reinterpret_cast<void **>(ptr); // 将freeList_[index]指向的内存块的下一个内存块地址（取决于内存块的实现）
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
        *reinterpret_cast<void **>(ptr) = freeList_[index];
        freeList_[index] = ptr;

        // 更新自由链表大小
        freeListSize_[index]++; // 增加对应大小类的自由链表大小

        // 判断是否需要将部分内存回收给中心缓存
        if (shouldReturnToCentralCache(index))
        {
            returnToCentralCache(freeList_[index], size);
        }
    }

    // 判断是否需要将内存回收给中心缓存
    bool ThreadCache::shouldReturnToCentralCache(size_t index)
    {
        // 设定阈值，例如：当自由链表的大小超过一定数量时
        size_t threshold = 64; // 例如，64个内存块
        return (freeListSize_[index] > threshold);
    }

    void *ThreadCache::fetchFromCentralCache(size_t index)
    {
        size_t size = (index + 1) * ALIGNMENT;
        // 根据对象内存大小计算批量获取的数量
        size_t batchNum = getBatchNum(size);
        // 从中心缓存批量获取内存
        void *start = CentralCache::getInstance().fetchRange(index, batchNum);
        if (!start)
            return nullptr;

        // 更新自由链表大小
        freeListSize_[index] += batchNum; // 增加对应大小类的自由链表大小

        // 取一个返回，其余放入线程本地自由链表
        void *result = start;
        if (batchNum > 1)
        {
            freeList_[index] = *reinterpret_cast<void **>(start);
        }

        return result;
    }

    void ThreadCache::returnToCentralCache(void *start, size_t size)
    {
        // 根据大小计算对应的索引
        size_t index = SizeClass::getIndex(size);

        // 获取对齐后的实际块大小
        size_t alignedSize = SizeClass::roundUp(size);

        // 计算要归还内存块数量
        size_t batchNum = freeListSize_[index];
        if (batchNum <= 1)
            return; // 如果只有一个块，则不归还

        // 保留一部分在ThreadCache中（比如保留1/4）
        size_t keepNum = std::max(batchNum / 4, size_t(1));
        size_t returnNum = batchNum - keepNum;

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
                returnNum = batchNum - (i + 1);
                break;
            }
        }

        if (splitNode != nullptr)
        {
            // 将要返回的部分和要保留的部分断开
            void *nextNode = *reinterpret_cast<void **>(splitNode);
            *reinterpret_cast<void **>(splitNode) = nullptr; // 断开连接

            // 更新ThreadCache的空闲链表
            freeList_[index] = start;

            // 更新自由链表大小
            freeListSize_[index] = keepNum;

            // 将剩余部分返回给CentralCache
            if (returnNum > 0 && nextNode != nullptr)
            {
                CentralCache::getInstance().returnRange(nextNode, returnNum * alignedSize, index);
            }
        }
    }

    // 计算批量获取内存块的数量
    size_t ThreadCache::getBatchNum(size_t size)
    {
        // 基准：每次批量获取不超过4KB内存
        constexpr size_t MAX_BATCH_SIZE = 4 * 1024; // 4KB

        // 根据对象大小设置合理的基准批量数
        size_t baseNum;
        if (size <= 32)
            baseNum = 64; // 64 * 32 = 2KB
        else if (size <= 64)
            baseNum = 32; // 32 * 64 = 2KB
        else if (size <= 128)
            baseNum = 16; // 16 * 128 = 2KB
        else if (size <= 256)
            baseNum = 8; // 8 * 256 = 2KB
        else if (size <= 512)
            baseNum = 4; // 4 * 512 = 2KB
        else if (size <= 1024)
            baseNum = 2; // 2 * 1024 = 2KB
        else
            baseNum = 1; // 大于1024的对象每次只从中心缓存取1个

        // 计算最大批量数
        size_t maxNum = std::max(size_t(1), MAX_BATCH_SIZE / size);

        // 取最小值，但确保至少返回1
        return std::max(sizeof(1), std::min(maxNum, baseNum));
    }

} // namespace memoryPool