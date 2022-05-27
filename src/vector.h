#pragma once

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>


template <typename T>
class RawMemory {
public:
	RawMemory() = default;

	explicit RawMemory(size_t capacity)
		: buffer_(Allocate(capacity))
		, capacity_(capacity) {
	}

	RawMemory(const RawMemory&) = delete;
	RawMemory& operator=(const RawMemory& rhs) = delete;

	RawMemory(RawMemory&& other) noexcept : buffer_(other.GetAddress()), capacity_(other.Capacity()) {
		other.buffer_ = nullptr;
		other.capacity_ = 0;
	}
	RawMemory& operator=(RawMemory&& rhs) noexcept {
		buffer_ = rhs.GetAddress();
		rhs.buffer_ = nullptr;
		capacity_ = rhs.Capacity();
		rhs.capacity_ = 0;
		return *this;
	}

	~RawMemory() {
		Deallocate(buffer_);
	}

	T* operator+(size_t offset) noexcept {
		assert(offset <= capacity_);
		return buffer_ + offset;
	}

	const T* operator+(size_t offset) const noexcept {
		return const_cast<RawMemory&>(*this) + offset;
	}

	const T& operator[](size_t index) const noexcept {
		return const_cast<RawMemory&>(*this)[index];
	}

	T& operator[](size_t index) noexcept {
		assert(index < capacity_);
		return buffer_[index];
	}

	void Swap(RawMemory& other) noexcept {
		std::swap(buffer_, other.buffer_);
		std::swap(capacity_, other.capacity_);
	}

	const T* GetAddress() const noexcept {
		return buffer_;
	}

	T* GetAddress() noexcept {
		return buffer_;
	}

	size_t Capacity() const {
		return capacity_;
	}

private:
	static T* Allocate(size_t n) {
		return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
	}

	static void Deallocate(T* buf) noexcept {
		operator delete(buf);
	}

	T* buffer_ = nullptr;
	size_t capacity_ = 0;
};

template <typename T>
class Vector {
public:
	using iterator = T*;
	using const_iterator = const T*;

	Vector() = default;

	explicit Vector(size_t size) : data_(size), size_(size)	{
		std::uninitialized_value_construct_n(data_.GetAddress(), size);
	}

	Vector(const Vector& other)  {
		RawMemory<T> new_data(other.Size());
		size_ = other.Size();
		std::uninitialized_copy_n(other.data_.GetAddress(), other.Size(), new_data.GetAddress());
		data_.Swap(new_data);
	}

	Vector(Vector&& other) noexcept  : data_(std::move(other.data_)) {
		size_ = std::exchange(other.size_, -0);
	 }

	~Vector() {
		DestroyN(data_.GetAddress(), size_);
	}

	Vector& operator=(const Vector& rhs) {
		if (this != &rhs) {
			if (rhs.size_ > data_.Capacity()) {
				Vector rhs_copy(rhs);
				Swap(rhs_copy);
			} else {
				if (rhs.size_ < size_) {
					auto end = std::copy_n(rhs.data_.GetAddress(), rhs.size_, data_.GetAddress());
					std::destroy_n(end, size_ - rhs.size_);
				} else {
					auto end = std::copy_n(rhs.data_.GetAddress(), size_, data_.GetAddress());
					std::uninitialized_copy_n(rhs.data_.GetAddress() + size_, rhs.size_ - size_, end);
				}
				size_ = rhs.size_;
			}
		}
		return *this;
	}

	Vector& operator=(Vector&& rhs) noexcept {
		data_ = std::move(rhs.data_);
		size_ = std::exchange(rhs.size_, -0);
		return *this;
	}

	static void CopyConstruct(T* buf, const T& elem) {
		new (buf) T(elem);
	}

	size_t Size() const noexcept {
		return size_;
	}

	size_t Capacity() const noexcept {
		return data_.Capacity();
	}

	const T& operator[](size_t index) const noexcept {
		return const_cast<Vector&>(*this)[index];
	}

	T& operator[](size_t index) noexcept {
		assert(index < size_);
		return data_[index];
	}

	void Resize(size_t new_size) {
		if (new_size > Capacity()) {
			Reserve(new_size);
			std::uninitialized_value_construct_n(&data_[size_], new_size - size_);
		} else  {
			if (new_size < size_) {
				std::destroy_n(&data_[new_size], size_ - new_size);
			} else {
				std::uninitialized_value_construct_n(&data_[size_], new_size - size_);
			}
		}
		size_ = new_size;
	}

