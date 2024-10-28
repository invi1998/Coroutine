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



# 为什么不能同时包含 `return_void` 和 `return_value`？

在 C++20 中，协程的设计要求协程的 `promise_type` 必须明确指定其行为，包括如何处理返回值和如何表示没有返回值的情况。这是因为协程的 `promise_type` 需要生成相应的控制块和状态机，以确保协程的正确执行和资源管理。

### 为什么不能同时包含 `return_void` 和 `return_value`？

1. **类型一致性**:
   - 协程的返回类型必须是确定的。如果一个协程既可以通过 `return_void` 返回空值，又可以通过 `return_value` 返回具体的值，那么编译器将无法确定协程的最终返回类型。
   - 例如，如果协程的返回类型是 `void`，那么 `return_value` 就没有意义；反之，如果协程的返回类型是一个具体的类型（如 `int`），那么 `return_void` 也没有意义。

2. **语义清晰**:
   - 协程的设计要求语义清晰。如果一个协程既可以返回值也可以不返回值，那么使用者可能会感到困惑，不知道在什么情况下应该使用哪种返回方式。
   - 清晰的语义有助于编写更可靠和可维护的代码。

3. **编译器实现**:
   - 编译器在生成协程的控制块和状态机时，需要知道协程的具体行为。如果一个协程既包含 `return_void` 又包含 `return_value`，编译器将难以生成一致的控制块和状态机。
   - 编译器需要在编译时确定协程的返回类型和返回行为，以生成正确的代码。

### 如何选择 `return_void` 或 `return_value`？

- **`return_void`**:
  - 如果协程不需要返回任何值，可以使用 `return_void`。
  - 例如，一个简单的异步任务可能只需要执行某些操作而不返回结果。

  ```cpp
  struct MyPromise {
      std::suspend_never initial_suspend() { return {}; }
      std::suspend_never final_suspend() noexcept { return {}; }
      void return_void() {}  // 没有返回值
      void unhandled_exception() {}
  };
  ```

- **`return_value`**:
  - 如果协程需要返回一个具体的值，可以使用 `return_value`。
  - 例如，一个异步计算任务可能需要返回计算结果。

  ```cpp
  struct MyPromise {
      std::suspend_never initial_suspend() { return {}; }
      std::suspend_never final_suspend() noexcept { return {}; }
      int get_return_object() { return 0; }  // 返回值类型为 int
      void return_value(int value) { result = value; }  // 返回具体的值
      void unhandled_exception() {}
      int result;
  };
  ```

### 示例代码

下面是一个简单的示例，展示了如何定义一个返回值的协程和一个不返回值的协程：

#### 不返回值的协程

```cpp
#include <coroutine>
#include <iostream>

struct NoReturnValuePromise {
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
};

using NoReturnValueTask = std::coroutine_handle<NoReturnValuePromise>;

NoReturnValueTask no_return_value_coroutine() {
    std::cout << "Executing no_return_value_coroutine" << std::endl;
    co_return;
}

int main() {
    auto task = no_return_value_coroutine();
    task.resume();
    return 0;
}
```

#### 返回值的协程

```cpp
#include <coroutine>
#include <iostream>

struct ReturnValuePromise {
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    int get_return_object() { return 0; }
    void return_value(int value) { result = value; }
    void unhandled_exception() {}
    int result;
};

using ReturnValueTask = std::coroutine_handle<ReturnValuePromise>;

ReturnValueTask return_value_coroutine() {
    std::cout << "Executing return_value_coroutine" << std::endl;
    co_return 42;
}

int main() {
    auto task = return_value_coroutine();
    task.resume();
    std::cout << "Result: " << task.promise().result << std::endl;
    return 0;
}
```

### 总结

协程不能同时包含 `return_void` 和 `return_value`，因为这会导致类型不一致和语义模糊。选择合适的返回方式（`return_void` 或 `return_value`）可以使协程的实现更加清晰和可靠。



# 协程的限制

在 C++20 中，协程的使用有一些特定的限制，这些限制旨在确保协程的正确性和可预测性。以下是协程的一些主要限制：

### 1. 不能使用变长实参 (`...`)

