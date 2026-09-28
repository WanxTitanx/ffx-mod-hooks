#pragma once
#include "MonsterRewardsCore.h"
#include <array>
#include <charconv>
#include <string>
#include <string_view>

namespace FfxHooks::MonsterRewards {
inline constexpr std::string_view SettingsHeader="ffx.monster-rewards.v1";
inline constexpr std::size_t MaximumSettingsBytes=65535;
struct Rates {std::uint16_t ap=1,gil=1;};
using RateTable=std::array<Rates,SpeciesCount>;
inline bool ParseSettings(std::string_view input,RateTable& output) noexcept {
    output={};if(input.size()>MaximumSettingsBytes)return false;
    const auto first=input.find('\n');if(first==std::string_view::npos)return false;
    auto header=input.substr(0,first);if(!header.empty()&&header.back()=='\r')header.remove_suffix(1);
    if(header!=SettingsHeader)return false;
    input.remove_prefix(first+1);
    RateTable candidate{};std::array<bool,SpeciesCount> seen{};
    while(!input.empty()){
        const auto end=input.find('\n');if(end==std::string_view::npos)return false;
        auto row=input.substr(0,end);input.remove_prefix(end+1);
        if(!row.empty()&&row.back()=='\r')row.remove_suffix(1);
        if(row.empty())return false;
        unsigned values[3]{};
        for(unsigned i=0;i<3;++i){
            const auto at=row.find('\t');
            if((i<2&&at==std::string_view::npos)||(i==2&&at!=std::string_view::npos))return false;
            const auto field=i<2?row.substr(0,at):row;
            if(field.empty())return false;
            const auto result=std::from_chars(field.data(),field.data()+field.size(),values[i]);
            if(result.ec!=std::errc{}||result.ptr!=field.data()+field.size())return false;
            if(i<2)row.remove_prefix(at+1);
        }
        if(values[0]>=SpeciesCount||seen[values[0]]||!values[1]||values[1]>MaximumMultiplier||!values[2]||values[2]>MaximumMultiplier)return false;
        seen[values[0]]=true;candidate[values[0]]={static_cast<std::uint16_t>(values[1]),static_cast<std::uint16_t>(values[2])};
    }
    output=candidate;return true;
}
inline bool SerializeSettings(const RateTable& rates,std::string& output){
    std::string text(SettingsHeader);text+='\n';
    for(unsigned i=0;i<rates.size();++i){const auto& row=rates[i];
        if(!row.ap||row.ap>MaximumMultiplier||!row.gil||row.gil>MaximumMultiplier)return false;
        if(row.ap==1&&row.gil==1)continue;
        text+=std::to_string(i)+'\t'+std::to_string(row.ap)+'\t'+std::to_string(row.gil)+'\n';
    }
    if(text.size()>MaximumSettingsBytes)return false;
    output=std::move(text);return true;
}
}
