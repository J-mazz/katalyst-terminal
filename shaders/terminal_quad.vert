#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inInstancePos;
layout(location = 2) in vec2 inInstanceSize;
layout(location = 3) in vec4 inInstanceUV;
layout(location = 4) in vec4 inFg;
layout(location = 5) in vec4 inBg;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragFg;
layout(location = 2) out vec4 fragBg;

layout(push_constant) uniform PushConstants {
  vec2 screenSize;
} pc;

void main() {
  vec2 pixel = inInstancePos + inPos * inInstanceSize;
  vec2 ndc = vec2((pixel.x / pc.screenSize.x) * 2.0 - 1.0,
                  (pixel.y / pc.screenSize.y) * 2.0 - 1.0);
  gl_Position = vec4(ndc, 0.0, 1.0);

  fragUV = mix(inInstanceUV.xy, inInstanceUV.zw, inPos);
  fragFg = inFg;
  fragBg = inBg;
}
