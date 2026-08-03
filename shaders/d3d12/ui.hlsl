
cbuffer FrameConstants : register(b0) {
    float2 viewportSize;
    float2 logicalToFramebufferScale;
    float2 logicalToFramebufferTranslation;
    float minimumAntialiasWidth;
};

#ifndef HENIA_TEXTURE_FREE
cbuffer TextureConstants : register(b1) {
    uint4 textureAlphaModes0;
    uint4 textureAlphaModes1;
};
#endif

#ifndef HENIA_TEXTURE_FREE
Texture2D textures[8] : register(t0);
SamplerState linearSampler : register(s0);
#endif

struct VertexInput {
    float4 bounds : INSTANCE_BOUNDS;
    float4 uv : INSTANCE_UV;
    float4 color : INSTANCE_COLOR;
    float2 metrics : INSTANCE_METRICS;
    uint4 style : INSTANCE_STYLE;
    uint vertexId : SV_VertexID;
};

struct PixelInput {
    float4 position : SV_Position;
    float2 pixelPosition : PIXEL_POSITION;
    float2 textureUv : TEXTURE_UV;
    float4 tintColor : TINT_COLOR;
    float4 linePoints : LINE_POINTS;
    nointerpolation float4 lineNeighbors : LINE_NEIGHBORS;
    float2 shapeMetrics : SHAPE_METRICS;
    nointerpolation uint textureSlot : TEXTURE_SLOT;
    nointerpolation uint primitiveKind : PRIMITIVE_KIND;
    nointerpolation uint lineCap : LINE_CAP;
    nointerpolation uint lineJoin : LINE_JOIN;
    nointerpolation uint lineFlags : LINE_FLAGS;
    nointerpolation uint shaderParameter : SHADER_PARAMETER;
};

static const float2 corners[6] = {
    float2(0.0, 0.0), float2(1.0, 0.0), float2(1.0, 1.0),
    float2(0.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0)
};

PixelInput vertexMain(VertexInput input) {
    PixelInput output;
    float2 corner = corners[input.vertexId];
    uint kind = input.style.x;
    uint decodedLineJoin = input.style.w & 1;
    uint decodedLineFlags = (input.style.w >> 1) & 3;
    float2 pixel;
    if (kind == 2) {
        float2 start = input.bounds.xy;
        float2 finish = input.bounds.zw;
        float2 segment = finish - start;
        float segmentLength = length(segment);
        float2 direction = segment / segmentLength;
        float2 normal = float2(-direction.y, direction.x);
        float halfWidth = input.metrics.y * 0.5;
        uint joinMode = decodedLineJoin == 0 ? 0 : 2;
        uint startMode = (decodedLineFlags & 1) != 0 ? joinMode : input.style.z;
        uint endMode = (decodedLineFlags & 2) != 0 ? joinMode : input.style.z;
        float startExtension = (((decodedLineFlags & 1) != 0 || startMode != 0)
            ? halfWidth : 0.0) + 2.0;
        float endExtension = (((decodedLineFlags & 2) != 0 || endMode != 0)
            ? halfWidth : 0.0) + 2.0;
        float along = lerp(-startExtension, segmentLength + endExtension, corner.x);
        float across = lerp(-halfWidth - 2.0, halfWidth + 2.0, corner.y);
        pixel = start + direction * along + normal * across;
    } else if (kind == 9 || kind == 14) {
        float extent = input.metrics.y * 3.0 + 2.0;
        float2 offset = kind == 9 ? input.uv.xy : 0.0;
        pixel = lerp(
            input.bounds.xy + offset - extent,
            input.bounds.zw + offset + extent,
            corner);
    } else if (kind == 0 || kind == 5 || kind == 6 || kind == 7
        || kind == 8 || kind == 10 || kind == 12 || kind == 13
        || kind == 15) {
        pixel = lerp(input.bounds.xy - 2.0, input.bounds.zw + 2.0, corner);
    } else {
        pixel = lerp(input.bounds.xy, input.bounds.zw, corner);
    }
    float2 framebufferPixel = pixel * logicalToFramebufferScale
        + logicalToFramebufferTranslation;
    float2 normalized = framebufferPixel / viewportSize;
    output.position = float4(normalized.x * 2.0 - 1.0, 1.0 - normalized.y * 2.0, 0.0, 1.0);
    output.pixelPosition = pixel;
    output.textureUv = lerp(input.uv.xy, input.uv.zw, corner);
    output.tintColor = input.color;
    output.linePoints = kind == 1 ? input.uv : input.bounds;
    output.lineNeighbors = input.uv;
    output.shapeMetrics = input.metrics;
    output.textureSlot = input.style.y;
    output.primitiveKind = kind;
    output.lineCap = input.style.z;
    output.lineJoin = decodedLineJoin;
    output.lineFlags = decodedLineFlags;
    output.shaderParameter = input.style.w;
    return output;
}