协程不能使用传统的 C 风格的变长参数列表 (`...`)。原因如下：
- **类型不安全**：`...` 不提供类型安全性和类型信息，编译器无法在编译时确定参数的具体类型和数量。
- **协程框架生成**：协程需要在编译时生成控制块和状态机，`...` 无法提供足够的信息来生成这些结构。

### 2. 不能使用普通的 `return` 语句

协程不能使用普通的 `return` 语句来返回值。原因如下：
- **返回机制不同**：协程使用 `co_return` 关键字来返回值或结束协程。普通 `return` 语句会导致编译错误，因为协程的返回机制与普通函数不同。
- **状态管理**：协程需要管理其状态和上下文，普通 `return` 无法提供这种管理。

### 3. 不能使用占位符返回类型（`auto` 或 概念）

协程的返回类型不能使用 `auto` 或概念（concepts）。原因如下：
- **类型推导**：协程的返回类型需要在编译时明确指定，以便生成正确的控制块和状态机。`auto` 和概念无法提供足够的信息来进行类型推导。
- **编译器需求**：编译器需要在编译时确定协程的返回类型，以生成正确的代码。

### 4. 不能是 `consteval` 函数、`constexpr` 函数、构造函数、析构函数及 `main` 函数

- **`consteval` 函数**：`consteval` 函数必须在常量表达式上下文中执行，而协程的异步性质使其无法在常量表达式上下文中执行。
- **`constexpr` 函数**：`constexpr` 函数必须在编译时可求值，而协程的异步性质使其无法在编译时求值。
- **构造函数和析构函数**：构造函数和析构函数有特定的语义和生命周期管理要求，协程的异步性质与其不兼容。
- **`main` 函数**：`main` 函数是程序的入口点，必须同步执行，而协程的异步性质使其不适合用作 `main` 函数。

### 示例代码

以下是一些示例，展示了这些限制：

#### 不能使用变长实参

```cpp
#include <coroutine>
#include <iostream>

struct MyPromise {
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
};

using ReturnObject = std::coroutine_handle<MyPromise>;

// 错误：不能使用变长实参
// ReturnObject bar(...) {
//     std::cout << "bar called" << std::endl;
//     co_return;
// }

// 正确：使用 auto...
ReturnObject bar(auto... args) {
    std::cout << "bar called with " << sizeof...(args) << " arguments" << std::endl;
    co_return;
}
```

#### 不能使用普通的 `return` 语句

```cpp
#include <coroutine>
#include <iostream>

struct MyPromise {
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
};

using ReturnObject = std::coroutine_handle<MyPromise>;

// 错误：不能使用普通的 return 语句
// ReturnObject foo() {
//     std::cout << "foo called" << std::endl;
//     return;  // 错误
// }

// 正确：使用 co_return
ReturnObject foo() {
    std::cout << "foo called" << std::endl;
    co_return;
}
```

#### 不能使用占位符返回类型

```cpp
#include <coroutine>
#include <iostream>

struct MyPromise {
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
};

// 错误：不能使用 auto
// auto baz() -> std::coroutine_handle<MyPromise> {
//     std::cout << "baz called" << std::endl;
//     co_return;
// }

// 正确：明确指定返回类型
ReturnObject baz() {
    std::cout << "baz called" << std::endl;
    co_return;
}
```

#### 不能是 `consteval` 函数、`constexpr` 函数、构造函数、析构函数及 `main` 函数

```cpp
#include <coroutine>
#include <iostream>

struct MyPromise {
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
};

using ReturnObject = std::coroutine_handle<MyPromise>;

// 错误：不能是 consteval 函数
// consteval ReturnObject ce_coro() {
//     std::cout << "ce_coro called" << std::endl;
//     co_return;
// }

// 错误：不能是 constexpr 函数
// constexpr ReturnObject ce_coro() {
//     std::cout << "ce_coro called" << std::endl;
//     co_return;
// }

// 错误：不能是构造函数
// struct MyClass {
//     MyClass() {
//         co_return;
//     }
// };

// 错误：不能是析构函数
// struct MyClass {
//     ~MyClass() {
//         co_return;
//     }
// };

// 错误：不能是 main 函数
// int main() {
//     co_return;
// }

// 正确：普通的协程函数
ReturnObject coro() {
    std::cout << "coro called" << std::endl;
    co_return;
}

int main() {
    auto task = coro();
    task.resume();
    return 0;
}
```

