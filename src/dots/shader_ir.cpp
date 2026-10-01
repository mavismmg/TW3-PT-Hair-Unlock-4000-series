#include "shaders.h"
#include "rounded_normal_ir.h"
#include <algorithm>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
namespace witcher_dots {
namespace {
using RX=std::regex;
RX Pattern(const std::string& p) { return RX(p,std::regex_constants::ECMAScript); }
struct Located { size_t at{}, length{}; };
std::vector<Located> Lines(const std::string& s,const RX& pattern) {
    std::vector<Located> matches;
    size_t at=0;
    while(at<s.size()) {
        const auto end=s.find('\n',at);
        const auto n=(end==std::string::npos?s.size():end)-at;
        const auto line=s.substr(at,n);std::smatch m;
        if(std::regex_search(line,m,pattern)) matches.push_back({at+static_cast<size_t>(m.position()),static_cast<size_t>(m.length())});
        if(end==std::string::npos)break;at=end+1;
    }
    return matches;
}
std::string One(std::string s,const std::string& pattern,std::string_view replacement) {
    const auto span=pattern.find("[\\s\\S]*?");
    auto matches=Lines(s,Pattern(pattern.substr(0,span)));
    if(matches.size()!=1)throw std::runtime_error("shader profile marker mismatch: "+pattern);
    auto m=matches.front();
    if(span!=std::string::npos) {
        const auto tail=pattern.find("(?=",span);
        if(tail==std::string::npos||pattern.back()!=')')throw std::runtime_error("invalid shader range");
        const auto ends=Lines(s,Pattern(pattern.substr(tail+3,pattern.size()-tail-4)));
        if(ends.size()!=1||ends[0].at<=m.at)throw std::runtime_error("shader range mismatch");
        m.length=ends[0].at-m.at;
    }
    s.replace(m.at,m.length,replacement);
    return s;
}
std::string Match(const std::string& s,const std::string& pattern) {
    const auto matches=Lines(s,Pattern(pattern));
    if(matches.size()!=1)throw std::runtime_error("missing or ambiguous shader profile marker");
    return s.substr(matches[0].at,matches[0].length);
}
void ReplaceLiteral(std::string& s,std::string_view from,std::string_view to) {
    size_t at=0;
    while ((at=s.find(from,at))!=std::string::npos) { s.replace(at,from.size(),to);at+=to.size(); }
}
std::string Named(std::string s) {
    std::set<std::string> blocks{"0"};
    RX labels(R"(; <label>:(\d+))");
    for(auto it=std::sregex_iterator(s.begin(),s.end(),labels);it!=std::sregex_iterator();++it) blocks.insert((*it)[1]);
    RX refs(R"(%(\d+)\b)");
    std::string out;size_t pos=0;
    for(auto it=std::sregex_iterator(s.begin(),s.end(),refs);it!=std::sregex_iterator();++it) {
        const auto m=*it;out.append(s,pos,static_cast<size_t>(m.position())-pos);
        out+=(blocks.contains(m[1])?"%bb":"%r")+m[1].str();pos=static_cast<size_t>(m.position()+m.length());
    }
    out.append(s,pos,std::string::npos);
    std::istringstream lines(out);std::string line,result;
    while(std::getline(lines,line)) {
        line=std::regex_replace(line,Pattern(R"(^; <label>:(\d+)(.*)$)"),"bb$1:$2");
        line=std::regex_replace(line,Pattern(R"(^(define .* \{)$)"),"$1\nbb0:");
        result+=line+"\n";
    }
    return result;
}
void Prune(std::string& s) {
    std::istringstream input(s);std::string line,out;
    const RX symbol(R"((@[^ (]+)\()");
    while(std::getline(input,line)) {
        if (line.starts_with("declare ")) {
            std::smatch m;
            if (std::regex_search(line,m,symbol)) {
                const std::string needle=m[1].str()+"(";
                const auto at=s.find(needle);
                if (at!=std::string::npos&&s.find(needle,at+needle.size())==std::string::npos) continue;
            }
        }
        out+=line+"\n";
    }
    s=std::move(out);
}
std::string Endpoints(bool pre,std::string global) {
    std::string code=R"(  %dots.ptr = getelementptr inbounds [32 x %dx.types.Handle], [32 x %dx.types.Handle]* GLOBAL, i32 0, i32 INST, !dx.nonuniform !NON
  %dots.load = load %dx.types.Handle, %dx.types.Handle* %dots.ptr, align 4, !noalias !ALIAS
  %dots.handle0 = call %dx.types.Handle @dx.op.createHandleForLib.dx.types.Handle(i32 160, %dx.types.Handle %dots.load)
  %dots.handle = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %dots.handle0, %dx.types.ResourceProperties { i32 12, i32 16 })
  %dots.p0 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %dots.handle, i32 INDEX, i32 0, i8 15, i32 4)
  %dots.next = add i32 INDEX, 1
  %dots.p1 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %dots.handle, i32 %dots.next, i32 0, i8 15, i32 4)
)";
    ReplaceLiteral(code,"GLOBAL",global);ReplaceLiteral(code,"INST",pre?"%r291":"%dots.inst");
    ReplaceLiteral(code,"INDEX",pre?"%r324":"%r21");ReplaceLiteral(code,"NON",pre?"53":"27");
    ReplaceLiteral(code,"ALIAS",pre?"54":"28");
    if(!pre) code="  %dots.inst = call i32 @dx.op.instanceID.i32(i32 141)\n"+code;
    const std::array<int,8> ids=pre?std::array<int,8>{427,431,435,439,443,447,451,455}:std::array<int,8>{121,125,129,133,137,141,145,149};
    for(size_t i=0;i<ids.size();++i)
        code+="  %r"+std::to_string(ids[i])+" = extractvalue %dx.types.ResRet.f32 %dots.p"+(i<4?"0":"1")+", "+std::to_string(i%4)+"\n";
    return code;
}
std::string QueryU(const char* prim,const char* out,const char* tag) {
    std::string code=R"(  %TAG.bx = call float @dx.op.rayQuery_StateVector.f32(i32 194, i32 %r211, i8 0)
  %TAG.by = call float @dx.op.rayQuery_StateVector.f32(i32 194, i32 %r211, i8 1)
  %TAG.sum = fadd float %TAG.bx, %TAG.by
  %TAG.parity = and i32 %PRIM, 1
  %TAG.even = icmp eq i32 %TAG.parity, 0
  %OUT = select i1 %TAG.even, float %TAG.sum, float %TAG.by
)";
    ReplaceLiteral(code,"TAG",tag);ReplaceLiteral(code,"PRIM",prim);ReplaceLiteral(code,"OUT",out);
    return code;
}
void Normals(std::string& s,bool pre) {
    const std::array<int,3> outputs=pre?std::array<int,3>{562,563,564}:std::array<int,3>{249,250,251};
    for(int id:outputs) {
        const auto marker="%r"+std::to_string(id);
        auto phi=Match(s,"^  "+marker+" = phi float.*$");
        ReplaceLiteral(phi,marker+" =","%dots.flat.r"+std::to_string(id)+" =");
        s=One(std::move(s),"^  "+marker+" = phi float.*$",phi);
    }
    const auto marker=pre?"r565":"r252";
    const std::string pattern="^  %"+std::string(marker)+" =.*$";
    const std::string original=Match(s,pattern);
    // Preserve the rounded-normal math, but bind it to this exact shader's
    // endpoints, object-space ray origin/direction and normal outputs. The
    // hotfix removed an extension query and changed the native normal path.
    std::string rounded(pre?kPrepassRoundedNormal:kClosestRoundedNormal), rebased;
    const std::array<std::pair<int,int>,17> mapping{{
        {451,427},{455,431},{459,435},{463,439},{467,443},{471,447},{475,451},{479,455},
        {501,468},{502,469},{503,470},{504,471},{505,472},{506,473},
        {632,562},{633,563},{634,564}}};
    RX registers(R"(r(\d+)\b)");size_t at=0;
    for(auto it=std::sregex_iterator(rounded.begin(),rounded.end(),registers);it!=std::sregex_iterator();++it) {
        const auto m=*it;const int id=std::stoi(m[1]);int target=id;
        if(pre)for(auto [from,to]:mapping)if(id==from)target=to;
        if(!pre&&id>=283&&id<=285)target=id-34;
        rebased.append(rounded,at,static_cast<size_t>(m.position())-at);
        rebased+="r"+std::to_string(target);at=static_cast<size_t>(m.position()+m.length());
    }
    rebased.append(rounded,at,std::string::npos);
    s=One(std::move(s),pattern,rebased+original);
}
}
bool TranslateIr(std::string_view original,ShaderKind kind,std::string& translated,std::string& error) {
    try {
        const bool pre=kind==ShaderKind::Prepass;
        std::string s=Named(std::string(original));
        auto global=Match(s,R"(^(@".*HairVertexBuffers[^"]*") =)");global.resize(global.size()-2);
        if (!pre) {
            s=One(std::move(s),R"(^  %r9 = call i32 @dx.op.primitiveIndex.i32.*$)",R"(  %dots.prim = call i32 @dx.op.primitiveIndex.i32(i32 161)
  %r9 = lshr i32 %dots.prim, 2)");
            s=One(std::move(s),R"(^  %r12 = extractelement.*$)",R"(  %dots.bx = extractelement <2 x float> %r11, i32 0
  %dots.by = extractelement <2 x float> %r11, i32 1
  %dots.sum = fadd float %dots.bx, %dots.by
  %dots.parity = and i32 %dots.prim, 1
  %dots.even = icmp eq i32 %dots.parity, 0
  %r12 = select i1 %dots.even, float %dots.sum, float %dots.by)");
            s=One(std::move(s),R"(^  %r113 = [\s\S]*?(?=^  %r150 =))",Endpoints(false,global));
        } else {
            // The game bridge must establish exclusive ownership of hair mask
            // 0x80 and the 32 descriptor slots before admitting this program.
            s=One(std::move(s),R"(^  %r220 = [\s\S]*?(?=^  br i1 %r231,))",R"(  %dots.id = call i32 @dx.op.rayQuery_StateScalar.i32(i32 208, i32 %r211)
  %dots.triangle = icmp eq i32 %r217, 1
  %dots.slot = icmp ult i32 %dots.id, 32
  %dots.valid = and i1 %dots.triangle, %dots.slot
  %r231 = xor i1 %dots.valid, true
)");
            s=One(std::move(s),R"(^  %r238 = call i32 @dx.op.rayQuery_StateScalar.i32.*$)",
                "  %dots.palpha = call i32 @dx.op.rayQuery_StateScalar.i32(i32 210, i32 %r211)\n  %r238 = lshr i32 %dots.palpha, 2");
            s=One(std::move(s),R"(^  %r239 = call float @dx.op.rayQuery_StateVector.f32.*$)",QueryU("dots.palpha","r239","dots.ualpha"));
            // 5.00c already removed the main extended geometry query.
            s=One(std::move(s),R"(^  %r289 = call float @dx.op.rayQuery_StateVector.f32.*$)",
                "  %dots.pmain = call i32 @dx.op.rayQuery_StateScalar.i32(i32 210, i32 %r211)\n"+QueryU("dots.pmain","r289","dots.umain"));
            s=One(std::move(s),R"(^  %r290 = call i32 @dx.op.rayQuery_StateScalar.i32.*$)",
                "  %r290 = lshr i32 %dots.pmain, 2");
            s=One(std::move(s),R"(^  %r416 = [\s\S]*?(?=^  %r456 =))",Endpoints(true,global));
        }
        Normals(s,pre);Prune(s);
        if(s.find("call i32 @dx.op.bufferUpdateCounter")!=std::string::npos)
            throw std::runtime_error("untranslated NVIDIA extension sequence");
        translated=std::move(s);return true;
    } catch(const std::exception& e) { translated.clear();error=e.what();return false; }
}
}
