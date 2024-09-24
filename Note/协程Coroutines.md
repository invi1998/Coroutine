# 协程 (C++20)

协程是一种可以暂停执行以便稍后恢复的函数。协程是无栈的：它们通过返回给调用者来暂停执行，而恢复执行所需的数据与栈分开存储。这允许顺序代码异步执行（例如，处理非阻塞 I/O 而无需显式回调），并且还支持在惰性计算的无限序列和其他用法上的算法。

一个函数是协程，如果它的定义包含以下任意内容：

- co_await 表达式 — 暂停执行直到恢复

```c++
task<> tcp_echo_server()
{
    char data[1024];
    while (true)
    {
        std::size_t n = co_await socket.async_read_some(buffer(data));
        co_await async_write(socket, buffer(data, n));
    }
}
```

-  co_yield 表达式 — 暂停执行并返回一个值

```c++
generator<unsigned int> iota(unsigned int n = 0)
{
    while (true)
        co_yield n++;
}
```

- co_return 语句 — 完成执行并返回一个值

```c++
lazy<int> f()
{
    co_return 7;
}
```



**每个协程必须具有满足以下多个要求的返回类型**。

### Restrictions 限制

协程不能使用 [可变参数](https://en.cppreference.com/w/cpp/language/variadic_arguments)、普通 [返回](https://en.cppreference.com/w/cpp/language/return) 语句或 [占位符返回类型](https://en.cppreference.com/w/cpp/language/function) （[`auto`](https://en.cppreference.com/w/cpp/language/auto) 或 [概念](https://en.cppreference.com/w/cpp/language/constraints#Concepts)）。

[常量求值函数](https://en.cppreference.com/w/cpp/language/consteval)，[常量表达式函数](https://en.cppreference.com/w/cpp/language/constexpr)，[构造函数](https://en.cppreference.com/w/cpp/language/constructor)，[析构函数](https://en.cppreference.com/w/cpp/language/destructor)，以及[主函数](https://en.cppreference.com/w/cpp/language/main_function)不能是协程。

### Execution 执行

每个协程都与

- *promise 对象*，在协程内部进行操作。协程通过这个对象提交其结果或异常。Promise 对象与[std::promise](https://en.cppreference.com/w/cpp/thread/promise)没有任何关系。
- 外部操作的*协程句柄*。这是一个非拥有的句柄，用于恢复协程的执行或销毁协程帧。
- *协程状态*，这是内部的动态分配存储（除非分配被优化掉），包含的对象

当协程开始执行时，它会执行以下操作：

- [分配](https://en.cppreference.com/w/cpp/language/coroutines#Dynamic_allocation) 协程状态对象使用 [operator new](https://en.cppreference.com/w/cpp/memory/new/operator_new)。
- 将所有函数参数复制到协程状态：按值参数被移动或复制，按引用参数保持引用（因此，如果在引用对象的生命周期结束后恢复协程，可能会变成悬空引用——请参见下面的示例）。
- 调用承诺对象的构造函数。如果承诺类型有一个接受所有协程参数的构造函数，则调用该构造函数，并使用后复制的协程参数。否则，调用默认构造函数。
- 调用 promise.get_return_object() 并将结果保存在一个局部变量中。当协程第一次挂起时，该调用的结果将返回给调用者。在此步骤之前抛出的任何异常都会传播回调用者，而不是放入 promise 中。
- 调用 promise.initial_suspend() 并 `co_await` 其结果。典型的 `Promise` 类型要么返回 [std::suspend_always](https://en.cppreference.com/w/cpp/coroutine/suspend_always)，用于懒启动的协程，要么返回 [std::suspend_never](https://en.cppreference.com/w/cpp/coroutine/suspend_never)，用于急启动的协程。
- 当 co_await promise.initial_suspend() 恢复时，开始执行协程的主体。



一些参数变为悬空的例子：

```c++
#include <coroutine>
#include <iostream>
 
struct promise;
 
struct coroutine : std::coroutine_handle<promise>
{
    using promise_type = ::promise;
};
 
struct promise
{
    coroutine get_return_object() { return {coroutine::from_promise(*this)}; }
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
};
 
struct S
{
    int i;
    coroutine f()
    {
        std::cout << i;
        co_return;
    }
};
 
void bad1()
{
    coroutine h = S{0}.f();
    // S{0} destroyed
    h.resume(); // resumed coroutine executes std::cout << i, uses S::i after free
    h.destroy();
}
 
coroutine bad2()
{
    S s{0};
    return s.f(); // returned coroutine can't be resumed without committing use after free
}
 
void bad3()
{
    coroutine h = [i = 0]() -> coroutine // a lambda that's also a coroutine
    {
        std::cout << i;
        co_return;
    }(); // immediately invoked
    // lambda destroyed
    h.resume(); // uses (anonymous lambda type)::i after free
    h.destroy();
}
 
void good()
{
    coroutine h = [](int i) -> coroutine // make i a coroutine parameter
    {
        std::cout << i;
        co_return;
    }(0);
    // lambda destroyed
    h.resume(); // no problem, i has been copied to the coroutine
                // frame as a by-value parameter
    h.destroy();
}
```

当协程达到挂起点时

- 先前获得的返回对象在必要时会隐式转换为协程的返回类型，然后返回给调用者/恢复者。

当协程到达 co_return 语句时，它执行以下操作：

- 调用 promise.return_void() 以

  - co_return;

  - co_return expr; 其中 expr 的类型是 void

- 或调用 promise.return_value(expr) 用于 co_return expr;，其中 expr 具有非空类型。
- 以创建的相反顺序销毁所有具有自动存储持续时间的变量。
- 调用 promise.final_suspend() 并 co_await 结果。

从协程的末尾掉落相当于 co_return;，但如果在 `Promise` 的作用域中找不到 `return_void` 的声明，则行为是未定义的。一个函数的函数体中没有任何定义关键字，无论其返回类型如何，都不是协程，如果返回类型不是（可能是 cv 限定的）void，则从末尾掉落会导致未定义行为。

```c++
// assuming that task is some coroutine task type
task<void> f()
{
    // not a coroutine, undefined behavior
}
 
task<void> g()
{
    co_return;  // OK
}
 
task<void> h()
{
    co_await g();
    // OK, implicit co_return;
}
```





