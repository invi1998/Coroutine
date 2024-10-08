```c++
#include <iostream>
#include <coroutine>

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

		void return_void() {}
	};

	std::coroutine_handle<> handle;
	ReturnObject(std::coroutine_handle<> h) : handle(h) {}		// 用于构造 ReturnObject 对象

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

int main() {

	ReturnObject retObj = foo();		// 调用 foo() 返回一个 ReturnObject 对象句柄，打印 1 hello from foo，然后暂停

	retObj.handle.resume();				// 继续执行 foo()，打印 2 hello again from foo，然后暂停
	// retObj.handle();   // 调用的是 std::coroutine_handle<>::operator();

	retObj.handle.resume();				// 继续执行 foo()，打印 3 hello again from foo，然后结束

	return 0;
}

```

如上，我们构建了一个简单的协程

这里，如果我们把main中的协程句柄直接执行，因为协程句柄内部重载了()运算符，其重载内容就是调用其resume()函数，所以，

`retObj.handle();`  等价于 `retObj.handle.resume();`

```c++

int main() {

	ReturnObject retObj = foo();		// 调用 foo() 返回一个 ReturnObject 对象句柄，打印 1 hello from foo，然后暂停

	retObj.handle.resume();				// 继续执行 foo()，打印 2 hello again from foo，然后暂停
	retObj.handle();					// 调用的是 std::coroutine_handle<>::operator();	打印 3 hello again from foo，然后结束

	// retObj.handle.resume();				// 继续执行 foo()，打印 3 hello again from foo，然后结束

	return 0;
}

```

`std::coroutine_handle<>::operator()`重载源码

```c++

    void operator()() const {
        __builtin_coro_resume(_Ptr);
    }

    void resume() const {
        __builtin_coro_resume(_Ptr);
    }

```

