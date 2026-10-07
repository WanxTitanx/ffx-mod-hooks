#include "../hooks/ExecutableStartupGate.h"
#include "ExecutableFixtureIdentity.h"
#include <array>
#include <cstdio>

int main(){
    std::array<unsigned char,32> expected{};
    const auto digit=[](char value){return value<='9'?value-'0':value-'a'+10;};
    for(unsigned i=0;i<32;++i)expected[i]=static_cast<unsigned char>(
        (digit(ExecutableFixtureIdentity::Sha256[2*i])<<4)|digit(ExecutableFixtureIdentity::Sha256[2*i+1]));
    unsigned checks=0,failures=0;
    const auto check=[&](bool value){++checks;if(!value)++failures;};
    using FfxHooks::ExecutableStartup::HashMatches;
    check(HashMatches(expected.data(),expected.size()));
    check(!HashMatches(nullptr,expected.size()));
    check(!HashMatches(expected.data(),expected.size()-1));
    check(!HashMatches(expected.data(),expected.size()+1));
    for(unsigned i=0;i<32;++i)for(unsigned bit=0;bit<8;++bit){
        expected[i]^=static_cast<unsigned char>(1u<<bit);
        check(!HashMatches(expected.data(),expected.size()));
        expected[i]^=static_cast<unsigned char>(1u<<bit);
    }
    std::printf("STARTUP HASH RT0: %u/%u passed\n",checks-failures,checks);
    return failures?1:0;
}