### 总结

协程的设计和实现有其特定的要求和限制，这些限制确保了协程的正确性和可预测性。理解这些限制并遵循它们是编写有效和可靠的协程代码的关键。



# co_yield

`co_yield` 是 C++20 协程中的一个重要关键字，用于在协程中生成值或传递控制权。与 `co_await` 和 `co_return` 一样，`co_yield` 也是协程的三个主要控制关键字之一。下面详细解释 `co_yield` 的用途和用法。

### 用途

1. **生成值**:
   - 在生成器（generator）模式中，`co_yield` 用于生成一系列值。每次 `co_yield` 被执行时，协程会暂停执行，并将生成的值返回给调用者。调用者可以继续请求下一个值，直到生成器耗尽所有值。

2. **传递控制权**:
   - 在某些场景下，`co_yield` 也可以用于在协程之间传递控制权，而不仅仅是生成值。

### 基本用法

#### 生成器示例

下面是一个简单的生成器示例，展示了如何使用 `co_yield` 生成一系列整数：

```cpp
#include <coroutine>
#include <iostream>
#include <optional>

// 定义协程的 promise 类型
struct IntGeneratorPromise {
    std::suspend_always initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    std::optional<int> get_return_object() { return {}; }
    void return_value(int value) { current_value = value; }
    void return_void() {}
    void unhandled_exception() {}

    std::optional<int> current_value;
};

// 定义生成器的返回类型
using IntGenerator = std::coroutine_handle<IntGeneratorPromise>;

// 生成器函数
IntGenerator generate_integers(int start, int end) {
    for (int i = start; i <= end; ++i) {
        co_yield i;
    }
}

// 主函数
int main() {
    auto generator = generate_integers(1, 5);

    while (true) {
        if (!generator.done()) {
            generator.resume();
            if (generator.promise().current_value) {
                std::cout << *generator.promise().current_value << std::endl;
            }
        } else {
            break;
        }
    }

    return 0;
}
```

### 解释

1. **Promise 类型**:
   - `IntGeneratorPromise` 是生成器的 promise 类型，定义了协程的行为。
   - `get_return_object` 返回一个 `std::optional<int>`，表示生成器的返回类型。
   - `return_value` 用于设置当前生成的值。
   - `initial_suspend` 和 `final_suspend` 控制协程的初始和最终挂起点。

2. **生成器函数**:
   - `generate_integers` 是生成器函数，使用 `co_yield` 生成一系列整数。
   - 每次 `co_yield` 被执行时，协程会暂停，并将生成的值返回给调用者。

3. **主函数**:
   - 在 `main` 函数中，创建生成器对象并调用 `resume` 方法来恢复协程的执行。
   - 每次调用 `resume` 后，检查 `current_value` 是否有值，并输出该值。
   - 当生成器完成时，`done` 方法返回 `true`，循环终止。

### 注意事项

1. **控制权传递**:
   - `co_yield` 会使协程暂停执行，并将控制权返回给调用者。调用者可以使用 `resume` 方法恢复协程的执行。

2. **返回类型**:
   - 生成器的返回类型通常是一个包装类型（如 `std::optional`），用于表示生成的值或结束状态。

3. **异常处理**:
   - 在 `unhandled_exception` 方法中处理未捕获的异常，确保协程在发生错误时能够正确清理资源。

### 总结

`co_yield` 是 C++20 协程中用于生成值和传递控制权的重要关键字。通过生成器模式，`co_yield` 可以轻松地生成一系列值，并在需要时恢复协程的执行。



# co_await

`co_await` 是 C++20 协程中的一个关键操作符，用于在协程中等待异步操作的结果。它允许协程在等待某个异步操作完成时暂停执行，并在操作完成后恢复执行。`co_await` 是实现异步编程模型的核心机制之一，使得异步代码可以像同步代码一样编写，提高了代码的可读性和可维护性。

### 用途

1. **等待异步操作**:
   - `co_await` 用于等待一个异步操作的结果。当协程遇到 `co_await` 表达式时，它会暂停执行，并将控制权返回给调用者。一旦异步操作完成，协程会自动恢复执行。

