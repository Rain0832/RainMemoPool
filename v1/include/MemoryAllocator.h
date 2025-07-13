#pragma once
#include <array>
#include <memory>
#include "MemoryPoolBase.h"
#include "MemoryPoolAtomic.h"
#include "MemoryPoolLock.h"

namespace RainMemory
{

  class MemoryAllocator
  {
  public:
    enum class Strategy
    {
      Lock,
      Atomic
    };

    static void init(Strategy strategy = Strategy::Atomic)
    {
      strategy_ = strategy;
      for (int i = 0; i < MEMORY_POOL_NUM; ++i)
      {
        pools_[i]->init((i + 1) * SLOT_BASE_SIZE);
      }
    }

    static void *allocate(size_t size)
    {
      if (size > MAX_SLOT_SIZE)
        return ::operator new(size);
      int index = (size + SLOT_BASE_SIZE - 1) / SLOT_BASE_SIZE - 1;
      return pools_[index]->allocate();
    }

    static void deallocate(void *ptr, size_t size)
    {
      if (!ptr)
        return;
      if (size > MAX_SLOT_SIZE)
      {
        ::operator delete(ptr);
        return;
      }
      int index = (size + SLOT_BASE_SIZE - 1) / SLOT_BASE_SIZE - 1;
      pools_[index]->deallocate(ptr);
    }

    template <typename T, typename... Args>
    static T *newElement(Args &&...args)
    {
      void *p = allocate(sizeof(T));
      return new (p) T(std::forward<Args>(args)...);
    }

    template <typename T>
    static void deleteElement(T *p)
    {
      if (p)
      {
        p->~T();
        deallocate(p, sizeof(T));
      }
    }

  private:
    static inline Strategy strategy_ = Strategy::Atomic;
    static inline std::array<std::unique_ptr<MemoryPoolBase>, MEMORY_POOL_NUM> pools_ = []
    {
      std::array<std::unique_ptr<MemoryPoolBase>, MEMORY_POOL_NUM> arr;
      for (int i = 0; i < MEMORY_POOL_NUM; ++i)
      {
        arr[i] = strategy_ == Strategy::Lock
                     ? std::unique_ptr<MemoryPoolBase>(std::make_unique<MemoryPoolLock>())
                     : std::unique_ptr<MemoryPoolBase>(std::make_unique<MemoryPoolAtomic>());
      }
      return arr;
    }();
  };

} // namespace RainMemory
