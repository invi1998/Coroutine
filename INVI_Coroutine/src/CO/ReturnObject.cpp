#include "ReturnObject.h"

ReturnObject invi_generator(int start, int end, int step)
{
	for (int i = start; i < end; i += step)
	{
		co_yield i;
	}
	co_return std::suspend_never{};
}
