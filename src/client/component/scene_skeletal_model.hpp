#pragma once
#include <cstdint>
#include <cstdint>
#include "game/assets.hpp"
#include <array>
#include <memory>
#include <span>

namespace scene_models
{
	// A retained native DObj for a continuously posed prop. Native skinning owns
	// vertex deformation and frame storage; the source mesh/materials stay intact.
	class skeletal_model
	{
		struct storage;
		std::unique_ptr<storage> data_;
	public:
		using pose_provider=bool(*)(const void*,std::span<game::DObjAnimMat>) noexcept;
		using pose_observer=void(*)(const void*,const void*,std::uint32_t,float) noexcept;
		// 4000..4003 belong to carried weapons; 4032 is the native table bound.
		static constexpr unsigned scene_begin=4004,capacity=8;
		skeletal_model();~skeletal_model();
		bool create(game::XModel*,pose_provider=nullptr,pose_observer=nullptr);
		// Absolute world-space bones. The provider converts them to the current
		// native render origin while applying the pose, including cache reuse.
		// model_scale describes the matching local mesh to the observer; the
		// supplied world-space bones already include their positional size.
		bool submit(std::span<const game::DObjAnimMat> world_bones,unsigned flags=0,float model_scale=1.f);
		// Called with the DObj lock held at the existing skeleton boundary, after
		// native evaluation and before any skin jobs are published.
		static bool owns(const void*) noexcept;
		static void apply(void*) noexcept;
		static void enable() noexcept;
	};
}
