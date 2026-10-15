#include "Utils/ShaderAllocation.h"

#include <ankerl/unordered_dense.h>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace
{
	std::array<unsigned, 32> destructions{};
	unsigned created{};

	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}

	struct FakeDriver
	{
		unsigned releases{};
		void Release() { ++releases; }
	};

	struct alignas(std::max_align_t) Wrapper
	{
		unsigned id;
		FakeDriver* shader;
		std::uint64_t metadata;

		~Wrapper() { ++destructions.at(id); }
	};

	using Owner = Util::ShaderAllocation::TrailingBytesOwner<Wrapper>;
	constexpr std::array<std::uint8_t, 7> payload{ 0x00, 0x17, 0x80, 0xff, 0x42, 0x01, 0xab };

	Owner Make(std::size_t a_size = payload.size())
	{
		auto owner = Util::ShaderAllocation::MakeWithTrailingBytes<Wrapper>(a_size);
		Require(owner->id == 0 && owner->shader == nullptr && owner->metadata == 0,
			"Wrapper must be value-initialized");
		Require(reinterpret_cast<std::uintptr_t>(owner.get()) % alignof(Wrapper) == 0,
			"Wrapper alignment was lost");
		owner->id = ++created;
		return owner;
	}

	std::uint8_t* TrailingBytes(Wrapper* a_wrapper)
	{
		return reinterpret_cast<std::uint8_t*>(a_wrapper) + sizeof(Wrapper);
	}
}

namespace RE
{
	struct BSShader
	{
		enum Type
		{
			Total = 3
		};
	};

	namespace BSGraphics
	{
		using VertexShader = Wrapper;
		struct PixelShader
		{};
		struct ComputeShader
		{};
	}
}

namespace
{
	struct Cache
	{
#include "shader_allocation_cache_under_test.h"
	};

	using VertexMap = decltype(Cache::vertexShaders)::value_type;
	using PixelMap = decltype(Cache::pixelShaders)::value_type;
	using ComputeMap = decltype(Cache::computeShaders)::value_type;
	static_assert(std::is_same_v<VertexMap::mapped_type, Owner>);
	static_assert(std::is_same_v<PixelMap::mapped_type, std::unique_ptr<RE::BSGraphics::PixelShader>>);
	static_assert(std::is_same_v<ComputeMap::mapped_type, std::unique_ptr<RE::BSGraphics::ComputeShader>>);
	static_assert(std::is_empty_v<Owner::deleter_type>);
	static_assert(sizeof(Owner) == sizeof(Wrapper*));
	static_assert(!std::is_convertible_v<Owner, std::unique_ptr<Wrapper>>);

	void TestLocalOwnership()
	{
		const auto firstId = created + 1;
		{
			auto owner = Make();
			auto* address = owner.get();
			for (std::size_t i = 0; i < payload.size(); ++i)
				TrailingBytes(address)[i] = payload[i];
			{
				auto moved = std::move(owner);
				Require(!owner && moved.get() == address, "Move must preserve the wrapper address");
				owner.reset();
				for (std::size_t i = 0; i < payload.size(); ++i)
					Require(TrailingBytes(moved.get())[i] == payload[i], "Move lost trailing bytecode");
				Require(destructions[firstId] == 0, "Moved-from owner destroyed the wrapper");
			}
			Require(destructions[firstId] == 1, "Local owner must destroy once");
		}

		auto owner = Make(0);
		const auto resetId = owner->id;
		owner.reset();
		owner.reset();
		Owner::deleter_type{}(nullptr);
		Require(destructions[resetId] == 1, "Reset must destroy once, including zero trailing bytes");

		auto destination = Make();
		const auto replacedId = destination->id;
		auto source = Make();
		const auto movedId = source->id;
		destination = std::move(source);
		Require(!source && destination->id == movedId && destructions[replacedId] == 1,
			"Move assignment must destroy the previous allocation");
	}

