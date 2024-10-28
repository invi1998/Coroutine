#pragma once

#include <iostream>
#include <coroutine>
#include <optional>

struct ReturnObject
{
	struct promise_type
	{
		// promise_type() = default;

		std::optional<int> val_;		// 用于保存协程返回的值

		std::suspend_never initial_suspend() { return {}; }			// 协程开始时不挂起
		// std::suspend_never final_suspend() noexcept { return {}; }	// 协程结束时不挂起

		std::suspend_always final_suspend() noexcept { return {}; }	// 协程结束时挂起

		// 调用 promise.get_return_object() 并将结果保存在一个局部变量中。当协程第一次挂起时，该调用的结果将返回给调用者。
		// 在此步骤之前抛出的任何异常都会传播回调用者，而不是放入 promise 中。
		ReturnObject get_return_object()
		{
			return ReturnObject{ std::coroutine_handle<promise_type>::from_promise(*this) };
		}

		void unhandled_exception() {}

		// void return_void() {}
		// 协程返回只能在void和value之间二选一，不能同时存在
		void return_value(std::suspend_always)
		{
			std::cout << "return_value(std::suspend_always)\n";
		}

		void return_value(std::suspend_never)
		{
			std::cout << "return_value(std::suspend_never)\n";
		}

		std::suspend_always yield_value(int val)
		{
			val_.emplace(val);
			return std::suspend_always{};
		}

		[[nodiscard]] double get_data() const noexcept { return data_; }
		void set_data(double data) noexcept { data_ = data; }

	private:
		double data_{ 3.14 };
	};

	std::coroutine_handle<promise_type> handle;
	ReturnObject(std::coroutine_handle<promise_type> h) : handle{ h } {}		// 用于构造 ReturnObject 对象
	operator std::coroutine_handle<promise_type>() const { return handle; }		// 用于将 ReturnObject 对象转换为 std::coroutine_handle<promise_type> 对象

	int get_val() { return handle.promise().val_.value(); }



	
};

// 生成[start, end)区间的整数序列
ReturnObject invi_generator(int start, int end, int step = 1);

// 数据访问协程
ReturnObject DataAccessCoro();

// 调用数据访问协程
double caller();