2. **资源管理**:
   - `co_await` 通常与 `std::coroutine_handle` 和协程的 `promise_type` 一起使用，以确保在协程暂停和恢复时正确管理资源。

### 基本用法

#### 简单示例

下面是一个简单的示例，展示了如何使用 `co_await` 等待一个异步操作的结果：

```cpp
#include <coroutine>
#include <iostream>
#include <future>
#include <thread>
#include <chrono>

// 定义协程的 promise 类型
struct AsyncPromise {
    std::suspend_always initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_value(int value) { result = value; }
    void return_void() {}
    void unhandled_exception() {}

    int result;
};

// 定义协程的返回类型
using AsyncTask = std::coroutine_handle<AsyncPromise>;

// 异步任务函数
AsyncTask async_task() {
    std::cout << "Starting async task" << std::endl;

    // 模拟一个异步操作
    std::future<int> future = std::async(std::launch::async, [] {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        return 42;
    });

    // 等待异步操作完成
    int result = co_await future.get();

    std::cout << "Async task completed with result: " << result << std::endl;
    co_return result;
}

// 主函数
int main() {
    auto task = async_task();
    task.resume();

    // 等待协程完成
    while (!task.done()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        task.resume();
    }

    std::cout << "Final result: " << task.promise().result << std::endl;
    return 0;
}
```

### 解释

1. **Promise 类型**:
   - `AsyncPromise` 是协程的 promise 类型，定义了协程的行为。
   - `initial_suspend` 和 `final_suspend` 控制协程的初始和最终挂起点。
   - `return_value` 用于设置协程的返回值。
   - `unhandled_exception` 用于处理未捕获的异常。

2. **异步任务函数**:
   - `async_task` 是一个协程函数，模拟了一个异步操作。
   - 使用 `std::async` 创建一个异步任务，并返回一个 `std::future<int>`。
   - 使用 `co_await` 等待 `std::future` 的结果。当 `future.get()` 被调用时，协程会暂停执行，直到异步操作完成。
   - 一旦异步操作完成，协程会恢复执行，并输出结果。

3. **主函数**:
   - 在 `main` 函数中，创建协程对象并调用 `resume` 方法来恢复协程的执行。
   - 使用一个循环来定期检查协程是否完成，并调用 `resume` 方法恢复协程的执行。
   - 一旦协程完成，输出最终结果。

### 注意事项

1. **暂停和恢复**:
   - `co_await` 会使协程暂停执行，并将控制权返回给调用者。一旦异步操作完成，协程会自动恢复执行。
   - 为了确保协程能够正确恢复执行，调用者需要定期调用 `resume` 方法。

2. **资源管理**:
   - 协程的 `promise_type` 和 `std::coroutine_handle` 提供了机制来管理协程的资源，确保在协程暂停和恢复时正确处理资源。

3. **异常处理**:
   - 在 `unhandled_exception` 方法中处理未捕获的异常，确保协程在发生错误时能够正确清理资源。

### 总结

`co_await` 是 C++20 协程中用于等待异步操作结果的关键操作符。通过 `co_await`，协程可以暂停执行并等待异步操作完成，然后恢复执行。这使得异步代码可以像同步代码一样编写，提高了代码的可读性和可维护性。



# co_return

`co_return` 是 C++20 协程中的一个关键操作符，用于从协程中返回值或结束协程。与普通函数的 `return` 语句不同，`co_return` 专门用于协程，它允许协程在返回值的同时保持其状态，以便后续可以继续执行。下面详细介绍 `co_return` 的用途和用法。

### 用途

1. **返回值**:
   - `co_return` 用于从协程中返回一个值。当协程遇到 `co_return` 时，它会将指定的值返回给调用者，并结束当前的协程执行。

2. **结束协程**:
   - `co_return` 也可以用于不返回任何值的情况下结束协程。在这种情况下，协程会简单地结束执行，并将控制权返回给调用者。

### 基本用法

#### 返回值的示例

下面是一个简单的示例，展示了如何使用 `co_return` 从协程中返回一个值：

