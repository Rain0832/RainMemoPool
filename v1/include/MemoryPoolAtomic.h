#pragma once
#include <atomic>
#include <mutex>
#include "MemoryPoolBase.h"

namespace RainMemory
{

  class MemoryPoolAtomic : public MemoryPoolBase
  {
  public:
    using MemoryPoolBase::MemoryPoolBase;
    ~MemoryPoolAtomic();

    void init(size_t slot_size) override;
    void *allocate() override;
    void deallocate(void *ptr) override;

  private:
    void allocateNewBlock();
    size_t padPointer(char *p, size_t align);

    bool pushFreeList(Slot *slot);
    Slot *popFreeList();

  private:
    Slot *first_block_ = nullptr;
    Slot *cur_slot_ = nullptr;
    Slot *last_slot_ = nullptr;
    std::atomic<Slot *> free_list_ = {};

    std::mutex mutex_block_;
  };

} // namespace RainMemory
