#version 460 core

out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gDepth;
uniform sampler2D colorTexture;

uniform int edgeFlags;
uniform float depthThreshold;
uniform float normalThreshold;
uniform float sobelThreshold;
uniform float colorThreshold;
uniform vec3 edgeColor;
uniform vec2 screenSize;

uniform float depthExponent;
uniform float normalSplit;
uniform float sobelScale;
uniform float smoothWidth;
uniform float laplacianThreshold;
uniform float laplacianScale;

const int DEPTH_BASED = 1;
const int NORMAL_BASED = 2;
const int SOBEL = 4;
const int COLOR_BASED = 8;
const int LAPLACIAN = 16;

// Linearize depth
float getLinearDepth(float d)
{
    return pow(d, depthExponent);
}

// TODO: here i'm using a single function to avoid spamming code but I have a feeling some edges are weak because of this.
float getEdgeIntensity(float val, float threshold)
{
    // we have an edge when when val > threshold
    float edgeW = smoothWidth * 0.01;
    // using smoothstep seems better than step as it produces slightly less jagged lines
    //return step(threshold - edgeW, threshold + edgeW, val);
    return smoothstep(threshold - edgeW, threshold + edgeW, val);
}

float depthEdgeDetection()
{
    vec2 texelSize = 1.0 / screenSize;
    
    float depth = getLinearDepth(texture(gDepth, TexCoords).r);
    float depthN = getLinearDepth(texture(gDepth, TexCoords + vec2(0.0, texelSize.y)).r);
    float depthS = getLinearDepth(texture(gDepth, TexCoords - vec2(0.0, texelSize.y)).r);
    float depthE = getLinearDepth(texture(gDepth, TexCoords + vec2(texelSize.x, 0.0)).r);
    float depthW = getLinearDepth(texture(gDepth, TexCoords - vec2(texelSize.x, 0.0)).r);
    
    float depthGradX = abs(depthE - depthW);
    float depthGradY = abs(depthN - depthS);
    float depthGrad = sqrt(depthGradX * depthGradX + depthGradY * depthGradY);
    
    return getEdgeIntensity(depthGrad, depthThreshold);
}

float normalEdgeDetection()
{
    vec2 texelSize = 1.0 / screenSize;
    
    vec3 normal = texture(gNormal, TexCoords).rgb;
    vec3 normalN = texture(gNormal, TexCoords + vec2(0.0, texelSize.y)).rgb;
    vec3 normalS = texture(gNormal, TexCoords - vec2(0.0, texelSize.y)).rgb;
    vec3 normalE = texture(gNormal, TexCoords + vec2(texelSize.x, 0.0)).rgb;
    vec3 normalW = texture(gNormal, TexCoords - vec2(texelSize.x, 0.0)).rgb;
    
    float dotN = dot(normal, normalN);
    float dotS = dot(normal, normalS);
    float dotE = dot(normal, normalE);
    float dotW = dot(normal, normalW);
    float minDot = min(min(dotN, dotS), min(dotE, dotW));
    // Was more precise but also too conservative. For now let's keep it as is
    //float maxDot = max(max(dotN, dotS), max(dotE, dotW));
    //float diffDot = abs(maxDot - minDot);
    float diffDot = 1.0 - minDot;
    
    return getEdgeIntensity(diffDot, normalThreshold);
}

// Sobel computation
float computeSobel(float tl, float tm, float tr, float ml, float mr, float bl, float bm, float br) 
{
    float gx = tl * -1.0 + tr * 1.0 + ml * -2.0 + mr * 2.0 + bl * -1.0 + br * 1.0;
    float gy = tl * -1.0 + tm * -2.0 + tr * -1.0 + bl * 1.0 + bm * 2.0 + br * 1.0;
    return sqrt(gx * gx + gy * gy);
}

