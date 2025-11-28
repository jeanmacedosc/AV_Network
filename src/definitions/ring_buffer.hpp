#ifndef RING_BUFFER_HPP
#define RING_BUFFER_HPP

#include <array>
#include <mutex>
#include <optional>
#include <atomic>

/**
 * @brief Thread-safe circular buffer
 * @tparam T The type of data to store
 * @tparam Size The maximum number of elements
 */
template <typename T, size_t Size>
class RingBuffer {
public:
    RingBuffer() : _head(0), _tail(0), _is_full(false) {}

    /**
     * @brief Pushes an item into the buffer. 
     * If full, it overwrites the oldest data (Drop-Oldest policy) 
     * or returns false (Drop-Newest policy). 
     */
    bool push(const T& item) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (_is_full) {
            return false;
        }

        _buffer[_head] = item;
        _head = (_head + 1) % Size;
        _is_full = (_head == _tail);

        return true;
    }

    /**
     * @brief Pop an item from the buffer
     */
    std::optional<T> pop() {
        std::lock_guard<std::mutex> lock(_mutex);

        if (empty()) {
            return std::nullopt;
        }

        T item = _buffer[_tail];
        _tail = (_tail + 1) % Size;
        _is_full = false;

        return item;
    }

    bool empty() const {
        return (!_is_full && (_head == _tail));
    }
    
    bool full() const {
        return _is_full;
    }

private:
    std::array<T, Size> _buffer;
    size_t _head;
    size_t _tail;
    bool _is_full;
    mutable std::mutex _mutex;
};

#endif // RING_BUFFER_HPP