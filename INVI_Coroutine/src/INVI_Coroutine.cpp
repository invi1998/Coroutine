#include <iostream>

#include "CO/ReturnObject.h"
#include "Time/Timer.h"


ReturnObject foo()
{
	std::cout << "1 hello from foo \n";
	co_await std::suspend_always{};		// 执行到这里暂停
	// co_await std::suspend_never{};			// 执行到这里不暂停
	std::cout << "2 hello again from foo \n";
	co_await std::suspend_always{};		// 执行到这里暂停
	std::cout << "3 hello again from foo \n";
}

// 协程不能使用变长实参 (...)，而应该使用 auto ... 参数列表
// ReturnObject bar(...)
ReturnObject bar(auto ...args)
{
	std::cout << "sizeof...(args) = " << sizeof...(args) << std::endl;
	co_return std::suspend_never{};
}

// 协程不能是consteval函数, 也不能是constexpr函数，构造函数，析构函数以及main函数
// consteval ReturnObject baz()
// {
//	 co_return std::suspend_never{};
// }

//struct MyType : ReturnObject
//{
//	MyType() : ReturnObject{bar(1, 2, 3, 4, 5)}
//	{
//		co_return std::suspend_never{};
//	}
//};

// 协程不能使用auto占位符返回类型
//auto baz()
//{
//	co_return std::suspend_never{};
//}
auto baz_ok() ->ReturnObject // ok
{
	co_return std::suspend_always{};
}

// 协程不能使用普通return语句返回值
ReturnObject baz()
{
	// return {};		// error: return statement not allowed in coroutine
	co_return std::suspend_always();
}


static int ctr = 0;

struct FOOPromise
{
	// 协程作为成员函数
	ReturnObject member_coro()
	{
		co_await std::suspend_always{};
	}

	// 协程作为静态成员函数
	static ReturnObject static_member_coro()
	{
		std::cout << ctr++ << " static_member_coro start \n";
		co_await std::suspend_always{};
		std::cout << ctr++ << " static_member_coro finish \n";
	}
};

struct Base
{
	virtual ReturnObject virtual_coro() const = 0;
};

struct Derived : Base
{
	ReturnObject virtual_coro() const override
	{
		std::cout << ctr++ << " virtual_coro start \n";
		co_await std::suspend_always{};
		std::cout << ctr++ << " virtual_coro finish \n";
	}
};

// 协程作为lambda函数
auto lambda_coro = [](auto ...args) -> ReturnObject
{
	std::cout << ctr++ << " lambda_coro start \n";
	co_await std::suspend_always{};
	std::cout << ctr++ << " lambda_coro finish; " << "sizeof...(args) = " << sizeof...(args) << std::endl;

	co_return std::suspend_never{};

};

// 协程作为全局函数
static ReturnObject global_coro()
{
	std::cout << ctr++ << " global_coro start \n";
	co_await std::suspend_always{};
	std::cout << ctr++ << " global_coro finish \n";
}



int main() {
	const ReturnObject retObj = foo();		// 调用 foo() 返回一个 ReturnObject 对象句柄，打印 1 hello from foo，然后暂停

	retObj.handle.resume();				// 继续执行 foo()，打印 2 hello again from foo，然后暂停
	retObj.handle();					// 调用的是 std::coroutine_handle<>::operator();	打印 3 hello again from foo，然后结束

	// retObj.handle.resume();				// 继续执行 foo()，打印 3 hello again from foo，然后结束

	std::cout << std::boolalpha << retObj.handle.done() << std::endl;		// false
	// std::boolalpha 用于将 true 和 false 输出为 true 和 false，而不是 1 和 0
	// 按理说，协程执行完毕后，done() 应该返回 true，但是这里返回 false，这是因为 resume() 会使协程继续执行，直到遇到 co_return 或者结束

	const ReturnObject retObj2 = bar(1, 2, 3, 4, 5);		// 调用 bar() 返回一个 ReturnObject 对象句柄，打印 sizeof...(args) = 5

	std::cout << "-------------------\n";
	FOOPromise f;
	auto retobj1 = f.member_coro();
	retobj1.handle.resume();

	auto retobj2 = FOOPromise::static_member_coro();
	retobj2.handle.resume();

	std::shared_ptr<Base> p = std::make_shared<Derived>();
	auto retobj3 = p->virtual_coro();
	retobj3.handle.resume();

	auto retobj4 = lambda_coro(1, 2, 3, 4, 5);
	retobj4.handle.resume();

	auto retobj5 = global_coro();
	retobj5.handle.resume();

	std::cout << "-------------------\n";

	auto gen = invi_generator(0, 10, 2);
	while(!gen.handle.done())
	{
		std::cout << gen.get_val() << std::endl;
		gen.handle();
	}

	std::cout << "-------------------\n";

	std::cout << "caller() = " << caller() << std::endl;

	return 0;
}