```cpp
#include <coroutine>
#include <iostream>
#include <optional>

// 定义协程的 promise 类型
struct ValuePromise {
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_value(int value) { result = value; }
    void return_void() {}
    void unhandled_exception() {}

    int result;
};

// 定义协程的返回类型
using ValueTask = std::coroutine_handle<ValuePromise>;

// 协程函数
ValueTask compute_value() {
    std::cout << "Computing value..." << std::endl;
    co_return 42;
}

// 主函数
int main() {
    auto task = compute_value();
    task.resume();

    if (!task.done()) {
        std::cout << "Task is not done yet." << std::endl;
    } else {
        std::cout << "Task completed with result: " << task.promise().result << std::endl;
    }

    return 0;
}
```

### 解释

1. **Promise 类型**:
   - `ValuePromise` 是协程的 promise 类型，定义了协程的行为。
   - `initial_suspend` 和 `final_suspend` 控制协程的初始和最终挂起点。
   - `return_value` 用于设置协程的返回值。
   - `unhandled_exception` 用于处理未捕获的异常。

2. **协程函数**:
   - `compute_value` 是一个协程函数，计算一个值并使用 `co_return` 返回该值。
   - 当 `co_return 42` 被执行时，协程会将值 42 返回给调用者，并结束执行。

3. **主函数**:
   - 在 `main` 函数中，创建协程对象并调用 `resume` 方法来恢复协程的执行。
   - 检查协程是否完成，并输出结果。

#### 不返回值的示例

下面是一个示例，展示了如何使用 `co_return` 结束协程而不返回任何值：

```cpp
#include <coroutine>
#include <iostream>

// 定义协程的 promise 类型
struct VoidPromise {
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
};

// 定义协程的返回类型
using VoidTask = std::coroutine_handle<VoidPromise>;

// 协程函数
VoidTask perform_task() {
    std::cout << "Performing task..." << std::endl;
    co_return;
}

// 主函数
int main() {
    auto task = perform_task();
    task.resume();

    if (!task.done()) {
        std::cout << "Task is not done yet." << std::endl;
    } else {
        std::cout << "Task completed." << std::endl;
    }

    return 0;
}
```

### 解释

1. **Promise 类型**:
   - `VoidPromise` 是协程的 promise 类型，定义了协程的行为。
   - `initial_suspend` 和 `final_suspend` 控制协程的初始和最终挂起点。
   - `return_void` 用于结束协程而不返回任何值。
   - `unhandled_exception` 用于处理未捕获的异常。

2. **协程函数**:
   - `perform_task` 是一个协程函数，执行一个任务并使用 `co_return` 结束协程。
   - 当 `co_return` 被执行时，协程会结束执行，并将控制权返回给调用者。

