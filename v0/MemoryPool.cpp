#include "MemoryPool.h"

namespace RainMemoPool
{
    MemoryPoolLock::MemoryPoolLock(size_t block_size)
        : block_size(block_size)
    {
    }

    MemoryPoolLock::~MemoryPoolLock()
    {
        SlotLock *cur = first_block;
        while (cur)
        {
            SlotLock *next = cur->next;
            // 等同于 free(reinterpret_cast<void*>(first_block);
            // 转化为 void 指针，因为 void 类型不需要调用析构函数，只释放空间
            operator delete(reinterpret_cast<void *>(cur));
            cur = next;
        }
    }

    void MemoryPoolLock::init(size_t size)
    {
        assert(size > 0);
        slot_size_ = size;
        first_block = nullptr;
        cur_slot = nullptr;
        free_list = nullptr;
        last_slot = nullptr;
    }

    void *MemoryPoolLock::allocate()
    {
        // 优先使用空闲链表中的内存槽
        if (free_list != nullptr)
        {
            std::lock_guard<std::mutex> lock(mutex_for_free_list);
            if (free_list != nullptr)
            {
                SlotLock *temp = free_list;
                free_list = free_list->next;
                return temp;
            }
        }
        SlotLock *temp;
        std::lock_guard<std::mutex> lock(mutex_for_block);
        if (cur_slot >= last_slot)
        {
            // 当前内存块已无内存槽可用，开辟一块新的内存
            allocateNewBlock();
        }
        temp = cur_slot;
        cur_slot += slot_size / sizeof(SlotLock);

        return temp;
    }

    void MemoryPoolLock::deallocate(void *ptr)
    {
        if (!ptr)
            return;

        std::lock_guard<std::mutex> lock(mutex_for_free_list);
        reinterpret_cast<SlotLock *>(ptr)->next = free_list;
        free_list = reinterpret_cast<SlotLock *>(ptr);
    }

    void MemoryPoolLock::allocateNewBlock()
    {
        // std::cout << "申请一块内存块，SlotSize: " << SlotSize_ << std::endl;
        // 头插法插入新的内存块
        void *new_block = operator new(block_size);
        reinterpret_cast<SlotLock *>(new_block)->next = first_block;
        first_block = reinterpret_cast<SlotLock *>(new_block);

        char *body = reinterpret_cast<char *>(new_block) + sizeof(SlotLock *);
        size_t padding_size = padPointer(body, slot_size_); // 计算对齐需要填充内存的大小
        cur_slot = reinterpret_cast<SlotLock *>(body + padding_size);

        // 超过该标记位置，则说明该内存块已无内存槽可用，需向系统申请新的内存块
        last_slot = reinterpret_cast<SlotLock *>(reinterpret_cast<size_t>(new_block) + block_size - slot_size_ + 1);
    }

    size_t MemoryPoolLock::padPointer(char *p, size_t align)
    {
        // align 是槽大小
        return align - (reinterpret_cast<size_t>(p) % align);
    }

    MemoryPool::MemoryPool(size_t block_size)
        : block_size(block_size),
          slot_size(0),
          first_block(nullptr),
          cur_slot(nullptr),
          free_list(nullptr),
          last_slot(nullptr)
    {
    }

    MemoryPool::~MemoryPool()
    {
        // 把连续的block删除
        Slot *cur = first_block;
        while (cur)
        {
            Slot *next = cur->next;
            // 等同于 free(reinterpret_cast<void*>(firstBlock_));
            // 转化为 void 指针，因为 void 类型不需要调用析构函数，只释放空间
            operator delete(reinterpret_cast<void *>(cur));
            cur = next;
        }
    }

    void MemoryPool::init(size_t size)
    {
        assert(size > 0);
        slot_size = size;
        first_block = nullptr;
        cur_slot = nullptr;
        free_list = nullptr;
        last_slot = nullptr;
    }

