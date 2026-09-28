// Jarvis-HOOK: only externally declared deltas are added here. Native masks
// already contain native equipment effects and must never be applied twice.
bool EquipmentDeltas(unsigned owner,const void* actor,std::array<std::int32_t,ElementLimit>& output) noexcept {
    output={};if(pack.equipment.empty()||owner>=18)return true;
    EquipmentEffects::View view;
    if(!EquipmentEffects::Capture(module,owner,actor,Copy,view))return false;
    std::array<unsigned,10> seen{};unsigned seenCount=0;
    for(unsigned i=0;i<view.count;++i){
        const auto& entry=view.entries[i];const auto* binding=pack.Equipment(entry.word);
        if(!binding||(binding->kind!=2&&binding->kind!=entry.kind)||!(binding->owners&(1u<<owner)))continue;
        bool duplicate=false;for(unsigned j=0;j<seenCount;++j)if(seen[j]==entry.word)duplicate=true;
        if(duplicate)continue;seen[seenCount++]=entry.word;
        const auto* expected=admitted.ExpectedEquipment(entry.word);std::array<Byte,108> bytes{};
        if(!expected||!Copy(bytes.data(),expected->address,expected->width)||
           !admitted.Equipment(entry.word,expected->address,bytes.data(),expected->width))return false;
        if(binding->sos){
            Byte state=0;if(!Copy(&state,static_cast<const Byte*>(actor)+0x63E,1))return false;
            // Exact native39B2A0 applies SOS masks only in condition states1/2.
            if(state!=1&&state!=2)continue;
        }
        for(const auto& delta:binding->deltas){
            if(delta.element>=pack.registry.Size()||pack.registry.At(delta.element)->nativeBit)return false;
            output[delta.element]+=delta.deltaBp; // <=10 bounded bindings, each <=35000.
        }
    }
    return true;
}
