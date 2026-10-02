#define ImTextureID ImU64
#include "overlay.h"
#include <deps/imgui/imgui_internal.h>
#include <cassert>
#include <cmath>
#include <cstdio>
int main() {
    using namespace witcher_dots;
    Snapshot s{};s.stage=Stage::Active;s.applicable=s.requested=s.shaderReady=s.gameHairTraced=true;
    s.liveOwners=11;s.hairInstances=5;s.deviceId=0x2783;s.driverVersion=61714;
    const char* active="active (tracing converted hair)";
    assert(hair_overlay::Tracing(active)&&!hair_overlay::Tracing("active (some hair conversions rejected)"));
    strcpy_s(s.reason,"last fallback: historical rejection");
    assert(!hair_overlay::ShowReason(s,active));
    assert(hair_overlay::ShowReason(s,"raster fallback (hair conversion rejected)"));
    s.stage=Stage::Failed;assert(hair_overlay::ShowReason(s,"unavailable"));s.stage=Stage::Active;
    const auto plain=hair_overlay::Diagnostics(s,active,"prepared");
    assert(plain.find("Status: active (tracing converted hair)")!=std::string::npos);
    assert(plain.find("Retained associations/admitted instances: 11 / 5")!=std::string::npos);
    assert(plain.find("dashdogy / Michael Robles")!=std::string::npos);
    assert(plain.find("Tracked queues/limit/capacity misses:")!=std::string::npos);
    assert(plain.find("Pool failures budget/busy/allocation:")!=std::string::npos);
    assert(plain.find("Geometry layout: approved 12 vertices")!=std::string::npos);
    s.indexedGeometry=true;assert(hair_overlay::Diagnostics(s,active,"prepared").find("indexed 8 vertices (experimental)")!=std::string::npos);s.indexedGeometry=false;
    assert(plain.find("VirtualQuery (nested):")==std::string::npos);
    s.cpuProfileEnabled=s.cpuProfileKnown=true;
    s.cpuMsPerSecond[0]=100;s.cpuCallsPerSecond[0]=200;
    const auto detailed=hair_overlay::Diagnostics(s,active,"prepared");
    assert(detailed.find("100 ms/s; 200 calls/s; 500 us/call")!=std::string::npos);
    assert(detailed.find("Builder owner/context gate (separate):")!=std::string::npos);
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.DisplaySize={1280,960};io.DeltaTime=1.f/60;
    io.IniFilename=nullptr;io.LogFilename=nullptr;
    unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);assert(pixels&&width&&height);
    for(float scale:{1.f,1.5f})for(int mode=0;mode<4;++mode)for(int frame=0;frame<2;++frame) {
        io.FontGlobalScale=scale;cpu_profile::SetEnabled(mode==3);
        ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({900,900});
        ImGui::Begin("PT Hair UI test",nullptr,ImGuiWindowFlags_NoSavedSettings);
        auto* window=ImGui::GetCurrentWindow();
        window->StateStorage.SetInt(window->GetID("Support / Diagnostics"),mode>=1);
        window->StateStorage.SetInt(window->GetID("About / Compatibility Notes"),mode>=2);
        window->StateStorage.SetInt(window->GetID("CPU timings (advanced)"),mode==3);
        window->StateStorage.SetInt(window->GetID("GPU timings (advanced)"),mode==3);
        const float start=ImGui::GetCursorPosY();
        hair_overlay::Draw(s,active,"prepared");
        if(mode==0)assert(ImGui::GetCursorPosY()-start<420*scale);
        ImGui::End();ImGui::Render();const auto* draws=ImGui::GetDrawData();assert(draws&&draws->Valid);
        for(const auto* list:draws->CmdLists)for(const auto& vertex:list->VtxBuffer)
            assert(std::isfinite(vertex.pos.x)&&std::isfinite(vertex.pos.y));
    }
    cpu_profile::SetEnabled(false);ImGui::DestroyContext();
    std::puts("PASS: compact/default and expanded UI render at 100%/150%; status/rejection/clipboard diagnostics and timing units consistent");
}
