#pragma once
#include "dots/witcher_dots.h"
#include <deps/imgui/imgui.h>
#include <cstring>
#include <sstream>
#include <string>

namespace hair_overlay {
inline constexpr const char* kVersion="1.0.0";
inline const char* YesNo(bool value) {return value?"Yes":"No";}
inline double MiB(uint64_t bytes) {return static_cast<double>(bytes)/(1024.0*1024.0);}
inline bool Tracing(const char* activity) {return std::strcmp(activity,"active (tracing converted hair)")==0;}
inline bool ShowReason(const witcher_dots::Snapshot& s,const char* activity) {
    return s.reason[0]&&(s.stage!=witcher_dots::Stage::Active
        ||std::strstr(activity,"rejected")!=nullptr);
}
inline std::string Diagnostics(const witcher_dots::Snapshot& s,const char* activity,const char* stage) {
    std::ostringstream out;
    out<<"TW3 PT Hair Unlock "<<kVersion<<"\nStatus: "<<activity<<"\nBackend: "<<stage
       <<"\nWitcher 3 process: "<<YesNo(s.applicable)<<"\nFallback requested: "<<YesNo(s.requested)
       <<"\nGPU PCI ID: 0x"<<std::hex<<s.deviceId<<std::dec
       <<"\nNVIDIA driver: "<<s.driverVersion<<" (hundredths)\nConverter ready: "<<YesNo(s.shaderReady)
       <<"\nGame Path Traced Hair: "<<YesNo(s.gameHairTraced)<<"\nDetail: "<<s.reason
       <<"\nPrebuilds/builds/updates: "<<s.prebuilds<<" / "<<s.builds<<" / "<<s.updates
       <<"\nShader libraries/TLAS copies: "<<s.shaderLibraries<<" / "<<s.instanceCopies
       <<"\nRetained associations/admitted instances: "<<s.liveOwners<<" / "<<s.hairInstances
       <<"\nGeometry/BLAS/scratch bytes: "<<s.geometryBytes<<" / "<<s.hairBlasBytes<<" / "<<s.hairScratchBytes
       <<"\nRejected/declined while Off: "<<s.rejected<<" / "<<s.declinedWhileOff
       <<"\nLast build/traced instance age (ms; UINT64_MAX = unknown): "<<s.lastBuildAgeMs<<" / "<<s.lastHairAgeMs
       <<"\nTracked lists/limit/capacity misses: "<<s.trackedLists<<" / "<<s.listLimit<<" / "<<s.listCapacityMisses
       <<"\nPrebuild cache hits/driver queries: "<<s.prebuildCacheHits<<" / "<<s.prebuildDriverQueries
       <<"\nFence queries: "<<s.fenceDriverQueries<<"\nScoped reuse/full validations: "<<s.inputReuseHits<<" / "<<s.inputReuseMisses
       <<"\nLeases recording/recorded/awaiting GPU/reusable/unsafe: "<<s.leasesRecording<<" / "<<s.leasesRecorded
       <<" / "<<s.leasesPending<<" / "<<s.leasesAvailable<<" / "<<s.leasesUnsafe
       <<"\nPool allocations/reuses/releases: "<<s.poolAllocations<<" / "<<s.poolReturns<<" / "<<s.poolReleases
       <<"\nFull rebuilds/evictions: "<<s.fullRebuilds<<" / "<<s.evictions
       <<"\nProcess VRAM/budget bytes: "<<s.vramUsage<<" / "<<s.vramBudget<<"; available: "<<YesNo(s.memoryKnown)
       <<"\nHook elapsed since launch (us, including waits; excludes builder gate): "<<s.hookMicroseconds;
    if(s.recentHookTimeKnown)out<<"\nRecent hook elapsed (ms/s, including waits): "<<s.recentHookMsPerSecond;
    out<<"\nDetailed timings enabled: "<<YesNo(s.cpuProfileEnabled)<<"; window ready: "<<YesNo(s.cpuProfileKnown)<<"\n";
    if(s.cpuProfileEnabled&&s.cpuProfileKnown)for(size_t i=0;i<witcher_dots::cpu_profile::kCount;++i)
        out<<witcher_dots::cpu_profile::kLabels[i]<<": "<<s.cpuMsPerSecond[i]<<" ms/s; "<<s.cpuCallsPerSecond[i]
           <<" calls/s; "<<(s.cpuCallsPerSecond[i]>0?s.cpuMsPerSecond[i]*1000/s.cpuCallsPerSecond[i]:0)<<" us/call\n";
    out<<"Timings include waits; nested rows overlap, do not sum. Not GPU time or CPU utilization.\n"
       <<"Original DOTS fallback: dashdogy / Michael Robles (MIT). Addon port: mavismmg.\n";
    return out.str();
}
inline void Draw(const witcher_dots::Snapshot& s,const char* activity,const char* stage) {
    ImGui::Text("PT Hair Unlock %s",kVersion);
    ImGui::TextColored(Tracing(activity)?ImVec4(0.35f,1.0f,0.55f,1.0f):ImVec4(1.0f,0.75f,0.25f,1.0f),"%s",activity);
    ImGui::Text("Game Path Traced Hair: %s",s.gameHairTraced?"On":"Off / not active");
    if(ShowReason(s,activity))ImGui::TextWrapped("Status detail: %s",s.reason);
    ImGui::TextWrapped("Enable Path Tracing, DLSS Ray Reconstruction, NVIDIA HairWorks and Path Traced Hair in the game's graphics settings.");
    ImGui::TextWrapped("Experimental RTX 40 triangle fallback, not native LSS hardware. Restart the game before updating or removing the addon.");
    ImGui::Separator();
    if(ImGui::CollapsingHeader("Support / Diagnostics")) {
        if(ImGui::Button("Copy diagnostics"))ImGui::SetClipboardText(Diagnostics(s,activity,stage).c_str());
        ImGui::TextWrapped("Review diagnostics and ReShade.log before sharing. Remove the addon if the renderer becomes unstable.");
        ImGui::SeparatorText("Compatibility");
        ImGui::Text("Witcher 3 process: %s; fallback requested: %s",YesNo(s.applicable),YesNo(s.requested));
        ImGui::Text("Backend: %s; converter ready: %s",stage,YesNo(s.shaderReady));
        ImGui::Text("GPU PCI ID: 0x%04X",s.deviceId);
        if(s.driverVersion)ImGui::Text("NVIDIA driver: %u.%02u",s.driverVersion/100,s.driverVersion%100);
        if(s.reason[0]&&!ShowReason(s,activity))ImGui::TextWrapped("Last detail: %s",s.reason);
        ImGui::SeparatorText("Converted Hair");
        ImGui::Text("Prebuilds / builds / updates: %llu / %llu / %llu",s.prebuilds,s.builds,s.updates);
        ImGui::Text("Shader libraries / TLAS copies: %llu / %llu",s.shaderLibraries,s.instanceCopies);
        ImGui::Text("Retained associations / admitted instances: %u / %u",s.liveOwners,s.hairInstances);
        ImGui::Text("Geometry / BLAS / scratch: %.1f / %.1f / %.1f MiB",MiB(s.geometryBytes),MiB(s.hairBlasBytes),MiB(s.hairScratchBytes));
        ImGui::Text("Rejected / declined while Off: %llu / %llu",s.rejected,s.declinedWhileOff);
        if(s.lastBuildAgeMs!=UINT64_MAX)ImGui::Text("Last build: %llu ms ago",s.lastBuildAgeMs);
        if(s.lastHairAgeMs!=UINT64_MAX)ImGui::Text("Last traced instance: %llu ms ago",s.lastHairAgeMs);
        ImGui::SeparatorText("Resource Tracking");
        ImGui::Text("Command lists: %u / %u; capacity misses: %llu",s.trackedLists,s.listLimit,s.listCapacityMisses);
        ImGui::Text("BLAS cache hits / queries: %llu / %llu",s.prebuildCacheHits,s.prebuildDriverQueries);
        ImGui::Text("Fence queries: %llu; scoped reuse / validations: %llu / %llu",s.fenceDriverQueries,s.inputReuseHits,s.inputReuseMisses);
        ImGui::TextWrapped("Leases recording / recorded / awaiting GPU / reusable / unsafe: %u / %u / %u / %u / %u",s.leasesRecording,s.leasesRecorded,s.leasesPending,s.leasesAvailable,s.leasesUnsafe);
        ImGui::Text("Pool allocations / reuses / releases: %llu / %llu / %llu",s.poolAllocations,s.poolReturns,s.poolReleases);
        ImGui::Text("Full rebuilds / evictions: %llu / %llu",s.fullRebuilds,s.evictions);
        if(s.memoryKnown)ImGui::Text("Process VRAM / budget: %.0f / %.0f MiB",MiB(s.vramUsage),MiB(s.vramBudget));
        if(ImGui::TreeNode("CPU timings (advanced)")) {
            bool profiling=witcher_dots::cpu_profile::Enabled();
            if(ImGui::Checkbox("Enable detailed CPU timings (this session only)",&profiling))witcher_dots::cpu_profile::SetEnabled(profiling);
            ImGui::TextWrapped("Elapsed time includes waits across threads. Nested rows overlap: do not add them. Not GPU time or CPU utilization. Builder gate is outside the hook totals; reference rows count COM blocks, not individual AddRef/Release calls.");
            if(!profiling)ImGui::TextWrapped("Timers are off. Enable only for diagnosis; disable for FPS comparisons.");
            else if(!s.cpuProfileKnown)ImGui::TextUnformatted("Collecting: keep this panel open for at least one second.");
            else for(size_t i=0;i<witcher_dots::cpu_profile::kCount;++i)
                ImGui::Text("%s: %.2f ms/s; %.0f calls/s; %.2f us/call",witcher_dots::cpu_profile::kLabels[i],s.cpuMsPerSecond[i],s.cpuCallsPerSecond[i],s.cpuCallsPerSecond[i]>0?s.cpuMsPerSecond[i]*1000/s.cpuCallsPerSecond[i]:0);
            if(s.recentHookTimeKnown)ImGui::Text("Recent hook elapsed: %.2f ms/s (includes waits)",s.recentHookMsPerSecond);
            ImGui::Text("Hook elapsed since launch: %.3f ms",double(s.hookMicroseconds)/1000);
            ImGui::TreePop();
        }
    }
    ImGui::Separator();
    ImGui::TextWrapped("DOTS fallback: dashdogy / Michael Robles (MIT). Addon port: mavismmg.");
    if(ImGui::CollapsingHeader("About / Compatibility Notes")) {
        ImGui::TextWrapped("Supports the exact Steam 5.00c DX12 5.0.0.1044392 executable, NVIDIA Ada / RTX 40, a supported driver and verified shader/runtime hashes. Other builds fail closed.");
        ImGui::TextWrapped("Do not combine this addon with RTXMFG's Witcher DOTS backend or another PT Hair unlocker. This addon does not require MFG Unlock.");
        ImGui::TextWrapped("Original DOTS renderer/geometry/shader implementation: dashdogy / Michael Robles, RTX40MFG-Unlock, MIT, commit 49dc07ba00568c4337d7efc79a4b9e6470289d15. Guarded-read reference: upstream v1.4.1. Third-party license notices are included in the download.");
    }
}
}
