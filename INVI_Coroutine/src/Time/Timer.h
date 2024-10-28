#pragma once

#include <iostream>
#include <chrono>
#include <thread>

template<typename T = std::chrono::seconds>
void sleep(int64_t n) {
	std::this_thread::sleep_for(T(n));
}

class Timer {
public:
	Timer() : m_begin(std::chrono::high_resolution_clock::now()) {}

	~Timer() {
		m_end = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double, std::milli> duration = m_end - m_begin;
		std::cout << "Time elapsed: " << duration.count() << " ms" << std::endl;
	}

	void reset() {
		m_begin = std::chrono::high_resolution_clock::now();
	}

	// 获取时间函数模板
	template<typename T = std::chrono::milliseconds>
	int64_t elapsed() {
		return std::chrono::duration_cast<T>(std::chrono::high_resolution_clock::now() - m_begin).count();
	}

private:
	std::chrono::time_point<std::chrono::high_resolution_clock> m_begin;		// 计时开始时间
	std::chrono::time_point<std::chrono::high_resolution_clock> m_end;		  // 计时结束时间
};

