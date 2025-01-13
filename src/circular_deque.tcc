// SPDX-License-Identifier: MIT
// Copyright 2018 Massachusetts Institute of Technology
#ifndef CIRCULAR_DEQUE_TCC
#define CIRCULAR_DEQUE_TCC

template<typename T>
circular_deque_base<T>::circular_deque_base(unsigned int capacity) noexcept :
		data_(reinterpret_cast<T*>(&first_)), capacity_(capacity), head_(0), size_(0) {}
template<typename T>
circular_deque_base<T>::~circular_deque_base() {
	std::destroy(prefix_begin(), prefix_end());
	std::destroy(suffix_begin(), suffix_end());
	if (!small())
#ifdef __SANITIZE_ADDRESS__
		free(data_);
#else
		sdallocx(data_, capacity_*sizeof(T), MALLOCX_ALIGN(alignof(T)));
#endif
}

template<typename T>
bool circular_deque_base<T>::small() const noexcept {
//	return data_ == reinterpret_cast<const T*>(&first_);
	return static_cast<const void*>(data_) == static_cast<const void*>(&first_);
}

template<typename T>
circular_deque_base<T>& circular_deque_base<T>::operator=(const circular_deque_base& other) {
	if (this == &other) return *this;

	if (size() >= other.size()) {
		//TODO: work out how the segments work out so we can use memcpy for
		//trivially-copyable types
		auto endpoint = std::copy(other.begin(), other.end(), begin());
		std::destroy(endpoint, end());
		size_ = other.size();
	} else if (other.size() <= capacity()) {
		std::copy(other.begin(), other.begin()+size(), begin());
		if (!linear()) //our free space is contiguous starting at suffix_end()
			std::uninitialized_copy(other.begin()+size(), other.end(), suffix_end());
		else {
			size_type beforeWrapping = capacity_ - (head_+size_);
			std::uninitialized_copy_n(other.begin()+size(), beforeWrapping, prefix_end());
			std::uninitialized_copy(other.begin()+size()+beforeWrapping, other.end(), suffix_begin());
		}
		size_ = other.size();
	} else {
		//We clear the elements we'd otherwise copy-assign into so we don't
		//copy them when growing.
		clear();
		ensureCapacity(other.size());
		append(other.prefix_begin(), other.prefix_end());
		append(other.suffix_begin(), other.suffix_end());
	}
	return *this;
}
template<typename T>
circular_deque_base<T>& circular_deque_base<T>::operator=(circular_deque_base&& other) {
	if (this == &other) return *this;
	if (!other.small()) {
		clear();
		if (!small())
#ifdef __SANITIZE_ADDRESS__
			free(data_);
#else
			sdallocx(data_, capacity_*sizeof(T), MALLOCX_ALIGN(alignof(T)));
#endif
		data_ = other.data_;
		head_ = other.head_;
		size_ = other.size_;
		//reset other to its at-construction state
		capacity_ = other.capacity_;
		other.data_ = reinterpret_cast<T*>(&other.first_);
		other.head_ = other.size_ = 0;
		//We don't know what the other vector's N was, but it's at least 1, so be safe.
		other.capacity_ = 1;
	} else if (other.size() <= capacity()) {
		std::move(other.begin(), other.begin()+size(), begin());
		if (!linear()) //our free space is contiguous starting at suffix_end()
			std::uninitialized_move(other.begin()+size(), other.end(), suffix_end());
		else {
			size_type beforeWrapping = capacity_ - (head_+size_);
			std::uninitialized_move_n(other.begin()+size(), beforeWrapping, prefix_end());
			std::uninitialized_move(other.begin()+size()+beforeWrapping, other.end(), suffix_begin());
		}
		size_ = other.size();
	} else {
		//We clear the elements we'd otherwise move-assign into so we don't
		//copy them when growing.
		clear();
		ensureCapacity(other.size());
		append(std::make_move_iterator(other.prefix_begin()), std::make_move_iterator(other.prefix_end()));
		append(std::make_move_iterator(other.suffix_begin()), std::make_move_iterator(other.suffix_end()));
	}
	return *this;
}