float4 sampleTexture(uint slot, float2 uv) {
#ifdef HENIA_TEXTURE_FREE
    return 1.0;
#else
    float4 sampled = float4(1.0, 1.0, 1.0, 1.0);
    if (slot == 0) sampled = textures[0].SampleLevel(linearSampler, uv, 0.0);
    else if (slot == 1) sampled = textures[1].SampleLevel(linearSampler, uv, 0.0);
    else if (slot == 2) sampled = textures[2].SampleLevel(linearSampler, uv, 0.0);
    else if (slot == 3) sampled = textures[3].SampleLevel(linearSampler, uv, 0.0);
    else if (slot == 4) sampled = textures[4].SampleLevel(linearSampler, uv, 0.0);
    else if (slot == 5) sampled = textures[5].SampleLevel(linearSampler, uv, 0.0);
    else if (slot == 6) sampled = textures[6].SampleLevel(linearSampler, uv, 0.0);
    else sampled = textures[7].SampleLevel(linearSampler, uv, 0.0);
    return sampled;
#endif
}

float4 straightTextureSample(uint slot, float2 uv) {
    float4 sampled = sampleTexture(slot, uv);
#ifndef HENIA_TEXTURE_FREE
    uint alphaMode = slot < 4
        ? textureAlphaModes0[slot]
        : textureAlphaModes1[slot - 4];
    if (alphaMode == 2) {
        sampled.rgb = sampled.a > 0.00001 ? sampled.rgb / sampled.a : 0.0;
    } else if (alphaMode == 3) {
        sampled.a = 1.0;
    }
#endif
    return sampled;
}

