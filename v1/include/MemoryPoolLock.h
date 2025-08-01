#pragma once
#include <mutex>
#include "MemoryPoolBase.h"

namespace RainMemory
{
  // 继承 Base 抽象类
  class MemoryPoolLock : public MemoryPoolBase
  {
  public:
    using MemoryPoolBase::MemoryPoolBase;
    ~MemoryPoolLock();

    void init(size_t slot_size) override;
    void *allocate() override;
    void deallocate(void *ptr) override;

  private:
    void allocateNewBlock();
    size_t padPointer(char *p, size_t align);

  private:
    Slot *first_block_ = nullptr;
    Slot *cur_slot_ = nullptr;
    Slot *last_slot_ = nullptr;
    // 普通链表 + 互斥锁
    Slot *free_list_ = nullptr;

    std::mutex mutex_block_;
    std::mutex mutex_free_;
  };

} // namespace RainMemory
