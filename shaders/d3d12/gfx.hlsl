
cbuffer FrameConstants : register(b0) {
    float4x4 viewProjection;
    float2 viewportSize;
    float timeSeconds;
    float motionScale;
    uint frameFlags;
    uint3 framePadding;
};

struct VertexInput {
    float4 minimumAndWidth : INSTANCE_MINIMUM_WIDTH;
    float4 maximumAndHue : INSTANCE_MAXIMUM_HUE;
    float4 color : INSTANCE_COLOR;
    uint effects : INSTANCE_EFFECTS;
    uint3 reserved : INSTANCE_RESERVED;
    uint vertexId : SV_VertexID;
};

struct PixelInput {
    float4 position : SV_Position;
    nointerpolation float4 color : COLOR;
    noperspective float edgeAcross : EDGE_ACROSS;
    noperspective float edgeAlong : EDGE_ALONG;
    nointerpolation float segmentLength : SEGMENT_LENGTH;
    nointerpolation float halfWidth : HALF_WIDTH;
    nointerpolation float hueOffset : HUE_OFFSET;
    nointerpolation uint effects : EFFECTS;
    nointerpolation float validEdge : VALID_EDGE;
    nointerpolation uint primitiveKind : PRIMITIVE_KIND;
};

static const int2 edges[12] = {
    int2(0, 1), int2(2, 3), int2(0, 2), int2(1, 3),
    int2(4, 5), int2(6, 7), int2(4, 6), int2(5, 7),
    int2(0, 4), int2(1, 5), int2(2, 6), int2(3, 7)
};
static const uint edgeFaces[12] = {
    17u, 33u, 5u, 9u,
    18u, 34u, 6u, 10u,
    20u, 24u, 36u, 40u
};
static const float2 quad[4] = {
    float2(0.0, -1.0), float2(1.0, -1.0),
    float2(1.0, 1.0), float2(0.0, 1.0)
};
static const int4 faces[6] = {
    int4(0, 2, 3, 1), int4(4, 5, 7, 6),
    int4(0, 4, 6, 2), int4(1, 3, 7, 5),
    int4(0, 1, 5, 4), int4(2, 6, 7, 3)
};

float3 corner(int code) {
    return float3(float(code & 1), float((code >> 1) & 1), float((code >> 2) & 1));
}

float unpackMotionComponent(uint value) {
    uint bits = ((value >> 20u) << 31u)
        | (((value >> 12u) & 0xffu) << 23u)
        | ((value & 0xfffu) << 11u);
    return asfloat(bits);
}

float3 decodeMotionDelta(VertexInput input) {
    float3 result = float3(0.0, 0.0, 0.0);
    if ((input.effects & 4u) == 0u) {
        result = float3(
            asfloat(input.reserved.x),
            asfloat(input.reserved.y),
            asfloat(input.reserved.z));
    } else {
        uint low = input.reserved.y;
        uint high = input.reserved.z;
        result = float3(
            unpackMotionComponent(low & 0x1fffffu),
            unpackMotionComponent((low >> 21u) | ((high & 0x3ffu) << 11u)),
            unpackMotionComponent((high >> 10u) & 0x1fffffu));
    }
    return result;
}

float planeDistance(float4 clipPoint, int planeIndex, bool zeroToOne) {
    if (planeIndex == 0) return clipPoint.w + clipPoint.x;
    if (planeIndex == 1) return clipPoint.w - clipPoint.x;
    if (planeIndex == 2) return clipPoint.w + clipPoint.y;
    if (planeIndex == 3) return clipPoint.w - clipPoint.y;
    if (planeIndex == 4) return zeroToOne ? clipPoint.z : clipPoint.w + clipPoint.z;
    if (planeIndex == 5) return clipPoint.w - clipPoint.z;
    return clipPoint.w - 0.0001;
}

uint clipAgainstPlane(
    inout float4 startClip,
    inout float4 finishClip,
    int plane,
    bool zeroToOne) {
    float startDistance = planeDistance(startClip, plane, zeroToOne);
    float finishDistance = planeDistance(finishClip, plane, zeroToOne);
    bool startInside = startDistance >= 0.0;
    bool finishInside = finishDistance >= 0.0;
    uint accepted = startInside || finishInside ? 1u : 0u;
    if (accepted != 0u && startInside != finishInside) {
        float denominator = startDistance - finishDistance;
        if (abs(denominator) <= 1e-20) {
            accepted = 0u;
        } else {
            float amount = clamp(startDistance / denominator, 0.0, 1.0);
            float4 clipped = lerp(startClip, finishClip, amount);
            if (startInside) {
                finishClip = clipped;
            } else {
                startClip = clipped;
            }
        }
    }
    return accepted;
}

uint clipSegment(inout float4 startClip, inout float4 finishClip, bool zeroToOne) {
    uint accepted = clipAgainstPlane(startClip, finishClip, 6, zeroToOne);
    [unroll]
    for (int plane = 0; plane < 6; ++plane) {
        if (accepted != 0u) {
            accepted = clipAgainstPlane(startClip, finishClip, plane, zeroToOne);
        }
    }
    return accepted != 0u && startClip.w >= 0.0001 && finishClip.w >= 0.0001
        ? 1u : 0u;
}