template<typename T>
bool circular_deque_base<T>::empty() const noexcept {
	return !size_;
}
template<typename T>
typename circular_deque_base<T>::size_type circular_deque_base<T>::size() const noexcept {
	return size_;
}
template<typename T>
typename circular_deque_base<T>::size_type circular_deque_base<T>::capacity() const noexcept {
	return capacity_;
}

template<typename T>
auto circular_deque_base<T>::begin() noexcept {
	//TODO: if (linear()) return prefix_begin();
	//but that isn't the same type as below
	return boost::range::join(boost::make_iterator_range(prefix_begin(), prefix_end()),
			boost::make_iterator_range(suffix_begin(), suffix_end())).begin();
}
template<typename T>
auto circular_deque_base<T>::begin() const noexcept {
	//TODO: if (linear()) return prefix_begin();
	//but that isn't the same type as below
	return boost::range::join(boost::make_iterator_range(prefix_begin(), prefix_end()),
			boost::make_iterator_range(suffix_begin(), suffix_end())).begin();
}
template<typename T>
auto circular_deque_base<T>::end() noexcept {
	//TODO: if (linear()) return prefix_end();
	//but that isn't the same type as below
	return boost::range::join(boost::make_iterator_range(prefix_begin(), prefix_end()),
			boost::make_iterator_range(suffix_begin(), suffix_end())).end();
}
template<typename T>
auto circular_deque_base<T>::end() const noexcept {
	//TODO: if (linear()) return prefix_end();
	//but that isn't the same type as below
	return boost::range::join(boost::make_iterator_range(prefix_begin(), prefix_end()),
			boost::make_iterator_range(suffix_begin(), suffix_end())).end();
}

template<typename T>
typename circular_deque_base<T>::size_type circular_deque_base<T>::reserve(size_type minCapacity) {
	if (minCapacity > capacity())
		ensureCapacity(minCapacity);
	return capacity();
}

template<typename T>
T& circular_deque_base<T>::front() noexcept {
	assert(!empty());
	return data_[head_];
}
template<typename T>
const T& circular_deque_base<T>::front() const noexcept {
	assert(!empty());
	return data_[head_];
}
template<typename T>
T& circular_deque_base<T>::back() noexcept {
	assert(!empty());
	return data_[mask(head_+size_-1)];
}
template<typename T>
const T& circular_deque_base<T>::back() const noexcept {
	assert(!empty());
	return data_[mask(head_+size_-1)];
}

template<typename T>
void circular_deque_base<T>::push_front(const T& e) {
	if (full()) ensureCapacity();
	head_ = dec(head_);
	::new (data_+head_) T(e);
	++size_;
}
template<typename T>
void circular_deque_base<T>::push_front(T&& e) {
	if (full()) ensureCapacity();
	head_ = dec(head_);
	::new (data_+head_) T(std::move(e));
	++size_;
}
template<typename T>
template<typename... Args>
void circular_deque_base<T>::emplace_front(Args... args) {
	if (full()) ensureCapacity();
	head_ = dec(head_);
	::new (data_+head_) T(std::forward<Args>(args)...);
	++size_;
}
template<typename T>
T circular_deque_base<T>::pop_front() {
	assert(!empty());
	T ret = std::move(front());
	(data_+head_)->~T();
	head_ = inc(head_);
	--size_;
	return ret;
}

template<typename T>
void circular_deque_base<T>::push_back(const T& e) {
	if (full()) ensureCapacity();
	::new(data_ + mask(head_+size_)) T(e);
	++size_;
}
template<typename T>
void circular_deque_base<T>::push_back(T&& e) {
	if (full()) ensureCapacity();
	::new(data_ + mask(head_+size_)) T(std::move(e));
	++size_;
}
template<typename T>
template<typename... Args>
void circular_deque_base<T>::emplace_back(Args... args) {
	if (full()) ensureCapacity();
	::new(data_ + mask(head_+size_)) T(std::forward<Args>(args)...);
	++size_;
}
template<typename T>
T circular_deque_base<T>::pop_back() {
	assert(!empty());
	T ret = std::move(back());
	--size_;
	(data_ + mask(head_+size_))->~T();
	return ret;
}

template<typename T>
void circular_deque_base<T>::clear() {
	std::destroy(prefix_begin(), prefix_end());
	std::destroy(suffix_begin(), suffix_end());
	size_ = 0;
	head_ = 0;
	//We don't shrink, so no change to data_ or capacity_.
}

