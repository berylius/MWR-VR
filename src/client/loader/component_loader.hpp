#pragma once
#include "component_interface.hpp"
#include "mwr_port.hpp"

class component_loader final
{
public:
	class premature_shutdown_trigger final : public std::exception
	{
		[[nodiscard]] const char* what() const noexcept override
		{
			return "Premature shutdown requested";
		}
	};

	template <typename T, bool enabled = true>
	class installer final
	{
		static_assert(std::is_base_of<component_interface, T>::value, "component has invalid base class");

	public:
		installer()
		{
			// Reject legacy components before their constructors/static owners run.
			if constexpr (enabled) register_component(std::make_unique<T>());
		}
	};

	template <typename T>
	static T* get()
	{
		for (const auto& component_ : get_components())
		{
			if (typeid(*component_.get()) == typeid(T))
			{
				return reinterpret_cast<T*>(component_.get());
			}
		}

		return nullptr;
	}

	static void register_component(std::unique_ptr<component_interface>&& component);

	static bool post_start();
	static bool post_load();
	static void post_unpack();
	static void pre_destroy();
	static void clean();

	static void* load_import(const std::string& library, const std::string& function);

	static void trigger_premature_shutdown();

private:
	static std::vector<std::unique_ptr<component_interface>>& get_components();
};

#define MWR_COMPONENT_JOIN_IMPL(a, b) a##b
#define MWR_COMPONENT_JOIN(a, b) MWR_COMPONENT_JOIN_IMPL(a, b)
#define REGISTER_COMPONENT(name)                          \
namespace                                                 \
{                                                         \
	static component_loader::installer<name, mwr_port::is_enabled(#name)> MWR_COMPONENT_JOIN(__component_, __COUNTER__); \
}
