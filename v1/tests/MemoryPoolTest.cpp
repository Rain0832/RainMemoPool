#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "MemoryAllocator.h"

using namespace RainMemory;

#define TIMES 500
#define WORKS 5
#define ROUNDS 50

#define SMALL_LEVEL 1
#define MID_LEVEL 5
#define BIG_LEVEL 10
#define LARGE_LEVEL 20

class TestSmallLevel
{
	int id[SMALL_LEVEL];
};
class TestMidLevel
{
	int id[MID_LEVEL];
};
class TestBigLevel
{
	int id[BIG_LEVEL];
};
class TestLargeLevel
{
	int id[LARGE_LEVEL];
};

void BenchmarkMemoryPool(size_t ntimes, size_t nworks, size_t rounds)
{
	std::vector<std::thread> threads(nworks);
	auto total_start = std::chrono::steady_clock::now();

	for (size_t k = 0; k < nworks; ++k)
	{
		threads[k] = std::thread([=]()
														 {
            for (size_t j = 0; j < rounds; ++j) {
                for (size_t i = 0; i < ntimes; ++i) {
                    auto* p1 = MemoryAllocator::newElement<TestSmallLevel>();
                    MemoryAllocator::deleteElement(p1);
                    auto* p2 = MemoryAllocator::newElement<TestMidLevel>();
                    MemoryAllocator::deleteElement(p2);
                    auto* p3 = MemoryAllocator::newElement<TestBigLevel>();
                    MemoryAllocator::deleteElement(p3);
                    auto* p4 = MemoryAllocator::newElement<TestLargeLevel>();
                    MemoryAllocator::deleteElement(p4);
                }
            } });
	}
	for (auto &t : threads)
		t.join();
	auto total_end = std::chrono::steady_clock::now();

	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(total_end - total_start).count();
	std::cout << nworks << " 个线程并发执行 " << rounds << " 轮次\n";
	std::cout << "每轮次 newElement & deleteElement " << ntimes << " 次\n";
	std::cout << "总计花费：" << duration << " ms\n";
}

void BenchmarkNew(size_t ntimes, size_t nworks, size_t rounds)
{
	std::vector<std::thread> threads(nworks);
	auto total_start = std::chrono::steady_clock::now();

	for (size_t k = 0; k < nworks; ++k)
	{
		threads[k] = std::thread([=]()
														 {
            for (size_t j = 0; j < rounds; ++j) {
                for (size_t i = 0; i < ntimes; ++i) {
                    auto* p1 = new TestSmallLevel;
                    delete p1;
                    auto* p2 = new TestMidLevel;
                    delete p2;
                    auto* p3 = new TestBigLevel;
                    delete p3;
                    auto* p4 = new TestLargeLevel;
                    delete p4;
                }
            } });
	}
	for (auto &t : threads)
		t.join();
	auto total_end = std::chrono::steady_clock::now();

	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(total_end - total_start).count();
	std::cout << nworks << " 个线程并发执行 " << rounds << " 轮次\n";
	std::cout << "每轮次 malloc & free " << ntimes << " 次\n";
	std::cout << "总计花费：" << duration << " ms\n";
}

int main()
{
	std::cout << "================ 使用内存池(互斥锁) ===================\n";
	MemoryAllocator::init(MemoryAllocator::Strategy::Lock);
	BenchmarkMemoryPool(TIMES, WORKS, ROUNDS);

	sleep(10);
	std::cout << "\n================ 使用内存池(原子操作) ===================\n";
	MemoryAllocator::init(MemoryAllocator::Strategy::Atomic);
	BenchmarkMemoryPool(TIMES, WORKS, ROUNDS);

	std::cout << "\n================ 使用 new/delete ===================\n";
	BenchmarkNew(TIMES, WORKS, ROUNDS);

	return 0;
}