	void PushBack(const T& value) {
		EmplaceBack(value);
	}

	void PushBack(T&& value) {
		EmplaceBack(std::move(value));
	}

	void PopBack() noexcept {
		if (size_ != 0) {
			std::destroy_n(&data_[size_ - 1], 1);
			--size_;
		}
	}

	template <typename... Args>
	T& EmplaceBack(Args&&... args) {
		Emplace(end(), std::forward<Args>(args)...);
		return data_[size_ -1];
	}

	iterator begin() noexcept { return data_.GetAddress(); }
	iterator end() noexcept { return  begin() + size_;}
	const_iterator begin() const noexcept {	return data_.GetAddress(); }
	const_iterator end() const noexcept { return  begin() + size_;	}
	const_iterator cbegin() const noexcept { return data_.GetAddress(); }
	const_iterator cend() const noexcept { return  begin() + size_; }


	template <typename... Args>
	iterator Emplace(const_iterator pos, Args&&... args) {
		size_t position_num = pos - begin();
		if (size_ == Capacity()) {
			MemoryRelocateEmplace(position_num, std::forward<Args>(args)...);
		} else {
			NoMemoryRelocateEmplace(position_num, std::forward<Args>(args)...);
		}
		return &data_[position_num];
	}

	iterator Erase(const_iterator pos) noexcept(std::is_nothrow_move_assignable_v<T>) {
		size_t position_num = pos - begin();
		if (size_ != 0) {
			data_[position_num] = std::move(data_[position_num +1]);
			std::move(&data_[position_num +1], &data_[size_ -1 ], &data_[position_num]);
			std::destroy_n(&data_[size_ -1], 1);
			--size_;
		}
		return &data_[position_num];
	}

	iterator Insert(const_iterator pos, const T& value) {
		return Emplace(pos, value);
	}
	iterator Insert(const_iterator pos, T&& value) {
		return Emplace(pos, std::move(value));
	}

	static void Destroy(T* buf) noexcept {
		buf->~T();
	}
	static void DestroyN(T* buf, size_t n) noexcept {
		for (size_t i = 0; i != n; ++i) {
			Destroy(buf + i);
		}
	}

	void Swap(Vector& other) noexcept {
		data_.Swap(other.data_);
		size_t temp_size = size_;
		size_ = other.size_;
		other.size_ = temp_size;
	}

	void Reserve(size_t new_capacity) {
		if (new_capacity <= this->Capacity()) {
			return;
		}
		RawMemory<T> new_data(new_capacity);
		if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
			std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
		} else {
			std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
		}
		data_.Swap(new_data);
		DestroyN(new_data.GetAddress(), size_);
	}

private:
	RawMemory<T> data_;
	size_t size_ = 0;

	template <typename... Args>
	void MemoryRelocateEmplace(const size_t position_num, Args&&... args) {
		RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
		new (&new_data[position_num]) T(std::forward<Args>(args)...);
		if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
			if (position_num != 0) {
				std::uninitialized_move_n(data_.GetAddress(), position_num , new_data.GetAddress());
			}
			if (size_ > position_num) {
				std::uninitialized_move_n(&data_[position_num], size_ - position_num, &new_data[position_num + 1]);
			}
		} else {
			if (position_num != 0) {
				std::uninitialized_copy_n(data_.GetAddress(), position_num , new_data.GetAddress());
			}
			if (size_ > position_num) {
				std::uninitialized_copy_n(&data_[position_num], size_ - position_num, &new_data[position_num + 1]);
			}
		}
		std::destroy_n(data_.GetAddress(), size_);
		data_.Swap(new_data);
		++size_;
	}

	template <typename... Args>
	void NoMemoryRelocateEmplace(const size_t position_num, Args&&... args) {
		if (position_num == size_) {
			new (&data_[position_num]) T(std::forward<Args>(args)...);
		} else {
			T temp(std::forward<Args>(args)...);
			new (&data_[size_]) T(std::move(data_[size_ - 1]));
			std::move_backward(&data_[position_num], &data_[size_ -1], &data_[size_]);
			data_[position_num] = std::move(temp);
		}
		++size_;
	}
};
