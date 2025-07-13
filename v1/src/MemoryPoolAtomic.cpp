#include "MemoryPoolAtomic.h"

namespace RainMemory
{

  MemoryPoolAtomic::~MemoryPoolAtomic()
  {
    Slot *cur = first_block_;
    while (cur)
    {
      Slot *next = cur->next.load();
      operator delete(reinterpret_cast<void *>(cur));
      cur = next;
    }
  }

  void MemoryPoolAtomic::init(size_t slot_size)
  {
    slot_size_ = slot_size;
    first_block_ = cur_slot_ = last_slot_ = nullptr;
    free_list_.store(nullptr);
  }

  void *MemoryPoolAtomic::allocate()
  {
    Slot *slot = popFreeList();
    if (slot)
      return slot;

    std::lock_guard<std::mutex> lock(mutex_block_);
    if (cur_slot_ >= last_slot_)
    {
      allocateNewBlock();
    }
    Slot *temp = cur_slot_;
    cur_slot_ = reinterpret_cast<Slot *>(reinterpret_cast<char *>(cur_slot_) + slot_size_);
    return temp;
  }

  void MemoryPoolAtomic::deallocate(void *ptr)
  {
    if (!ptr)
      return;
    Slot *slot = reinterpret_cast<Slot *>(ptr);
    pushFreeList(slot);
  }

  void MemoryPoolAtomic::allocateNewBlock()
  {
    void *block = operator new(block_size_);
    Slot *block_head = reinterpret_cast<Slot *>(block);
    block_head->next.store(first_block_);
    first_block_ = block_head;

    char *body = reinterpret_cast<char *>(block) + sizeof(Slot *);
    size_t padding = padPointer(body, slot_size_);
    cur_slot_ = reinterpret_cast<Slot *>(body + padding);
    last_slot_ = reinterpret_cast<Slot *>(reinterpret_cast<size_t>(block) + block_size_ - slot_size_ + 1);
  }

  size_t MemoryPoolAtomic::padPointer(char *p, size_t align)
  {
    size_t misalign = reinterpret_cast<size_t>(p) % align;
    return misalign ? (align - misalign) : 0;
  }

  bool MemoryPoolAtomic::pushFreeList(Slot *slot)
  {
    // 原子操作
    while (true)
    {
      Slot *old_head = free_list_.load(std::memory_order_relaxed);
      slot->next.store(old_head, std::memory_order_relaxed);
      if (free_list_.compare_exchange_weak(old_head, slot,
                                           std::memory_order_release,
                                           std::memory_order_relaxed))
      {
        return true;
      }
    }
  }

  Slot *MemoryPoolAtomic::popFreeList()
  {
    // 原子操作
    while (true)
    {
      Slot *old_head = free_list_.load(std::memory_order_acquire);
      if (!old_head)
        return nullptr;

      Slot *new_head = old_head->next.load(std::memory_order_relaxed);
      if (free_list_.compare_exchange_weak(old_head, new_head,
                                           std::memory_order_acquire,
                                           std::memory_order_relaxed))
      {
        return old_head;
      }
    }
  }

} // namespace RainMemory
