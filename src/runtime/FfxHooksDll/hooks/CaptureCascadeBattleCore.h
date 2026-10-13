#pragma once
#include "CaptureCascadeCore.h"
#include <vector>
namespace FfxHooks::CaptureCascade {
struct FormationProof {std::size_t size=0,slots=0;std::uint64_t hash=0;std::uint8_t replaceable=0;};
enum class FormationCode {Unchanged,Prepared,Invalid};
std::uint64_t FormationHash(const unsigned char*,std::size_t) noexcept;
FormationCode BuildFormation(const FormationProof&,const unsigned char*,std::size_t,
    const Config&,const State&,Aeon,const Candidate*,std::size_t,std::uint32_t,
    std::vector<unsigned char>&,Selection&) noexcept;
}
