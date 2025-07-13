#include "MemoryPoolLock.h"

namespace RainMemory
{

  MemoryPoolLock::~MemoryPoolLock()
  {
    Slot *cur = first_block_;
    while (cur)
    {
      Slot *next = cur->next.load();
      operator delete(reinterpret_cast<void *>(cur));
      cur = next;
    }
  }

  void MemoryPoolLock::init(size_t slot_size)
  {
    slot_size_ = slot_size;
    first_block_ = cur_slot_ = last_slot_ = free_list_ = nullptr;
  }

  void *MemoryPoolLock::allocate()
  {
    {
      std::lock_guard<std::mutex> lock(mutex_free_);
      if (free_list_)
      {
        Slot *temp = free_list_;
        free_list_ = free_list_->next.load();
        return temp;
      }
    }

    std::lock_guard<std::mutex> lock(mutex_block_);
    if (cur_slot_ >= last_slot_)
    {
      allocateNewBlock();
    }

    Slot *temp = cur_slot_;
    cur_slot_ = reinterpret_cast<Slot *>(reinterpret_cast<char *>(cur_slot_) + slot_size_);
    return temp;
  }

  void MemoryPoolLock::deallocate(void *ptr)
  {
    if (!ptr)
      return;
    std::lock_guard<std::mutex> lock(mutex_free_);
    Slot *slot = reinterpret_cast<Slot *>(ptr);
    slot->next.store(free_list_);
    free_list_ = slot;
  }

  void MemoryPoolLock::allocateNewBlock()
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

  size_t MemoryPoolLock::padPointer(char *p, size_t align)
  {
    size_t misalign = reinterpret_cast<size_t>(p) % align;
    return misalign ? (align - misalign) : 0;
  }

} // namespace RainMemory