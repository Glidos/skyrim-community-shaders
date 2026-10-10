#pragma once

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace GrassPassCache
{
	struct Key
	{
		uint32_t pass, vertex, pixel, extra;
		auto operator<=>(const Key&) const = default;
	};

	enum class Outcome
	{
		Native,
		Batched
	};

	/** @brief Retains storage for a bucket's small set of pass outcomes across frames. */
	class Cache
	{
	public:
		/** @brief Forget completed passes while retaining capacity for the next frame. */
		void Clear() noexcept
		{
			entries.clear();
			last = 0;
		}

		/** @brief Admit each exact pass once; the returned pointer lasts until the next Acquire or Clear. */
		std::pair<Outcome*, bool> Acquire(const Key& key)
		{
			if (last < entries.size() && entries[last].key == key)
				return { &entries[last].outcome, false };
			const auto found = std::lower_bound(entries.begin(), entries.end(), key,
				[](const Entry& entry, const Key& value) { return entry.key < value; });
			const auto index = static_cast<std::size_t>(found - entries.begin());
			if (found != entries.end() && found->key == key) {
				last = index;
				return { &found->outcome, false };
			}
			const auto inserted = entries.insert(found, { key, Outcome::Native });
			last = index;
			return { &inserted->outcome, true };
		}

	private:
		struct Entry
		{
			Key key;
			Outcome outcome;
		};
		std::vector<Entry> entries;
		std::size_t last = 0;
	};
}