template<typename T>
template<typename Iter>
void circular_deque_base<T>::append(Iter first, Iter last) {
	size_type incoming = numeric_cast<size_type>(std::distance(first, last));
	if (size() + incoming > capacity_)
		ensureCapacity(size() + incoming);

	if (linear()) {
		assert(capacity_ >= head_+size_);
		size_type remaining = capacity_ - (head_+size_);
		size_type segment = std::min(incoming, remaining);
		std::uninitialized_copy_n(first, segment, data_+(head_+size_));
		std::uninitialized_copy(first+segment, last, data_); //maybe wrap around
	} else
		std::uninitialized_copy(first, last, data_+mask(head_+size_));
	size_ += incoming;
}

template<typename T>
void circular_deque_base<T>::swap(circular_deque_base& other) {
	if (this == &other) return;
	using std::swap;
	if (!small() && !other.small()) {
		swap(data_, other.data_);
		swap(capacity_, other.capacity_);
		swap(head_, other.head_);
		swap(size_, other.size_);
	} else {
		if (other.size() > capacity())
			ensureCapacity(other.size());
		if (size() > other.capacity())
			other.ensureCapacity(size());

		size_type commonPrefix = std::min(size(), other.size());
		size_type p = head_, q = other.head_;
		for (size_type i = 0; i < commonPrefix; ++i, p = inc(p), q = other.inc(q))
			swap(data_[p], other.data_[q]);

		if (size() < other.size()) {
			size_type oldsize = size();
			if (other.data_+q < other.prefix_end()) {
				append(std::make_move_iterator(other.data_+q), std::make_move_iterator(other.prefix_end()));
				append(std::make_move_iterator(other.suffix_begin()), std::make_move_iterator(other.suffix_end()));
				std::destroy(other.data_+q, other.prefix_end());
				std::destroy(other.suffix_begin(), other.suffix_end());
			} else {
				append(std::make_move_iterator(other.data_+q), std::make_move_iterator(other.suffix_end()));
				std::destroy(other.data_+q, other.suffix_end());
			}
			//append adjusted our size already
			other.size_ = oldsize;
		} else if (size() > other.size()) {
			size_type oldsize = other.size();
			if (data_+p < prefix_end()) {
				other.append(std::make_move_iterator(data_+p), std::make_move_iterator(prefix_end()));
				other.append(std::make_move_iterator(suffix_begin()), std::make_move_iterator(suffix_end()));
				std::destroy(data_+p, prefix_end());
				std::destroy(suffix_begin(), suffix_end());
			} else {
				other.append(std::make_move_iterator(data_+p), std::make_move_iterator(suffix_end()));
				std::destroy(data_+p, suffix_end());
			}
			//other.append adjusted other.size
			size_ = oldsize;
		}
	}
}

template<typename T>
typename circular_deque_base<T>::size_type circular_deque_base<T>::mask(size_type i) const noexcept {
	assert(i < 2*capacity_);
	return i >= capacity_ ? i - capacity_ : i;
}
template<typename T>
typename circular_deque_base<T>::size_type circular_deque_base<T>::inc(size_type i) const noexcept {
	assert(i < capacity_);
	return i+1 >= capacity_ ? 0 : i+1;
}
template<typename T>
typename circular_deque_base<T>::size_type circular_deque_base<T>::dec(size_type i) const noexcept {
	assert(i < capacity_); //comparing vs 0 doesn't make sense for unsigned
	//deliberate underflow
	return i-1 >= capacity_ ? capacity_-1 : i-1;
}

