#include "ReturnObject.h"

ReturnObject invi_generator(int start, int end, int step)
{
	for (int i = start; i < end; i += step)
	{
		co_yield i;
	}
	co_return std::suspend_never{};
}

ReturnObject DataAccessCoro()
{
	std::cout << "DataAccessCoro start\n";
	co_await std::suspend_always{};		// 挂起当前协程
	std::cout << "DataAccessCoro continue\n";
	co_await std::suspend_always{};		// 挂起当前协程
	std::cout << "DataAccessCoro finish\n";
	co_await std::suspend_always{};		// 挂起当前协程
}

double caller()
{
	std::coroutine_handle<ReturnObject::promise_type> hd = DataAccessCoro().handle;

	ReturnObject::promise_type prom = hd.promise();	// 获取协程的 promise 对象，必须保证返回对象的协程句柄中具有正确的模板特化

	double sum = 0;
	while (!hd.done())
	{
		sum += prom.get_data();
		prom.set_data(sum);
		hd();		// 等价于 hd.resume()
	}
	const double ret = prom.get_data();
	return ret;
}
