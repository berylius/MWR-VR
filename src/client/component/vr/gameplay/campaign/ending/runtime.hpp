#pragma once
#include "interaction.hpp"
#include "../../scripted_arms_policy.hpp"
#include "../../weapon_carry.hpp"
#include <string>

namespace vr::gameplay::ending
{
    struct presentation
    {
        stage phase{};int body{-1},secondary_body{-1},knife{-1},shepherd{-1},carrier{-1};
        bool active{},independent{},hide_body{},ground_actor{};
        int waiting_price{-1};bool helper_camera{};
        std::uint64_t reference{},lease{};
        unsigned held{};std::array<hands::anchor,2> grips{};
        const weapons::profile* hand_pose{};
        bool throwing_blade{};
        hands::anchor knife_attachment{};
        unsigned crawl_held{};std::array<hands::anchor,2> crawl_wrists{};
    };
#if defined(MWR_NATIVE_HOST)
    // This provider describes MW2CR's Shepherd ending, absent from MWR. Exclude
    // its H2 script hooks/addresses rather than loading an inactive H2 service.
    inline presentation latest() noexcept { return {}; }
    inline void update() {}
    inline void constrain_hands(std::array<hands::anchor,2>&,const std::array<hands::quat,2>&,hands::vec,std::uint64_t) noexcept {}
    inline bool owns_pose(const void*) noexcept { return false; }
    inline bool wants_pose(const void*,const unsigned*) noexcept { return false; }
    inline void apply_pose(void*,bool) noexcept {}
    inline std::string status() { return "MW2CR ending provider unavailable for the MWR campaign\n"; }
#else
    presentation latest() noexcept;
    void update(); // Existing server sequence owner, before publishing its view.
    void constrain_hands(std::array<hands::anchor,2>&,const std::array<hands::quat,2>&,hands::vec,std::uint64_t) noexcept;
    bool owns_pose(const void*) noexcept;
    bool wants_pose(const void*,const unsigned*) noexcept;
    void apply_pose(void*,bool rebuilt) noexcept;
    std::string status();
#endif
}
