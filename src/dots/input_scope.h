#pragma once
#include "gpu_runtime.h"
namespace witcher_dots {
// No cache outlives its builder call. Reentrant builders have independent
// retained frontends/resources and restore the outer TLS scope on return.
struct OwnerScope {void* owner{};ID3D12GraphicsCommandList4* list{};bool havePrebuild{};InputReuse reuse;};
inline thread_local OwnerScope* ownerScope{};
class OwnerScopeBinding {
    OwnerScope* previous_;
public:
    explicit OwnerScopeBinding(OwnerScope& scope) noexcept : previous_(ownerScope) {ownerScope=&scope;}
    OwnerScopeBinding(const OwnerScopeBinding&)=delete;
    ~OwnerScopeBinding() {ownerScope=previous_;}
};
}
