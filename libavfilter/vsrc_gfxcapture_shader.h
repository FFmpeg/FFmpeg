/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVFILTER_VSRC_GFXCAPTURE_SHADER_H
#define AVFILTER_VSRC_GFXCAPTURE_SHADER_H

#define HLSL_SHADER(shader) #shader

static const char render_shader_src[] = HLSL_SHADER(
    struct VSOut {
        float4 pos : SV_Position;
        float2 uv : TEXCOORD0;
    };

    cbuffer cb : register(b0) {
        float2 tS;
        float2 dS;
        float2 uvMin;
        float2 uvMax;
        uint to_unpremult;
        uint to_srgb;
        uint2 pad;
    };

    Texture2D t0 : register(t0);
    SamplerState s0 : register(s0);

    VSOut main_vs(uint id : SV_VertexID) {
        VSOut o;
        o.pos = float4(id == 2 ? 3.0 : -1.0, id == 1 ? 3.0 : -1.0, 0, 1);
        o.uv = lerp(uvMin, uvMax, float2((o.pos.x + 1) * 0.5, 1 - (o.pos.y + 1) * 0.5));
        return o;
    }

    float4 cubic(float v) {
        float4 n = float4(1.0, 2.0, 3.0, 4.0) - v;
        float4 s = n * n * n;
        float  x = s.x;
        float  y = s.y - 4.0 * s.x;
        float  z = s.z - 4.0 * s.y + 6.0 * s.x;
        float  w = 6.0 - x - y - z;
        return float4(x, y, z, w) * (1.0 / 6.0);
    }

    float4 texBicubic(Texture2D t, SamplerState ss, float2 uv) {
        float2 itS = 1.0 / tS;

        float2 tc = uv * tS - 0.5;
        float2 fxy = frac(tc);
        tc -= fxy;

        float4 xc = cubic(fxy.x);
        float4 yc = cubic(fxy.y);

        float4 s = float4(xc.xz + xc.yw, yc.xz + yc.yw);
        float4 o = tc.xxyy + (float2(-0.5, 1.5)).xyxy + float4(xc.yw, yc.yw) / s;
        o *= itS.xxyy;

        float4 s0 = t.Sample(ss, o.xz);
        float4 s1 = t.Sample(ss, o.yz);
        float4 s2 = t.Sample(ss, o.xw);
        float4 s3 = t.Sample(ss, o.yw);

        float sx = s.x / (s.x + s.y);
        float sy = s.z / (s.z + s.w);

        return lerp(lerp(s3, s2, sx), lerp(s1, s0, sx), sy);
    }

    float4 unpremultiply(float4 c) {
        if (c.a < 1e-6)
            return float4(0.0, 0.0, 0.0, 0.0);
        return float4(c.rgb / c.a, c.a);
    }

    float4 premultiply(float4 c) {
        return float4(c.rgb * c.a, c.a);
    }

    float3 linear_to_srgb(float3 c) {
        c = max(c, 0.0);
        float3 lo = 12.92 * c;
        float3 hi = 1.055 * pow(c, 1.0 / 2.4) - 0.055;
        return saturate(lerp(hi, lo, step(c, 0.0031308)));
    }

    float4 fix_color(float4 c) {
        if (to_unpremult || to_srgb)
            c = unpremultiply(c);
        if (to_srgb) {
            c.rgb = linear_to_srgb(c.rgb);
            if (!to_unpremult)
                c = premultiply(c);
        }
        return c;
    }

    float4 main_ps(VSOut i) : SV_Target {
        return fix_color(t0.Sample(s0, i.uv));
    }

    float4 main_ps_bicubic(VSOut i) : SV_Target {
        if (all(tS == dS))
            return main_ps(i);
        return fix_color(texBicubic(t0, s0, i.uv));
    }
);

#undef HLSL_SHADER

#endif /* AVFILTER_VSRC_GFXCAPTURE_SHADER_H */