    void *MemoryPool::allocate()
    {
        // 优先使用空闲链表中的内存槽
        Slot *slot = popFreeList();
        if (slot != nullptr)
            return slot;

        Slot *temp;
        {
            std::lock_guard<std::mutex> lock(mutex_for_block);
            if (cur_slot >= last_slot)
            {
                // 当前内存块已无内存槽可用，开辟一块新的内存
                allocateNewBlock();
            }

            temp = cur_slot;
            // 这里不能直接 curSlot_ += SlotSize_ 因为curSlot_是Slot*类型，所以需要除以SlotSize_再加1
            cur_slot += slot_size / sizeof(Slot);
        }

        return temp;
    }

    void MemoryPool::deallocate(void *ptr)
    {
        if (!ptr)
            return;

        Slot *slot = reinterpret_cast<Slot *>(ptr);
        pushFreeList(slot);
    }

    void MemoryPool::allocateNewBlock()
    {
        // std::cout << "申请一块内存块，SlotSize: " << SlotSize_ << std::endl;
        //  头插法插入新的内存块
        void *new_block = operator new(block_size);
        reinterpret_cast<Slot *>(new_block)->next = first_block;
        first_block = reinterpret_cast<Slot *>(new_block);

        char *body = reinterpret_cast<char *>(new_block) + sizeof(Slot *);
        size_t padding_size = padPointer(body, slot_size); // 计算对齐需要填充内存的大小
        cur_slot = reinterpret_cast<Slot *>(body + padding_size);

        // 超过该标记位置，则说明该内存块已无内存槽可用，需向系统申请新的内存块
        last_slot = reinterpret_cast<Slot *>(reinterpret_cast<size_t>(new_block) + block_size - slot_size + 1);
    }

    // 让指针对齐到槽大小的倍数位置
    size_t MemoryPool::padPointer(char *p, size_t align)
    {
        // align 是槽大小
        return align - (reinterpret_cast<size_t>(p) % align);
    }

    // 实现无锁入队操作
    bool MemoryPool::pushFreeList(Slot *slot)
    {
        while (true)
        {
            // 获取当前头节点
            Slot *old_head = free_list.load(std::memory_order_relaxed);
            // 将新节点的 next 指向当前头节点
            slot->next.store(old_head, std::memory_order_relaxed);

            // 尝试将新节点设置为头节点
            if (free_list.compare_exchange_weak(
                    old_head,
                    slot,
                    std::memory_order_release,
                    std::memory_order_relaxed))
            {
                return true;
            }
            // 失败：说明另一个线程可能已经修改了 free_list
            // CAS 失败则重试
        }
    }

    // 实现无锁出队操作
    Slot *MemoryPool::popFreeList()
    {
        while (true)
        {
            Slot *old_head = free_list.load(std::memory_order_acquire);
            if (old_head == nullptr)
                return nullptr; // 队列为空

            // 在访问 new_head 之前再次验证 old_head 的有效性
            Slot *new_head = nullptr;
            try
            {
                new_head = old_head->next.load(std::memory_order_relaxed);
            }
            catch (...)
            {
                // 如果返回失败，则continue重新尝试申请内存
                continue;
            }

            // 尝试更新头结点
            // 原子性地尝试将 free_list 从 old_head 更新为 new_head
            if (free_list.compare_exchange_weak(old_head, new_head,
                                                std::memory_order_acquire,
                                                std::memory_order_relaxed))
            {
                return old_head;
            }
            // 失败：说明另一个线程可能已经修改了 free_list
            // CAS 失败则重试
        }
    }

    void HashBucket::initMemoryPool()
    {
        for (int i = 0; i < MEMORY_POOL_NUM; i++)
        {
            getMemoryPool(i).init((i + 1) * SLOT_BASE_SIZE);
        }
    }

    // 单例模式
    MemoryPool &HashBucket::getMemoryPool(int index)
    {
        static MemoryPool memory_pool[MEMORY_POOL_NUM];
        return memory_pool[index];
    }

} // namespace memoryPool
