//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "OpenGL_Shaders.h"

namespace zv
{

const char* commonFragmentLibrary = R"(
)";

const char* defaultVertexShader_glsl_130 =
    "in vec2 Position;\n"
    "in vec2 UV;\n"
    "out vec2 Frag_UV;\n"
    "void main()\n"
    "{\n"
    "    Frag_UV = UV;\n"
    "    gl_Position = vec4(Position.xy, 0, 1);\n"
    "}\n";
    
const char* defaultFragmentShader_glsl_130 =
    "uniform sampler2D Texture;\n"
    "in vec2 Frag_UV;\n"
    "out vec4 Out_Color;\n"
    "void main()\n"
    "{\n"
    "    Out_Color = texture(Texture, Frag_UV.st);\n"
    "}\n";

const char* fragmentShader_Normal_glsl_130 = R"(
    uniform sampler2D Texture;
    in vec2 Frag_UV;
    out vec4 Out_Color;
    void main()
    {
        vec4 srgb = texture(Texture, Frag_UV.st);
        Out_Color = srgb;
    }
)";

} // zv
