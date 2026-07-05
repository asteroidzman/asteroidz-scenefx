// Applies the output color transform (e.g. sRGB -> BT.2020 + PQ for HDR
// outputs) as a final fullscreen pass, via a baked 3D LUT indexed by the
// electrical sRGB frame content.
#extension GL_OES_texture_3D : require

#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

varying vec2 v_texcoord;
uniform sampler2D tex;
uniform mediump sampler3D lut;
uniform float lut_scale;
uniform float lut_offset;

void main() {
    vec3 color = texture2D(tex, v_texcoord).rgb;
    vec3 mapped = texture3D(lut, color * lut_scale + lut_offset).rgb;
    gl_FragColor = vec4(mapped, 1.0);
}
