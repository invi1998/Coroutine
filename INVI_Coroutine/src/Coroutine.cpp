#include <iostream>
#include <coroutine>

#include "Time/Timer.h"

struct ReturnObject
{
	struct promise_type
	{
		// promise_type() = default;

		std::suspend_never initial_suspend() { return {}; }
		std::suspend_never final_suspend() noexcept { return {}; }

		// 调用 promise.get_return_object() 并将结果保存在一个局部变量中。当协程第一次挂起时，该调用的结果将返回给调用者。
		// 在此步骤之前抛出的任何异常都会传播回调用者，而不是放入 promise 中。
		ReturnObject get_return_object()
		{
			return ReturnObject{ std::coroutine_handle<promise_type>::from_promise(*this) };
		}

		void unhandled_exception() {}

		// void return_void() {}

		void return_value(std::suspend_never) {}
	};

	std::coroutine_handle<> handle;
	ReturnObject(std::coroutine_handle<> h) : handle{h} {}		// 用于构造 ReturnObject 对象

};


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

// 协程不能使用普通return语句返回值
ReturnObject baz()
{
	// return {};		// error: return statement not allowed in coroutine
	co_return std::suspend_never{};
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

	return 0;
}

