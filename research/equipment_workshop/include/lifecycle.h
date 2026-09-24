#pragma once
#include "workshop.h"
#include <thread>
namespace workshop {
enum class InventoryEvent { Created, Swapped, Removed, Equipped, Unequipped };
// Owner-thread observer of completed native producers. No fingerprint matching,
// automatic installation or disk I/O. An unobserved producer quarantines the lane.
class Lifecycle {
public:
    bool Begin(bool enabled,bool exactProfile,const State&);
    bool Observe(InventoryEvent,unsigned first,unsigned second,unsigned owner,
                 std::uint64_t expectedRevision,const std::uint8_t* before,
                 const std::uint8_t* after);
    bool Snapshot(State&) const;
    void End();
private:
    bool Owned() const;
    bool active_=false;
    State state_{};
    std::thread::id owner_{};
};
}