	void TestCacheOwnership()
	{
		Cache cache;
		auto& map = cache.vertexShaders[0];
		auto owner = Make();
		const auto firstId = owner->id;
		auto* address = owner.get();
		TrailingBytes(address)[payload.size() - 1] = payload.back();
		map.insert_or_assign(7, std::move(owner));
		Require(!owner && map.at(7).get() == address && destructions[firstId] == 0,
			"Cache insertion must transfer storage ownership");
		Require(TrailingBytes(map.at(7).get())[payload.size() - 1] == payload.back(),
			"Cache insertion lost trailing bytes");

		auto replacement = Make();
		const auto replacementId = replacement->id;
		map.insert_or_assign(7, std::move(replacement));
		Require(destructions[firstId] == 1 && destructions[replacementId] == 0,
			"Cache replacement must destroy only the old wrapper");
		map.erase(7);
		Require(destructions[replacementId] == 1, "Cache erasure must destroy the wrapper");

		const auto clearId = created + 1;
		for (std::uint32_t key = 0; key < 8; ++key)
			map.insert_or_assign(key, Make());
		map.clear();
		for (auto id = clearId; id <= created; ++id)
			Require(destructions[id] == 1, "Cache clearing must destroy every wrapper once");
		map.insert_or_assign(9, Make());
	}

	struct ThrowingHash
	{
		std::uint64_t operator()(std::uint32_t a_key) const
		{
			if (a_key == 1)
				throw std::runtime_error("insertion rejected");
			return a_key;
		}
	};

	void TestAbandonedPublication()
	{
		const auto abandonedId = created + 1;
		{
			auto prepared = Make();
		}
		Require(destructions[abandonedId] == 1, "Abandoned preparation must clean up");

		const auto failedId = created + 1;
		bool caught = false;
		try {
			ankerl::unordered_dense::map<std::uint32_t, Owner, ThrowingHash> map;
			auto prepared = Make();
			map.insert_or_assign(1, std::move(prepared));
		} catch (const std::runtime_error&) {
			caught = true;
		}
		Require(caught && destructions[failedId] == 1, "Failed insertion must clean up once");
	}

	struct ConstructionProbe
	{
		static inline unsigned attempts{};
		static inline unsigned memberDestructions{};
		static inline unsigned wrapperDestructions{};
		struct Member
		{
			~Member() { ++memberDestructions; }
		} member;
		ConstructionProbe()
		{
			++attempts;
			throw std::runtime_error("construction rejected");
		}
		~ConstructionProbe() { ++wrapperDestructions; }
	};

	void TestAllocationFailures()
	{
		bool overflow = false;
		try {
			auto owner = Util::ShaderAllocation::MakeWithTrailingBytes<ConstructionProbe>(
				(std::numeric_limits<std::size_t>::max)() - sizeof(ConstructionProbe) + 1);
		} catch (const std::bad_array_new_length&) {
			overflow = true;
		}
		Require(overflow && ConstructionProbe::attempts == 0, "Overflow must fail before construction");

		bool caught = false;
		try {
			auto owner = Util::ShaderAllocation::MakeWithTrailingBytes<ConstructionProbe>(payload.size());
		} catch (const std::runtime_error&) {
			caught = true;
		}
		Require(caught && ConstructionProbe::attempts == 1 && ConstructionProbe::memberDestructions == 1 &&
					ConstructionProbe::wrapperDestructions == 0,
			"Failed construction must unwind members without destroying an unconstructed wrapper");
	}

	void TestDriverResponsibility()
	{
		FakeDriver driver;
		{
			auto owner = Make();
			owner->shader = &driver;
		}
		Require(driver.releases == 0, "Storage destruction must not release the driver object");
		{
			Cache cache;
			auto owner = Make();
			owner->shader = &driver;
			cache.vertexShaders[0].insert_or_assign(1, std::move(owner));
			cache.vertexShaders[0].at(1)->shader->Release();
			cache.vertexShaders[0].clear();
		}
		Require(driver.releases == 1, "Caller release must not be duplicated by the storage deleter");
	}
}

int main()
{
	try {
		TestLocalOwnership();
		TestCacheOwnership();
		TestAbandonedPublication();
		TestAllocationFailures();
		TestDriverResponsibility();
		Require(destructions[0] == 0, "No untracked wrapper may be destroyed");
		for (unsigned id = 1; id <= created; ++id)
			Require(destructions[id] == 1, "Every constructed wrapper must be destroyed exactly once");
		std::cout << "Shader allocation tests passed (" << created << " wrapper lifetimes)\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