float roundedBoxDistance(float2 positionValue, float2 halfSize, float radius) {
    float2 q = abs(positionValue) - halfSize + radius;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

float variableRoundedBoxDistance(float2 positionValue, float2 halfSize, float4 radii) {
    float radius = positionValue.x < 0.0
        ? (positionValue.y < 0.0 ? radii.x : radii.w)
        : (positionValue.y < 0.0 ? radii.y : radii.z);
    radius = min(max(radius, 0.0), min(halfSize.x, halfSize.y));
    float2 q = abs(positionValue) - halfSize + radius;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

float ellipseDistance(float2 positionValue, float2 halfSize) {
    float2 safeHalfSize = max(halfSize, 0.001);
    return (length(positionValue / safeHalfSize) - 1.0)
        * min(safeHalfSize.x, safeHalfSize.y);
}

float arcDistance(
    float2 positionValue,
    float2 halfSize,
    float start,
    float sweep,
    float thickness) {
    const float tau = 6.28318530718;
    float halfWidth = thickness * 0.5;
    float2 pathRadii = max(halfSize - halfWidth, 0.001);
    float angle = atan2(positionValue.y / pathRadii.y, positionValue.x / pathRadii.x);
    float direction = sweep < 0.0 ? -1.0 : 1.0;
    float relative = fmod(direction * (angle - start), tau);
    if (relative < 0.0) relative += tau;
    float span = abs(sweep);
    float radial = abs(ellipseDistance(positionValue, pathRadii)) - halfWidth;
    if (span >= tau - 0.0001 || relative <= span) return radial;
    float2 startPoint = float2(cos(start), sin(start)) * pathRadii;
    float finish = start + sweep;
    float2 finishPoint = float2(cos(finish), sin(finish)) * pathRadii;
    return min(length(positionValue - startPoint), length(positionValue - finishPoint)) - halfWidth;
}

float ninePatchCoordinate(float value, float destinationBorder, float sourceBorder) {
    if (value < destinationBorder) {
        return value / max(destinationBorder, 0.0001) * sourceBorder;
    }
    if (value > 1.0 - destinationBorder) {
        return 1.0 - (1.0 - value) / max(destinationBorder, 0.0001) * sourceBorder;
    }
    return sourceBorder
        + (value - destinationBorder) / max(1.0 - destinationBorder * 2.0, 0.0001)
            * (1.0 - sourceBorder * 2.0);
}

float boxDistance(float2 positionValue, float2 halfSize) {
    float2 q = abs(positionValue) - halfSize;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
}



float cappedSegmentDistance(
    float2 positionValue,
    float2 start,
    float2 finish,
    uint startMode,
    uint endMode,
    float halfWidth) {
    float2 segment = finish - start;
    float segmentLength = length(segment);
    float2 direction = segment / segmentLength;
    float2 normal = float2(-direction.y, direction.x);
    float startExtension = startMode == 1 ? halfWidth : 0.0;
    float endExtension = endMode == 1 ? halfWidth : 0.0;
    float center = (segmentLength + endExtension - startExtension) * 0.5;
    float2 local = float2(
        dot(positionValue - start, direction) - center,
        dot(positionValue - start, normal));
    float distanceValue = boxDistance(
        local,
        float2((segmentLength + startExtension + endExtension) * 0.5, halfWidth));
    if (startMode == 2) distanceValue = min(distanceValue, length(positionValue - start) - halfWidth);
    if (endMode == 2) distanceValue = min(distanceValue, length(positionValue - finish) - halfWidth);
    return distanceValue;
}

float triangleDistance(float2 positionValue, float2 a, float2 b, float2 c) {
    float2 e0 = b - a;
    float2 e1 = c - b;
    float2 e2 = a - c;
    float2 v0 = positionValue - a;
    float2 v1 = positionValue - b;
    float2 v2 = positionValue - c;
    float2 pq0 = v0 - e0 * saturate(dot(v0, e0) / dot(e0, e0));
    float2 pq1 = v1 - e1 * saturate(dot(v1, e1) / dot(e1, e1));
    float2 pq2 = v2 - e2 * saturate(dot(v2, e2) / dot(e2, e2));
    float orientation = sign(e0.x * e2.y - e0.y * e2.x);
    float2 distanceValue = min(
        min(
            float2(dot(pq0, pq0), orientation * (v0.x * e0.y - v0.y * e0.x)),
            float2(dot(pq1, pq1), orientation * (v1.x * e1.y - v1.y * e1.x))),
        float2(dot(pq2, pq2), orientation * (v2.x * e2.y - v2.y * e2.x)));
    return -sqrt(distanceValue.x) * sign(distanceValue.y);
}

float bevelJoinDistance(
    float2 positionValue,
    float2 before,
    float2 joint,
    float2 after,
    float halfWidth) {
    float2 incoming = normalize(joint - before);
    float2 outgoing = normalize(after - joint);
    float turn = incoming.x * outgoing.y - incoming.y * outgoing.x;
    if (abs(turn) < 0.0001) return 1e20;
    float outside = turn > 0.0 ? -1.0 : 1.0;
    float2 incomingNormal = float2(-incoming.y, incoming.x) * outside;
    float2 outgoingNormal = float2(-outgoing.y, outgoing.x) * outside;
    return triangleDistance(
        positionValue,
        joint,
        joint + incomingNormal * halfWidth,
        joint + outgoingNormal * halfWidth);
}

float4 pixelMain(PixelInput input) : SV_Target {
    float coverage = 1.0;
    float4 color = input.tintColor;

    if (input.primitiveKind == 0 || input.primitiveKind == 1
        || input.primitiveKind == 12 || input.primitiveKind == 15) {
        float2 primitiveSize = input.linePoints.zw - input.linePoints.xy;
        float2 centered = input.pixelPosition - (input.linePoints.xy + input.linePoints.zw) * 0.5;
        float distanceToEdge = roundedBoxDistance(
            centered,
            primitiveSize * 0.5,
            min(input.shapeMetrics.x, min(primitiveSize.x, primitiveSize.y) * 0.5));
        float antiAlias = max(fwidth(distanceToEdge), minimumAntialiasWidth);
        float outer = 1.0 - smoothstep(-antiAlias, antiAlias, distanceToEdge);
        if (input.primitiveKind == 1 || input.primitiveKind == 15) {
            float innerDistance = distanceToEdge + max(input.shapeMetrics.y, 0.0);
            float inner = 1.0 - smoothstep(-antiAlias, antiAlias, innerDistance);
            coverage = max(outer - inner, 0.0);
        } else {
            coverage = outer;
        }
    } else if (input.primitiveKind == 2) {
        uint joinMode = input.lineJoin == 0 ? 0 : 2;
        bool hasPrevious = (input.lineFlags & 1) != 0;
        bool hasNext = (input.lineFlags & 2) != 0;
        uint startMode = hasPrevious ? joinMode : input.lineCap;
        uint endMode = hasNext ? joinMode : input.lineCap;
        float halfWidth = input.shapeMetrics.y * 0.5;
        float distanceToLine = cappedSegmentDistance(
            input.pixelPosition,
            input.linePoints.xy,
            input.linePoints.zw,
            startMode,
            endMode,
            halfWidth);
        if (hasNext && input.lineJoin == 0) {
            distanceToLine = min(
                distanceToLine,
                bevelJoinDistance(
                    input.pixelPosition,
                    input.linePoints.xy,
                    input.linePoints.zw,
                    input.lineNeighbors.zw,
                    halfWidth));
        }
        float antiAlias = max(fwidth(distanceToLine), minimumAntialiasWidth);
        if (hasPrevious) {
            float previousDistance = cappedSegmentDistance(
                input.pixelPosition,
                input.lineNeighbors.xy,
                input.linePoints.xy,
                0,
                joinMode,
                halfWidth);
            if (input.lineJoin == 0) {
                previousDistance = min(
                    previousDistance,
                    bevelJoinDistance(
                        input.pixelPosition,
                        input.lineNeighbors.xy,
                        input.linePoints.xy,
                        input.linePoints.zw,
                        halfWidth));
            }
            if (previousDistance <= distanceToLine) discard;
        }
        if (hasNext) {
            float nextDistance = cappedSegmentDistance(
                input.pixelPosition,
                input.linePoints.zw,
                input.lineNeighbors.zw,
                joinMode,
                0,
                halfWidth);
            if (nextDistance < distanceToLine) discard;
        }
        coverage = 1.0 - smoothstep(-antiAlias, antiAlias, distanceToLine);
    } else if (input.primitiveKind == 5) {
        float2 halfSize = (input.linePoints.zw - input.linePoints.xy) * 0.5;
        float2 centered = input.pixelPosition
            - (input.linePoints.xy + input.linePoints.zw) * 0.5;
        float distanceToEdge = ellipseDistance(centered, halfSize);
        float antiAlias = max(fwidth(distanceToEdge), minimumAntialiasWidth);
        coverage = 1.0 - smoothstep(-antiAlias, antiAlias, distanceToEdge);
    } else if (input.primitiveKind == 6) {
        float2 halfSize = (input.linePoints.zw - input.linePoints.xy) * 0.5;
        float2 centered = input.pixelPosition
            - (input.linePoints.xy + input.linePoints.zw) * 0.5;
        float distanceToEdge = arcDistance(
            centered,
            halfSize,
            input.lineNeighbors.x,
            input.lineNeighbors.y,
            input.shapeMetrics.y);
        float antiAlias = max(fwidth(distanceToEdge), minimumAntialiasWidth);
        coverage = 1.0 - smoothstep(-antiAlias, antiAlias, distanceToEdge);
    } else if (input.primitiveKind == 7) {
        float2 primitiveSize = input.linePoints.zw - input.linePoints.xy;
        float2 centered = input.pixelPosition
            - (input.linePoints.xy + input.linePoints.zw) * 0.5;
        float distanceToEdge = roundedBoxDistance(
            centered,
            primitiveSize * 0.5,
            min(primitiveSize.x, primitiveSize.y) * 0.5);
        float antiAlias = max(fwidth(distanceToEdge), minimumAntialiasWidth);
        coverage = 1.0 - smoothstep(-antiAlias, antiAlias, distanceToEdge);
    } else if (input.primitiveKind == 8 || input.primitiveKind == 13) {
        float2 primitiveSize = input.linePoints.zw - input.linePoints.xy;
        float2 halfSize = primitiveSize * 0.5;
        float2 centered = input.pixelPosition
            - (input.linePoints.xy + input.linePoints.zw) * 0.5;
        float distanceToEdge = roundedBoxDistance(
            centered,
            halfSize,
            min(input.shapeMetrics.x, min(primitiveSize.x, primitiveSize.y) * 0.5));
        float antiAlias = max(fwidth(distanceToEdge), minimumAntialiasWidth);
        coverage = 1.0 - smoothstep(-antiAlias, antiAlias, distanceToEdge);
        float2 direction = float2(cos(input.shapeMetrics.y), sin(input.shapeMetrics.y));
        float extent = max(dot(abs(direction), halfSize), 0.0001);
        float phaseShift = input.primitiveKind == 13
            ? sin(float(input.shaderParameter) / 255.0 * 6.28318530718) * 0.25
            : 0.0;
        float amount = saturate(
            0.5 + dot(centered, direction) / (extent * 2.0) + phaseShift);
        color = lerp(input.tintColor, input.lineNeighbors, amount);
    } else if (input.primitiveKind == 9 || input.primitiveKind == 14) {
        float2 offset = input.primitiveKind == 9 ? input.lineNeighbors.xy : 0.0;
        float2 center = (input.linePoints.xy + input.linePoints.zw) * 0.5
            + offset;
        float2 halfSize = (input.linePoints.zw - input.linePoints.xy) * 0.5;
        float distanceToEdge = roundedBoxDistance(
            input.pixelPosition - center,
            halfSize,
            min(input.shapeMetrics.x, min(halfSize.x, halfSize.y)));
        float normalizedDistance = max(distanceToEdge, 0.0)
            / max(input.shapeMetrics.y, 0.001);
        coverage = distanceToEdge <= 0.0
            ? 1.0
            : exp(-0.5 * normalizedDistance * normalizedDistance);
    } else if (input.primitiveKind == 10) {
        float2 primitiveSize = input.linePoints.zw - input.linePoints.xy;
        float2 halfSize = primitiveSize * 0.5;
        float2 centered = input.pixelPosition
            - (input.linePoints.xy + input.linePoints.zw) * 0.5;
        float distanceToEdge = variableRoundedBoxDistance(
            centered,
            halfSize,
            input.lineNeighbors);
        float antiAlias = max(fwidth(distanceToEdge), minimumAntialiasWidth);
        float outer = 1.0 - smoothstep(-antiAlias, antiAlias, distanceToEdge);
        float borderWidth = min(input.shapeMetrics.y, min(halfSize.x, halfSize.y));
        float2 innerHalfSize = max(halfSize - borderWidth, 0.001);
        float innerDistance = variableRoundedBoxDistance(
            centered,
            innerHalfSize,
            max(input.lineNeighbors - borderWidth, 0.0));
        float inner = 1.0 - smoothstep(-antiAlias, antiAlias, innerDistance);
        coverage = max(outer - inner, 0.0);
    } else if (input.primitiveKind == 3) {
        color *= straightTextureSample(input.textureSlot, input.textureUv);
    } else if (input.primitiveKind == 4) {
        color.a *= sampleTexture(input.textureSlot, input.textureUv).r;
    } else if (input.primitiveKind == 11) {
        float2 primitiveSize = input.linePoints.zw - input.linePoints.xy;
        float2 local = saturate((input.pixelPosition - input.linePoints.xy) / primitiveSize);
        float2 destinationBorder = min(
            input.shapeMetrics.x / primitiveSize,
            0.5);
        float2 mapped = float2(
            ninePatchCoordinate(local.x, destinationBorder.x, input.shapeMetrics.y),
            ninePatchCoordinate(local.y, destinationBorder.y, input.shapeMetrics.y));
        float2 uv = lerp(input.lineNeighbors.xy, input.lineNeighbors.zw, mapped);
        color *= straightTextureSample(input.textureSlot, uv);
    } else if (input.primitiveKind == 16) {
        float distanceValue = sampleTexture(input.textureSlot, input.textureUv).r;
        coverage = smoothstep(
            input.shapeMetrics.x - input.shapeMetrics.y,
            input.shapeMetrics.x + input.shapeMetrics.y,
            distanceValue);
    }

    color.a *= coverage;
    clip(color.a - 0.001);
    return float4(color.rgb * color.a, color.a);
}
