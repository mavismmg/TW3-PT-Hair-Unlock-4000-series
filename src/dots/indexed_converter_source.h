#pragma once
#include "converter_source.h"
#include <string>
namespace witcher_dots {
// The approved HLSL/string stays intact. Change only stores/layout in this
// experimental variant; all geometry math and guards remain verbatim.
inline std::string IndexedConverterSource() {
    std::string source(kConverterHlsl);
    const auto replace=[&](std::string_view before,std::string_view after) {
        const auto at=source.find(before);
        if(at==std::string::npos||source.find(before,at+before.size())!=std::string::npos)return false;
        source.replace(at,before.size(),after);return true;
    };
    if(!replace("j < 12","j < 8")||!replace("OutputVertexCount / 12","OutputVertexCount / 8")
        ||!replace("segment * 12","segment * 8")||!replace("face * 6","face * 4")
        ||!replace("DotsVertices[f+3] = A;","DotsVertices[f+3] = D;")
        ||!replace("        DotsVertices[f+4] = D;\n        DotsVertices[f+5] = B;\n",""))return {};
    return source;
}
}