3. **主函数**:
   - 在 `main` 函数中，创建协程对象并调用 `resume` 方法来恢复协程的执行。
   - 检查协程
   
   
   
   
   
   # std::suspend_always final_suspend()
   
   
   
   在 C++20 协程中，`final_suspend` 是一个重要的挂起点，用于控制协程在结束时的行为。`std::suspend_never` 和 `std::suspend_always` 是两种不同的挂起策略，它们决定了协程在 `final_suspend` 时的行为。
   
   ### 区别
   
   1. **`std::suspend_never`**:
      - **行为**：当协程到达 `final_suspend` 时，不会挂起协程，而是立即继续执行。
      - **效果**：协程会在 `final_suspend` 时立即完成，不会返回到调用者。
   
   2. **`std::suspend_always`**:
      - **行为**：当协程到达 `final_suspend` 时，会挂起协程，返回控制权给调用者。
      - **效果**：协程会在 `final_suspend` 时暂停执行，返回到调用者，稍后再由调用者恢复。
   
   ### 实际使用场景
   
   #### `std::suspend_never final_suspend()`
   
   1. **立即完成**：
      - 当你希望协程在结束时立即完成，而不是返回到调用者时，可以使用 `std::suspend_never`。
      - 适用于那些不需要在结束时进行额外异步操作的协程。
   
   2. **资源释放**：
      - 如果协程在结束时需要立即释放资源，而不是等待调用者恢复，可以使用 `std::suspend_never`。
   
   #### `std::suspend_always final_suspend()`
   
   1. **异步清理**：
      - 当协程在结束时需要进行一些异步清理工作（如关闭文件、释放网络连接等），可以使用 `std::suspend_always`。
      - 适用于那些需要在结束时进行异步操作的协程。
   
   2. **多阶段完成**：
      - 如果协程的完成过程需要多个阶段，每个阶段都需要返回到调用者，可以使用 `std::suspend_always`。
   
   ### 示例代码
   
   #### 使用 `std::suspend_never final_suspend()`
   
   ```cpp
   #include <coroutine>
   #include <iostream>
   
   // 定义协程的 promise 类型
   struct NeverSuspendPromise {
       std::suspend_never initial_suspend() { return {}; }
       std::suspend_never final_suspend() noexcept { return {}; }
       void return_value(int value) { result = value; }
       void return_void() {}
       void unhandled_exception() {}
   
       int result;
   };
   
   // 定义协程的返回类型
   using NeverSuspendTask = std::coroutine_handle<NeverSuspendPromise>;
   
   // 协程函数
   NeverSuspendTask never_suspend_coroutine() {
       std::cout << "Starting coroutine..." << std::endl;
       co_return 42;
   }
   
   // 主函数
   int main() {
       auto task = never_suspend_coroutine();
       task.resume();
   
       if (!task.done()) {
           std::cout << "Task is not done yet." << std::endl;
       } else {
           std::cout << "Task completed with result: " << task.promise().result << std::endl;
       }
   
       return 0;
   }
   ```
   
   #### 使用 `std::suspend_always final_suspend()`
   
   ```cpp
   #include <coroutine>
   #include <iostream>
   #include <thread>
   #include <chrono>
   
   // 定义协程的 promise 类型
   struct AlwaysSuspendPromise {
       std::suspend_never initial_suspend() { return {}; }
       std::suspend_always final_suspend() noexcept { return {}; }
       void return_value(int value) { result = value; }
       void return_void() {}
       void unhandled_exception() {}
   
       int result;
   };
   
   // 定义协程的返回类型
   using AlwaysSuspendTask = std::coroutine_handle<AlwaysSuspendPromise>;
   
   // 协程函数
   AlwaysSuspendTask always_suspend_coroutine() {
       std::cout << "Starting coroutine..." << std::endl;
       co_return 42;
   }
   
   // 主函数
   int main() {
       auto task = always_suspend_coroutine();
       task.resume();
   
       if (!task.done()) {
           std::cout << "Task is not done yet." << std::endl;
           std::this_thread::sleep_for(std::chrono::seconds(1));
           task.resume();
       }
   
       if (!task.done()) {
           std::cout << "Task is still not done." << std::endl;
       } else {
           std::cout << "Task completed with result: " << task.promise().result << std::endl;
       }
   
       return 0;
   }
   ```
   
   ### 解释
   
   1. **`std::suspend_never final_suspend()`**:
      - 在 `never_suspend_coroutine` 中，`final_suspend` 返回 `std::suspend_never`，因此协程在结束时不会挂起，而是立即完成。
      - 调用 `task.resume()` 一次后，协程就完成了，可以直接获取结果。
   
   2. **`std::suspend_always final_suspend()`**:
      - 在 `always_suspend_coroutine` 中，`final_suspend` 返回 `std::suspend_always`，因此协程在结束时会挂起，返回控制权给调用者。
      - 调用 `task.resume()` 一次后，协程会挂起，需要再次调用 `task.resume()` 才能完成并获取结果。
   
   ### 总结
   
   - **`std::suspend_never final_suspend()`**：适用于那些在结束时不需要挂起的协程，可以立即完成并释放资源。
   - **`std::suspend_always final_suspend()`**：适用于那些在结束时需要进行异步清理或其他多阶段完成操作的协程。
   
   选择合适的 `final_suspend` 策略可以确保协程在结束时的行为符合你的需求，提高代码的效率和可维护性。



# 协程访问在调用者中被挂起的协程数据

在 C++20 协程中，协程可以在挂起时保留其状态，这意味着协程的数据可以在挂起和恢复之间保持不变。如果你希望在调用者中访问被挂起的协程的数据，可以通过以下几种方法实现：

### 1. 使用 `promise_type` 访问数据

`promise_type` 是协程的内部状态管理器，可以通过它来存储和访问协程的数据。调用者可以通过 `std::coroutine_handle` 获取 `promise_type` 并访问其中的数据。

