#pragma once

#include "allocators.h"
#include "array.h"

namespace Lumix {

// Array with built-in storage
template <typename T, u32 N>
struct StackArray : Array<T> {
	StackArray(IAllocator& fallback)
		: Array<T>(m_stack_allocator)
		, m_stack_allocator(fallback)
	{
		Array<T>::reserve(N);
	}
	
	~StackArray() {
		clear();
		if (m_data) m_allocator.deallocate_aligned(m_data);
		m_data = nullptr;
		m_capacity = 0;
	}

private:
	StackAllocator<sizeof(T) * N, alignof(T)> m_stack_allocator;
};


} // namespace Lumix