template<typename T>
T* circular_deque_base<T>::prefix_begin() noexcept {
	return data_ + head_;
}
template<typename T>
const T* circular_deque_base<T>::prefix_begin() const noexcept {
	return data_ + head_;
}
template<typename T>
T* circular_deque_base<T>::prefix_end() noexcept {
	return data_ + std::min(head_ + size_, capacity_);
}
template<typename T>
const T* circular_deque_base<T>::prefix_end() const noexcept {
	return data_ + std::min(head_ + size_, capacity_);
}
template<typename T>
T* circular_deque_base<T>::suffix_begin() noexcept {
	return data_;
}
template<typename T>
const T* circular_deque_base<T>::suffix_begin() const noexcept {
	return data_;
}
template<typename T>
T* circular_deque_base<T>::suffix_end() noexcept {
	//underflow-aware code
	return head_ + size_ > capacity_ ? data_ + head_ + size_ - capacity_ : data_;
}
template<typename T>
const T* circular_deque_base<T>::suffix_end() const noexcept {
	//underflow-aware code
	return head_ + size_ > capacity_ ? data_ + head_ + size_ - capacity_ : data_;
}
template<typename T>
bool circular_deque_base<T>::linear() const noexcept {
	return suffix_begin() == suffix_end();
}

template<typename T>
void circular_deque_base<T>::ensureCapacity(size_type minCapacity) {
	size_type targetCapacity = 2 * capacity_; //TODO: tune growth factor (see Folly's FBVector docs)
	targetCapacity = std::max(targetCapacity, minCapacity);
	std::size_t targetBytes = targetCapacity * sizeof(T);
#ifdef __SANITIZE_ADDRESS__
	std::size_t actualBytes = targetBytes;
	void* allocation = malloc(actualBytes);
#else
	std::size_t actualBytes = nallocx(targetBytes, MALLOCX_ALIGN(alignof(T)));
	void* allocation = mallocx(actualBytes, MALLOCX_ALIGN(alignof(T)));
#endif
	//we may lose some bytes due to truncation, but that's okay, sdallocx copes
	size_type actualCapacity = numeric_cast<size_type>(actualBytes / sizeof(T));

	T* newData = reinterpret_cast<T*>(allocation);
	T* p = std::uninitialized_move(prefix_begin(), prefix_end(), newData);
	p = std::uninitialized_move(suffix_begin(), suffix_end(), p);
	std::destroy(prefix_begin(), prefix_end());
	std::destroy(suffix_begin(), suffix_end());
	if (!small())
#ifdef __SANITIZE_ADDRESS__
		free(data_);
#else
		//not sure if I'm supposed to pass flags here...
		sdallocx(data_, capacity_*sizeof(T), MALLOCX_ALIGN(alignof(T)));
#endif

	data_ = newData;
	capacity_ = actualCapacity;
	head_ = 0;
	size_ = numeric_cast<size_type>(p - newData);
}
template<typename T>
bool circular_deque_base<T>::full() const noexcept {
	return size_ == capacity_;
}



template<typename T, unsigned int N>
circular_deque<T, N>::circular_deque(const circular_deque& o) : circular_deque_base<T>(N) {
	if (!o.empty())
		circular_deque_base<T>::operator=(o);
}
template<typename T, unsigned int N>
circular_deque<T, N>::circular_deque(circular_deque&& o) : circular_deque_base<T>(N) {
	if (!o.empty())
		circular_deque_base<T>::operator=(std::move(o));
}
template<typename T, unsigned int N>
circular_deque<T, N>& circular_deque<T, N>::operator=(const circular_deque& o) {
	circular_deque_base<T>::operator=(o);
	return *this;
}
template<typename T, unsigned int N>
circular_deque<T, N>& circular_deque<T, N>::operator=(circular_deque&& o) {
	circular_deque_base<T>::operator=(std::move(o));
	return *this;
}

template<typename T, unsigned int N>
circular_deque<T, N>::circular_deque(const circular_deque_base<T>& o) : circular_deque_base<T>(N) {
	if (!o.empty())
		circular_deque_base<T>::operator=(o);
}
template<typename T, unsigned int N>
circular_deque<T, N>::circular_deque(circular_deque_base<T>&& o) : circular_deque_base<T>(N) {
	if (!o.empty())
		circular_deque_base<T>::operator=(std::move(o));
}
template<typename T, unsigned int N>
circular_deque<T, N>& circular_deque<T, N>::operator=(const circular_deque_base<T>& o) {
	circular_deque_base<T>::operator=(o);
	return *this;
}
template<typename T, unsigned int N>
circular_deque<T, N>& circular_deque<T, N>::operator=(circular_deque_base<T>&& o) {
	circular_deque_base<T>::operator=(std::move(o));
	return *this;
}

#endif /* CIRCULAR_DEQUE_TCC */

