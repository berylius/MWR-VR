#pragma once
#include <array>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace vr::native_post_aa
{
	inline constexpr std::array<const char*,6> menu_modes{
		"None","FXAA","SMAA 1x","SMAA T2x","Filmic SMAA 1x","Filmic SMAA T2x"};
	inline bool same_text(std::string_view a,std::string_view b) noexcept
	{
		if(a.size()!=b.size())return false;
		const auto lower=[](char c){return c>='A' && c<='Z' ? c+('a'-'A') : c;};
		for(std::size_t i=0;i<a.size();++i)if(lower(a[i])!=lower(b[i]))return false;
		return true;
	}
	inline std::optional<unsigned> saved_menu_mode(std::string_view profile)
	{
		std::istringstream lines{std::string(profile)};std::string line;
		std::optional<unsigned> result;
		while(std::getline(lines,line))
		{
			std::istringstream fields{line};std::string command,name,value;
			if(!(fields>>command>>name>>std::quoted(value)) ||
				(!same_text(command,"seta") && !same_text(command,"set")) ||
				(!same_text(name,"r_postAA") && !same_text(name,"0x2CF72D9E")))continue;
			result.reset();
			for(unsigned i=0;i<menu_modes.size();++i)
				if(same_text(value,menu_modes[i]) || value==std::to_string(i)){result=i;break;}
		}
		return result;
	}
}