PixelInput vertexMain(VertexInput input) {
    PixelInput output;
    float3 motionOffset = (input.effects & 2u) != 0u
        ? decodeMotionDelta(input) * motionScale
        : 0.0;
    float3 minimumValue = input.minimumAndWidth.xyz + motionOffset;
    float3 maximumValue = input.maximumAndHue.xyz + motionOffset;
    bool zeroToOne = (frameFlags & 1u) == 0u;
    output.color = input.color;
    output.edgeAcross = 0.0;
    output.edgeAlong = 0.0;
    output.segmentLength = 0.0;
    output.halfWidth = max(input.minimumAndWidth.w, 0.5) * 0.5;
    output.hueOffset = input.maximumAndHue.w;
    output.effects = input.effects;
    output.validEdge = 0.0;
    output.primitiveKind = input.vertexId >= 48u ? 1u : 0u;
    uint faceMask = (input.effects & 64u) != 0u
        ? (input.effects >> 16u) & 63u
        : 63u;

    if (output.primitiveKind != 0u) {
        uint faceVertex = input.vertexId - 48u;
        uint face = faceVertex / 4u;
        uint localVertex = faceVertex % 4u;
        int cornerCode = faces[face][localVertex];
        float3 position = lerp(minimumValue, maximumValue, corner(cornerCode));
        float4 clipPosition = mul(viewProjection, float4(position, 1.0));
        output.validEdge = (input.effects & 16u) != 0u
            && (faceMask & (1u << face)) != 0u
            && all(isfinite(clipPosition)) ? 1.0 : 0.0;
        if (output.validEdge < 0.5) {
            output.position = float4(2.0, 2.0, 2.0, 1.0);
            return output;
        }
        if (!zeroToOne) {
            clipPosition.z = clipPosition.z * 0.5 + clipPosition.w * 0.5;
        }
        output.position = clipPosition;
        return output;
    }

    int edgeIndex = input.vertexId / 4;
    float2 vertex = quad[input.vertexId % 4];
    float3 start = lerp(minimumValue, maximumValue, corner(edges[edgeIndex].x));
    float3 finish = lerp(minimumValue, maximumValue, corner(edges[edgeIndex].y));
    float4 startClip = mul(viewProjection, float4(start, 1.0));
    float4 finishClip = mul(viewProjection, float4(finish, 1.0));
    output.validEdge = (input.effects & 32u) == 0u
        && (faceMask & edgeFaces[edgeIndex]) != 0u
        && clipSegment(startClip, finishClip, zeroToOne) != 0u ? 1.0 : 0.0;

    float fringe = 1.25;
    float expandedWidth = output.halfWidth + fringe;
    if (output.validEdge < 0.5) {
        output.position = float4(2.0, 2.0, 2.0, 1.0);
        return output;
    }

    float2 startNdc = startClip.xy / startClip.w;
    float2 finishNdc = finishClip.xy / finishClip.w;
    float2 directionPixels = (finishNdc - startNdc) * viewportSize * 0.5;
    output.segmentLength = length(directionPixels);
    float2 direction = output.segmentLength > 0.0001
        ? directionPixels / output.segmentLength
        : float2(1.0, 0.0);
    float2 normalPixels = float2(-direction.y, direction.x);
    bool finishVertex = vertex.x > 0.5;
    output.edgeAlong = finishVertex ? output.segmentLength + fringe : -fringe;
    output.edgeAcross = vertex.y * expandedWidth;
    float capOffset = finishVertex ? fringe : -fringe;
    float2 offsetPixels = direction * capOffset
        + normalPixels * expandedWidth * vertex.y;
    float2 offsetNdc = offsetPixels * 2.0 / viewportSize;
    float4 endpoint = finishVertex ? finishClip : startClip;
    endpoint.xy += offsetNdc * endpoint.w;
    if (!zeroToOne) {
        endpoint.z = endpoint.z * 0.5 + endpoint.w * 0.5;
    }
    output.position = endpoint;
    return output;
}

float3 hue(float value) {
    float3 shifted = abs(frac(value + float3(0.0, 0.6666667, 0.3333333)) * 6.0 - 3.0);
    return saturate(shifted - 1.0);
}

float4 pixelMain(PixelInput input) : SV_Target {
    clip(input.validEdge - 0.5);
    float4 color = input.color;
    if ((input.effects & 1u) != 0u) {
        color.rgb *= hue(frac(timeSeconds * 0.08 + input.hueOffset));
    }
    if (input.primitiveKind != 0u) {
        color.a *= float((input.effects >> 8u) & 255u) / 255.0;
        clip(color.a - 0.001);
        return float4(color.rgb * color.a, color.a);
    }
    float2 centered = float2(
        input.edgeAlong - input.segmentLength * 0.5,
        input.edgeAcross);
    float2 outside = abs(centered) - float2(input.segmentLength * 0.5, input.halfWidth);
    float distanceToEdge = length(max(outside, 0.0))
        + min(max(outside.x, outside.y), 0.0);
    float antiAlias = max(fwidth(distanceToEdge), 0.75);
    float coverage = 1.0 - smoothstep(-antiAlias, antiAlias, distanceToEdge);
    color.a *= coverage;
    clip(color.a - 0.001);
    return float4(color.rgb * color.a, color.a);
}
