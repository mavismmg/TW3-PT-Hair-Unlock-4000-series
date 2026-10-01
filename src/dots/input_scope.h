#pragma once
#include "gpu_runtime.h"
#include "checked_memory.h"
namespace witcher_dots {
// No cache outlives its builder call. Reentrant builders have independent
// retained frontends/resources and restore the outer TLS scope on return.
struct OwnerScope {void* owner{};ID3D12GraphicsCommandList4* list{};bool havePrebuild{};InputReuse reuse;ScopedReadRange memory;};
inline thread_local OwnerScope* ownerScope{};
template<class T> bool ReadOwnerField(const void* owner,size_t offset,T& out) noexcept {
    // Another/reentrant owner, an unvalidated scope, or a field outside the
    // bounded proof takes the full checked path. Never consult an outer scope.
    if(const auto* scope=ownerScope;scope&&scope->owner==owner&&scope->memory.Contains(owner,offset,sizeof(T)))
        return scope->memory.Read(owner,offset,out);
    return Read(owner,offset,out);
}
class OwnerScopeBinding {
    OwnerScope* previous_;
public:
    explicit OwnerScopeBinding(OwnerScope& scope) noexcept : previous_(ownerScope) {ownerScope=&scope;}
    OwnerScopeBinding(const OwnerScopeBinding&)=delete;
    ~OwnerScopeBinding() {ownerScope=previous_;}
};
}
