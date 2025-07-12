#include <iostream>
#include <thread>
#include <vector>

#include "MemoryPool.h"

#define TIMES 100
#define WORKS 1
#define ROUNDS 10

#define SMALL_LEVEL 1
#define MID_LEVEL 5
#define BIG_LEVEL 10
#define LARGE_LEVEL 20

using namespace RainMemoPool;

// 测试用例类，不同大小的对象
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

// 单轮次申请释放次数 线程数 轮次
void BenchmarkMemoryPool(size_t ntimes, size_t nworks, size_t rounds)
{
	std::vector<std::thread> vthread(nworks); // 线程池
	size_t total_costtime = 0;
	for (size_t k = 0; k < nworks; ++k) // 创建 nworks 个线程
	{
		vthread[k] = std::thread([&]()
														 {
			for (size_t j = 0; j < rounds; ++j)
			{
				size_t begin1 = clock();
				for (size_t i = 0; i < ntimes; i++)
				{
                    TestSmallLevel* p1 = newElement<TestSmallLevel>(); // 内存池对外接口
                    deleteElement<TestSmallLevel>(p1);
                    TestMidLevel* p2 = newElement<TestMidLevel>();
                    deleteElement<TestMidLevel>(p2);
                    TestBigLevel* p3 = newElement<TestBigLevel>();
                    deleteElement<TestBigLevel>(p3);
                    TestLargeLevel* p4 = newElement<TestLargeLevel>();
                    deleteElement<TestLargeLevel>(p4);
				}
				size_t end1 = clock();

				total_costtime += end1 - begin1;
			} });
	}
	for (auto &t : vthread)
	{
		t.join();
	}
	std::cout << nworks << " 个线程并发执行 " << rounds << " 轮次" << std::endl;
	std::cout << "每轮次 newElement & deleteElement " << ntimes << " 次" << std::endl;
	std::cout << "总计花费：" << total_costtime << " ms" << std::endl;
}

void BenchmarkNew(size_t ntimes, size_t nworks, size_t rounds)
{
	std::vector<std::thread> vthread(nworks);
	size_t total_costtime = 0;
	for (size_t k = 0; k < nworks; ++k)
	{
		vthread[k] = std::thread([&]()
														 {
			for (size_t j = 0; j < rounds; ++j)
			{
				size_t begin1 = clock();
				for (size_t i = 0; i < ntimes; i++)
				{
                    TestSmallLevel* p1 = new TestSmallLevel;
                    delete p1;
                    TestMidLevel* p2 = new TestMidLevel;
                    delete p2;
                    TestBigLevel* p3 = new TestBigLevel;
                    delete p3;
                    TestLargeLevel* p4 = new TestLargeLevel;
                    delete p4;
				}
				size_t end1 = clock();
				
				total_costtime += end1 - begin1;
			} });
	}
	for (auto &t : vthread)
	{
		t.join();
	}
	std::cout << nworks << " 个线程并发执行 " << rounds << " 轮次" << std::endl;
	std::cout << "每轮次 malloc & free " << ntimes << " 次" << std::endl;
	std::cout << "总计花费：" << total_costtime << " ms" << std::endl;
}

int main()
{
	HashBucket::initMemoryPool(); // 使用内存池接口前一定要先调用该函数
	std::cout << "===========================================================================" << std::endl;
	BenchmarkMemoryPool(TIMES, WORKS, ROUNDS); // 测试内存池
	std::cout << "===========================================================================" << std::endl;
	BenchmarkNew(TIMES, WORKS, ROUNDS); // 测试 new delete

	return 0;
}