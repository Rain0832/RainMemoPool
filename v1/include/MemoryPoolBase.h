#pragma once
#include <atomic>
#include <cstddef>

namespace RainMemory
{
  // 最大支持对象
  constexpr int MEMORY_POOL_NUM = 64;
  // 内存池管理的最小单元
  constexpr int SLOT_BASE_SIZE = 8;
  //  64 个不同大小的内存池
  constexpr int MAX_SLOT_SIZE = 512;

  struct Slot
  {
    // 内存槽 链表结点
    std::atomic<Slot *> next;
    // 数据域，这里只测试了内存池的分配和释放，不存储具体数据
  };

  // 抽象类，纯虚函数 + 公共变量
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