// Compute Sobel for per-axis Normals
float normalSobelDetection() 
{
    vec2 texelSize = 1.0 / screenSize;
    
    vec3 tl = texture(gNormal, TexCoords + vec2(-texelSize.x, texelSize.y)).rgb;
    vec3 tm = texture(gNormal, TexCoords + vec2(0.0, texelSize.y)).rgb;
    vec3 tr = texture(gNormal, TexCoords + vec2(texelSize.x, texelSize.y)).rgb;
    vec3 ml = texture(gNormal, TexCoords + vec2(-texelSize.x, 0.0)).rgb;
    vec3 mr = texture(gNormal, TexCoords + vec2(texelSize.x, 0.0)).rgb;
    vec3 bl = texture(gNormal, TexCoords + vec2(-texelSize.x, -texelSize.y)).rgb;
    vec3 bm = texture(gNormal, TexCoords + vec2(0.0, -texelSize.y)).rgb;
    vec3 br = texture(gNormal, TexCoords + vec2(texelSize.x, -texelSize.y)).rgb;
    
    float magX = computeSobel(tl.x, tm.x, tr.x, ml.x, mr.x, bl.x, bm.x, br.x);
    float magY = computeSobel(tl.y, tm.y, tr.y, ml.y, mr.y, bl.y, bm.y, br.y);
    float magZ = computeSobel(tl.z, tm.z, tr.z, ml.z, mr.z, bl.z, bm.z, br.z);
    
    float totalSobel = (magX + magY + magZ) * sobelScale; 
    
    return getEdgeIntensity(totalSobel, sobelThreshold);
}

//This computes Sobel for colors
float colorEdgeDetection()
{
    vec2 texelSize = 1.0 / screenSize;
    
    // magic numbers to tune how much each channel impactgs color difference 
    const vec3 lumWeights = vec3(0.299, 0.587, 0.114);
    
    float tl = dot(texture(colorTexture, TexCoords + vec2(-texelSize.x, texelSize.y)).rgb, lumWeights);
    float tm = dot(texture(colorTexture, TexCoords + vec2(0.0, texelSize.y)).rgb, lumWeights);
    float tr = dot(texture(colorTexture, TexCoords + vec2(texelSize.x, texelSize.y)).rgb, lumWeights);
    float ml = dot(texture(colorTexture, TexCoords + vec2(-texelSize.x, 0.0)).rgb, lumWeights);
    float mr = dot(texture(colorTexture, TexCoords + vec2(texelSize.x, 0.0)).rgb, lumWeights);
    float bl = dot(texture(colorTexture, TexCoords + vec2(-texelSize.x, -texelSize.y)).rgb, lumWeights);
    float bm = dot(texture(colorTexture, TexCoords + vec2(0.0, -texelSize.y)).rgb, lumWeights);
    float br = dot(texture(colorTexture, TexCoords + vec2(texelSize.x, -texelSize.y)).rgb, lumWeights);
    
    float edgeStr = computeSobel(tl, tm, tr, ml, mr, bl, bm, br) * sobelScale;
    
    return getEdgeIntensity(edgeStr, colorThreshold);
}

// 3 by 3 Laplacian with 8 Neighbor kernel
float laplacianEdgeDetection() 
{
    vec2 texelSize = 1.0 / screenSize;
    
    float center = getLinearDepth(texture(gDepth, TexCoords).r);
    float n = getLinearDepth(texture(gDepth, TexCoords + vec2(0.0, texelSize.y)).r);
    float s = getLinearDepth(texture(gDepth, TexCoords - vec2(0.0, texelSize.y)).r);
    float e = getLinearDepth(texture(gDepth, TexCoords + vec2(texelSize.x, 0.0)).r);
    float w = getLinearDepth(texture(gDepth, TexCoords - vec2(texelSize.x, 0.0)).r);
    
    float nw = getLinearDepth(texture(gDepth, TexCoords + vec2(-texelSize.x, texelSize.y)).r);
    float ne = getLinearDepth(texture(gDepth, TexCoords + vec2(texelSize.x, texelSize.y)).r);
    float sw = getLinearDepth(texture(gDepth, TexCoords + vec2(-texelSize.x, -texelSize.y)).r);
    float se = getLinearDepth(texture(gDepth, TexCoords + vec2(texelSize.x, -texelSize.y)).r);
    
    float laplacian = (n + s + e + w + nw + ne + sw + se) - 8.0 * center;
    
    // laplacian measures the intensity of change so we multiply by the slider value and check if it meets the threashold
    return getEdgeIntensity(abs(laplacian) * laplacianScale, laplacianThreshold);
}

void main()
{
    float edge = 0.0;
    
    if ((edgeFlags & DEPTH_BASED) != 0) {
        edge = max(edge, depthEdgeDetection());
    }
    
    if ((edgeFlags & NORMAL_BASED) != 0) {
        edge = max(edge, normalEdgeDetection());
    }
    
    if ((edgeFlags & SOBEL) != 0) {
        edge = max(edge, normalSobelDetection());
    }
    
    if ((edgeFlags & COLOR_BASED) != 0) {
        edge = max(edge, colorEdgeDetection());
    }
    
    if ((edgeFlags & LAPLACIAN) != 0) {
        edge = max(edge, laplacianEdgeDetection());
    }
    
    FragColor = vec4(edgeColor * edge, edge);
}