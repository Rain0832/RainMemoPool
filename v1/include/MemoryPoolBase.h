#pragma once
#include <atomic>
#include <cstddef>

namespace RainMemory
{

  constexpr int MEMORY_POOL_NUM = 64;
  constexpr int SLOT_BASE_SIZE = 8;
  constexpr int MAX_SLOT_SIZE = 512;

  struct Slot
  {
    std::atomic<Slot *> next;
  };

  class MemoryPoolBase
  {
  public:
    MemoryPoolBase(size_t block_size = 4096) : block_size_(block_size) {}
    virtual ~MemoryPoolBase() {}

    virtual void init(size_t slot_size) = 0;
    virtual void *allocate() = 0;
    virtual void deallocate(void *ptr) = 0;

  protected:
    size_t block_size_;
    size_t slot_size_;
  };

} // namespace RainMemory