#### 示例代码

```cpp
#include <coroutine>
#include <iostream>
#include <optional>

// 定义协程的 promise 类型
struct MyPromise {
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_value(int value) { result = value; }
    void return_void() {}
    void unhandled_exception() {}

    int result;

    // 提供一个方法来访问协程的数据
    int get_result() const { return result; }
};

// 定义协程的返回类型
using MyTask = std::coroutine_handle<MyPromise>;

// 协程函数
MyTask my_coroutine() {
    std::cout << "Starting coroutine..." << std::endl;
    co_return 42;
}

// 主函数
int main() {
    auto task = my_coroutine();
    task.resume();

    if (task.done()) {
        // 通过 promise_type 访问协程的数据
        int result = task.promise().get_result();
        std::cout << "Task completed with result: " << result << std::endl;
    }

    return 0;
}
```

### 解释

1. **`MyPromise` 类**:
   - `initial_suspend` 和 `final_suspend` 控制协程的初始和最终挂起点。
   - `return_value` 用于设置协程的返回值。
   - `unhandled_exception` 用于处理未捕获的异常。
   - `get_result` 方法用于访问协程的内部数据 `result`。

2. **`MyTask` 类型**:
   - `MyTask` 是 `std::coroutine_handle<MyPromise>` 的别名，用于管理协程的生命周期。

3. **`my_coroutine` 函数**:
   - 协程函数 `my_coroutine` 计算一个值并使用 `co_return` 返回该值。

4. **主函数**:
   - 创建协程对象并调用 `resume` 方法来恢复协程的执行。
   - 检查协程是否完成，并通过 `task.promise().get_result()` 访问协程的内部数据。

### 2. 使用 `std::coroutine_handle` 的 `address` 方法

如果需要在多个地方访问协程的数据，可以将 `std::coroutine_handle` 的地址传递给其他函数或对象，从而在外部访问协程的状态。

#### 示例代码

```cpp
#include <coroutine>
#include <iostream>
#include <optional>

// 定义协程的 promise 类型
struct MyPromise {
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_value(int value) { result = value; }
    void return_void() {}
    void unhandled_exception() {}

    int result;

    // 提供一个方法来访问协程的数据
    int get_result() const { return result; }
};

// 定义协程的返回类型
using MyTask = std::coroutine_handle<MyPromise>;

// 协程函数
MyTask my_coroutine() {
    std::cout << "Starting coroutine..." << std::endl;
    co_return 42;
}

// 外部函数，通过 handle 访问协程的数据
void access_coroutine_data(MyTask task) {
    if (task.done()) {
        int result = task.promise().get_result();
        std::cout << "Accessing result from external function: " << result << std::endl;
    }
}

// 主函数
int main() {
    auto task = my_coroutine();
    task.resume();

    if (task.done()) {
        // 通过 promise_type 访问协程的数据
        int result = task.promise().get_result();
        std::cout << "Task completed with result: " << result << std::endl;

        // 通过外部函数访问协程的数据
        access_coroutine_data(task);
    }

    return 0;
}
```

### 解释

1. **`access_coroutine_data` 函数**:
   - 该函数接受一个 `MyTask` 对象作为参数，并通过 `task.promise().get_result()` 访问协程的内部数据。

2. **主函数**:
   - 创建协程对象并调用 `resume` 方法来恢复协程的执行。
   - 检查协程是否完成，并通过 `task.promise().get_result()` 访问协程的内部数据。
   - 调用 `access_coroutine_data` 函数，通过外部函数访问协程的数据。

### 总结

- **使用 `promise_type` 访问数据**：通过 `promise_type` 提供的方法，可以在调用者中直接访问协程的内部数据。
- **使用 `std::coroutine_handle` 的 `address` 方法**：将 `std::coroutine_handle` 的地址传递给其他函数或对象，从而在外部访问协程的状态。

这两种方法都可以有效地在调用者中访问被挂起的协程数据，选择哪种方法取决于你的具体需求和代码结构。



# 从调用者内部修改协程在其挂起状态下的数据

## 协程外部

## 协程内部



# 协程挂起或者恢复期间执行同步或者异步调用





