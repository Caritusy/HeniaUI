
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
    float3 motionDelta : INSTANCE_MOTION_DELTA;
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
};

static const int2 edges[12] = {
    int2(0, 1), int2(2, 3), int2(0, 2), int2(1, 3),
    int2(4, 5), int2(6, 7), int2(4, 6), int2(5, 7),
    int2(0, 4), int2(1, 5), int2(2, 6), int2(3, 7)
};
static const float2 quad[6] = {
    float2(0.0, -1.0), float2(1.0, -1.0), float2(1.0, 1.0),
    float2(0.0, -1.0), float2(1.0, 1.0), float2(0.0, 1.0)
};

float3 corner(int code) {
    return float3(float(code & 1), float((code >> 1) & 1), float((code >> 2) & 1));
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
    int edgeIndex = input.vertexId / 6;
    float2 vertex = quad[input.vertexId % 6];
    float3 motionOffset = (input.effects & 2u) != 0u
        ? input.motionDelta * motionScale
        : 0.0;
    float3 minimumValue = input.minimumAndWidth.xyz + motionOffset;
    float3 maximumValue = input.maximumAndHue.xyz + motionOffset;
    float3 start = lerp(minimumValue, maximumValue, corner(edges[edgeIndex].x));
    float3 finish = lerp(minimumValue, maximumValue, corner(edges[edgeIndex].y));
    float4 startClip = mul(viewProjection, float4(start, 1.0));
    float4 finishClip = mul(viewProjection, float4(finish, 1.0));
    bool zeroToOne = (frameFlags & 1u) == 0u;
    output.validEdge = clipSegment(startClip, finishClip, zeroToOne) != 0u ? 1.0 : 0.0;

    output.halfWidth = max(input.minimumAndWidth.w, 0.5) * 0.5;
    float fringe = 1.25;
    float expandedWidth = output.halfWidth + fringe;
    output.color = input.color;
    output.edgeAcross = 0.0;
    output.edgeAlong = 0.0;
    output.segmentLength = 0.0;
    output.hueOffset = input.maximumAndHue.w;
    output.effects = input.effects;
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
    float2 centered = float2(
        input.edgeAlong - input.segmentLength * 0.5,
        input.edgeAcross);
    float2 outside = abs(centered) - float2(input.segmentLength * 0.5, input.halfWidth);
    float distanceToEdge = length(max(outside, 0.0))
        + min(max(outside.x, outside.y), 0.0);
    float antiAlias = max(fwidth(distanceToEdge), 0.75);
    float coverage = 1.0 - smoothstep(-antiAlias, antiAlias, distanceToEdge);
    float4 color = input.color;
    if ((input.effects & 1u) != 0u) {
        color.rgb *= hue(frac(timeSeconds * 0.08 + input.hueOffset));
    }
    color.a *= coverage;
    clip(color.a - 0.001);
    return float4(color.rgb * color.a, color.a);
}
