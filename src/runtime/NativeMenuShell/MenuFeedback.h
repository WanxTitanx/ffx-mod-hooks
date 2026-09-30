#pragma once

namespace FfxHooks::MenuAudio {
using Sink=void(*)(int);
class Scope;
inline thread_local Scope* current=nullptr;
class Scope {
public:
    Scope() noexcept:parent_(current){current=this;}
    Scope(const Scope&)=delete;
    Scope& operator=(const Scope&)=delete;
    ~Scope(){
        if(current!=this)return;
        current=parent_;
        if(!sink_)return;
        if(parent_)parent_->Queue(id_,sink_);else sink_(id_);
    }
    void Queue(int id,Sink sink) noexcept {
        // Keep one audible result per input callback; errors outrank movement.
        if(!sink_||Priority(id)>=Priority(id_)){id_=id;sink_=sink;}
    }
    void Discard() noexcept {id_=0;sink_=nullptr;}
private:
    static int Priority(int id) noexcept {return id==3?3:id==4?2:1;}
    Scope* parent_;Sink sink_=nullptr;int id_=0;
};
// The native pump's SEH recovery may bypass C++ stack unwinding. Retire the
// thread-local owner before processing any later input on that thread.
inline void AbandonThreadFeedback() noexcept {current=nullptr;}
inline void Dispatch(int id,Sink sink){
    if(!sink)return;
    if(current&&(id==1||id==3||id==4))current->Queue(id,sink);
    else sink(id);
}
}
