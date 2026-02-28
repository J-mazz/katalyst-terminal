#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragFg;
layout(location = 2) in vec4 fragBg;

layout(set = 0, binding = 0) uniform sampler2D glyphAtlas;

layout(location = 0) out vec4 outColor;

void main() {
  float alpha = texture(glyphAtlas, fragUV).r;
  vec3 rgb = mix(fragBg.rgb, fragFg.rgb, alpha);
  float outAlpha = max(fragBg.a, alpha * fragFg.a);
  outColor = vec4(rgb, outAlpha);
}
