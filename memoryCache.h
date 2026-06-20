#pragma once

#include <memory>
#include <atomic>
#include <chrono>
#include <unordered_map>
#include <array>
#include <cstring>
#include <memory.hpp>

namespace safety {
	template<typename T>
	class cached {
	public:
		cached() = default;
		cached(const cached&) = delete;
		cached& operator=(const cached&) = delete;
		cached(cached&&) = delete;
		cached& operator=(cached&&) = delete;

		void publish(T value) {
			m_ptr.store(std::make_shared<const T>(std::move(value)), std::memory_order_release);
		}

		std::shared_ptr<const T> snapshot() const {
			return m_ptr.load(std::memory_order_acquire);
		}

		template<typename Func>
		bool with(Func&& fn) const {
			auto snap = snapshot();
			if (!snap) return false;
			fn(*snap);
			return true;
		}

		void clear() {
			m_ptr.store({}, std::memory_order_release);
		}

		bool valid() const {
			return snapshot() != nullptr;
		}

	private:
		std::atomic<std::shared_ptr<const T>> m_ptr{};
	};

	template<typename T>
	class owner {
	public:
		safety::cached<T> cache;

		void update(T value) {
			cache.publish(std::move(value));
		}
	};
}

// Usage:
//   cm cache(drv);
//   cache.direct<uint64_t>(addr);  // 
//   cache.fast<uint64_t>(addr);    // uncached  bones, position
//   cache.medium<uint64_t>(addr);  // 500ms TTL  structural pointers
//   cache.slow<uint64_t>(addr);    // 15s TTL  FName pool, class keys

class cm {
public:
	explicit cm(Memory& mem) : mem_(&mem) {}
	cm(const cm&) = delete;
	cm& operator=(const cm&) = delete;

	Memory* mem() const { return mem_; }

	template<typename T>
	T direct(std::uint64_t addr) { return mem_->Read<T>(addr); }
	bool direct(std::uint64_t addr, void* buf, std::size_t size) { return mem_->ReadRaw(addr, buf, size); }

	template<typename T>
	T fast(std::uint64_t addr) { return read_cached<T>(addr, fast_cache_, std::chrono::microseconds(10)); }
	bool fast(std::uint64_t addr, void* buf, std::size_t size) { return read_cached(addr, buf, size, fast_cache_, std::chrono::microseconds(10)); }

	template<typename T>
	T debit(std::uint64_t addr) { return read_cached<T>(addr, fast_cache_, std::chrono::milliseconds(2)); }
	bool debit(std::uint64_t addr, void* buf, std::size_t size) { return read_cached(addr, buf, size, fast_cache_, std::chrono::milliseconds(2)); }

	template<typename T>
	T medium(std::uint64_t addr) { return read_cached<T>(addr, medium_cache_, std::chrono::milliseconds(500)); }
	bool medium(std::uint64_t addr, void* buf, std::size_t size) { return read_cached(addr, buf, size, medium_cache_, std::chrono::milliseconds(500)); }

	template<typename T>
	T slow(std::uint64_t addr) { return read_cached<T>(addr, slow_cache_, std::chrono::seconds(15)); }
	bool slow(std::uint64_t addr, void* buf, std::size_t size) { return read_cached(addr, buf, size, slow_cache_, std::chrono::seconds(15)); }

	void evict_stale_medium(std::chrono::steady_clock::duration max_age) {
		const auto cutoff = std::chrono::steady_clock::now() - max_age;
		for (auto it = medium_cache_.begin(); it != medium_cache_.end(); ) {
			if (it->second.ts < cutoff)
				it = medium_cache_.erase(it);
			else
				++it;
		}
	}


	void evict_stale_slow(std::chrono::steady_clock::duration max_age) {
		const auto cutoff = std::chrono::steady_clock::now() - max_age;
		for (auto it = slow_cache_.begin(); it != slow_cache_.end(); ) {
			if (it->second.ts < cutoff)
				it = slow_cache_.erase(it);
			else
				++it;
		}
	}

	void clear() { medium_cache_.clear(); slow_cache_.clear(); }

private:
	struct entry {
		static constexpr std::size_t capacity = 128;
		alignas(8) std::array<std::uint8_t, capacity> data{};
		std::size_t size{};
		std::chrono::steady_clock::time_point ts{};
	};

	template<typename T, typename Duration>
	T read_cached(std::uint64_t addr, std::unordered_map<std::uint64_t, entry>& cache, Duration ttl) {
		if (!addr) return T{};

		constexpr auto n = sizeof(T);
		const auto now = std::chrono::steady_clock::now();

		if (auto it = cache.find(addr); it != cache.end() && it->second.size == n && (now - it->second.ts) < ttl) {
			T result{};
			std::memcpy(&result, it->second.data.data(), n);
			return result;
		}

		T value = mem_->Read<T>(addr);

		auto& e = cache[addr];
		if (e.size != n) { e.data = {}; e.size = n; }
		std::memcpy(e.data.data(), &value, n);
		e.ts = now;
		return value;
	}

	template<typename Duration>
	bool read_cached(std::uint64_t addr, void* buf, std::size_t size,
		std::unordered_map<std::uint64_t, entry>& cache, Duration ttl) {
		if (!addr || !buf || size == 0) return false;

		if (size > entry::capacity)
			return mem_->ReadRaw(addr, buf, size);

		const auto now = std::chrono::steady_clock::now();

		if (auto it = cache.find(addr); it != cache.end() && it->second.size == size && (now - it->second.ts) < ttl) {
			std::memcpy(buf, it->second.data.data(), size);
			return true;
		}

		if (!mem_->ReadRaw(addr, buf, size))
			return false;

		auto& e = cache[addr];
		if (e.size != size) { e.data = {}; e.size = size; }
		std::memcpy(e.data.data(), buf, size);
		e.ts = now;
		return true;
	}

	Memory* mem_;
	std::unordered_map<std::uint64_t, entry> fast_cache_;
	std::unordered_map<std::uint64_t, entry> medium_cache_;
	std::unordered_map<std::uint64_t, entry> slow_cache_;
};
