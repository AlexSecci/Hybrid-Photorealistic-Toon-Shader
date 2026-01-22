#version 460 core

// This is the primary shader for light calculations
out vec4 FragColor;

in vec2 TexCoords;

// G-Buffer textures
uniform sampler2D gBaseColor;       
uniform sampler2D gNormal;          
uniform sampler2D gPosition;        
uniform sampler2D gQuantization;    

// Shadow maps.
// TODO: I set both a dir and point arrays to 8 to allow testing but my showcase scene will have 2 directional (when one is active the other is not), 7 points and no spot light.
// Once I'm done testing I should decide what to do here
uniform sampler2D shadowMaps[8];
uniform samplerCube shadowCubeMaps[8];
uniform mat4 lightSpaceMatrices[8];

// Shadow parameters
uniform float shadowBias;
uniform float shadowNormalBias;
uniform int shadowPCFSamples;
uniform float shadowIntensity;
uniform bool enablePCF;
uniform float shadowFarPlane;

// Light struct. type can either be 0, 1 or 2 (dir / point / spot)
// For now this shader only really behaves well with dir + point lights.
// Spot lights technically work but aren't tuned.
struct Light {
    int type;
    vec3 position;
    vec3 direction;
    vec3 color;
    float intensity;
    float constant;         
    float linear;           
    float quadratic;        
    float cutOff;           
    float outerCutOff;      
    bool castShadows;
};

// Light data
uniform int numLights;
uniform Light lights[8];

// Camera data
uniform vec3 viewPos;

// Cel shading parameters
uniform int diffuseQuantizationBands;
uniform float specularThreshold1;
uniform float specularThreshold2;

// PCF sampling offsets for soft shadows
vec3 sampleOffsetDirections[20] = vec3[]
(
   vec3( 1,  1,  1), vec3( 1, -1,  1), vec3(-1, -1,  1), vec3(-1,  1,  1), 
   vec3( 1,  1, -1), vec3( 1, -1, -1), vec3(-1, -1, -1), vec3(-1,  1, -1),
   vec3( 1,  1,  0), vec3( 1, -1,  0), vec3(-1, -1,  0), vec3(-1,  1,  0),
   vec3( 1,  0,  1), vec3(-1,  0,  1), vec3( 1,  0, -1), vec3(-1,  0, -1),
   vec3( 0,  1,  1), vec3( 0, -1,  1), vec3( 0, -1, -1), vec3( 0,  1, -1)
);

// Calculate shadow for dir/spot lights
float calculateDirectionalSpotShadow(int lightIndex, vec3 fragPos, vec3 normal, vec3 lightDir)
{
    // First we need to transform to light space, then perspective divide and finally normalize to 0,1
    vec4 fragPosLightSpace = lightSpaceMatrices[lightIndex] * vec4(fragPos, 1.0);
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    
    // If outside shadow map then there is no shadow
    if(projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || 
       projCoords.y < 0.0 || projCoords.y > 1.0){
        //return 0;
        return 1.0;
       }
    
    // Get depth from light's perspective
    float currentDepth = projCoords.z;
    
    // Calculate bias
    float bias = max(shadowBias * (1.0 - dot(normal, lightDir)), shadowBias * 0.1);
    
    // Fake the position to be further along its normal to reduce shadow acne
    vec3 offsetPos = fragPos + normal * shadowNormalBias;
    vec4 offsetPosLightSpace = lightSpaceMatrices[lightIndex] * vec4(offsetPos, 1.0);
    vec3 offsetProjCoords = offsetPosLightSpace.xyz / offsetPosLightSpace.w;
    offsetProjCoords = offsetProjCoords * 0.5 + 0.5;
    currentDepth = offsetProjCoords.z;
    
    float shadow = 0.0;
    
    // Check if PCF is enabled and if so do the computations
    if (enablePCF && shadowPCFSamples > 0) {
        vec2 texelSize = 1.0 / textureSize(shadowMaps[lightIndex], 0);
        for(int x = -shadowPCFSamples; x <= shadowPCFSamples; ++x)
        {
            for(int y = -shadowPCFSamples; y <= shadowPCFSamples; ++y)
            {
                float pcfDepth = texture(shadowMaps[lightIndex], projCoords.xy + vec2(x, y) * texelSize).r;
                shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
            }
        }
        int kernelSize = shadowPCFSamples * 2 + 1;
        shadow /= float(kernelSize * kernelSize);
    } else {
        // Get closest depth value from light's perspective
        float closestDepth = texture(shadowMaps[lightIndex], projCoords.xy).r;
        shadow = currentDepth - bias > closestDepth ? 1.0 : 0.0;
    }
    
    // Once we're done with computing shadow we have to weight it
    shadow = mix(1.0, 1.0 - shadow, 1.0 - shadowIntensity);
    
    return shadow;
}

// Calculate shadow using damned cube maps
float calculatePointShadow(int lightIndex, vec3 fragPos, vec3 lightPos)
{
    // Get distance from light to fragment
    vec3 fragToLight = fragPos - lightPos;
    float currentDepth = length(fragToLight);
    
    // Recover depth from cubemap that is stored as distance/farPlane)
    float closestDepth = texture(shadowCubeMaps[lightIndex], fragToLight).r;
    closestDepth = closestDepth * shadowFarPlane;
    
    // Since point lights are giving a ton of acne we gonna increase it
    float bias = shadowBias * 2.5;
    
    float shadow = 0.0;
    
    // Check if PCF is enabled and if so do the computations
    if (enablePCF && shadowPCFSamples > 0) {
        //do not ever touch this logic until project is delivered unless u wanna spend another week debugging PCD shadows
        float samples = 4.0;
        float offset = 0.1;
        for(float x = -offset; x < offset; x += offset / (samples * 0.5))
        {
            for(float y = -offset; y < offset; y += offset / (samples * 0.5))
            {
                for(float z = -offset; z < offset; z += offset / (samples * 0.5))
                {
                    float closestDepth = texture(shadowCubeMaps[lightIndex], fragToLight + vec3(x, y, z)).r;
                    closestDepth *= shadowFarPlane;
                    if(currentDepth - bias > closestDepth)
                        shadow += 1.0;
                }
            }
        }
        shadow /= (samples * samples * samples);
    } else {
        // Hard shadows
        shadow = currentDepth - bias > closestDepth ? 1.0 : 0.0;
    }
    
    shadow = mix(1.0, 1.0 - shadow, 1.0 - shadowIntensity);
    
    return shadow;
}

// Simple quantization for lighti intensity and specular highlights
float quantizeDiffuseIntensity(float intensity, int bands) {
    if (bands <= 1) return intensity;
    
    float bandsFloat = float(bands);
    intensity = clamp(intensity, 0.0, 1.0);
    return floor(intensity * bandsFloat + 0.5) / bandsFloat;
}

float quantizeSpecularIntensity(float intensity, float threshold1, float threshold2) {
    intensity = clamp(intensity, 0.0, 1.0);
    threshold1 = clamp(threshold1, 0.0, 1.0);
    threshold2 = clamp(threshold2, threshold1 + 0.01, 1.0);
    
    if (intensity < threshold1) {
        return 0.0;
    } else if (intensity < threshold2) {
        return 0.8;
    } else {
        return 0.95;
    }
}

vec3 hybridCelShading(vec3 baseColor, float diffuseIntensity, float specularIntensity, int diffuseBands, float specThreshold1, float specThreshold2, float shadowFactor) {
    
    // Turns out that if we apply shadow BEFORE quantization we obtain better cel shading
    diffuseIntensity *= shadowFactor;
    
    // Quantize diffuse intensity
    float quantizedDiffuse = quantizeDiffuseIntensity(diffuseIntensity, diffuseBands);
    
    // Apply quantized diffuse to base color
    vec3 diffuseColor = baseColor * quantizedDiffuse;
    
    // no specular in shadowed areas
    specularIntensity *= shadowFactor;
    
    // Quantize specular highlights
    float quantizedSpecular = quantizeSpecularIntensity(specularIntensity, specThreshold1, specThreshold2);
    
    // Add quantized specular
    vec3 specularColor = vec3(quantizedSpecular * 0.6);
    vec3 finalColor = diffuseColor + specularColor;
    
    // Gamma correction
    //finalColor = pow(finalColor, vec3(0.9));
    finalColor = pow(finalColor, vec3(1));
    
    return finalColor;
}

// Blinn-Phong specular calculation
float calculateSpecularIntensity(vec3 lightDir, vec3 viewDir, vec3 normal, float shininess) {
    float NdotL = max(dot(normal, lightDir), 0.0);
    if (NdotL <= 0.0) return 0.0;
    
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfwayDir), 0.0);
    
    float specular = pow(NdotH, shininess);
    return specular * NdotL;
}

// G-buffer data structure
struct GBufferData {
    vec3 baseColor;
    float materialType;
    vec3 worldNormal;
    float roughness;
    vec3 worldPosition;
    float specularShininess;
    float ambientOcclusion;
    float edgeWeight;
};

GBufferData sampleGBuffer(vec2 texCoords) {
    GBufferData data;
    
    vec4 baseColorData = texture(gBaseColor, texCoords);
    vec4 normalData = texture(gNormal, texCoords);
    vec4 positionData = texture(gPosition, texCoords);
    vec4 quantizationData = texture(gQuantization, texCoords);
    
    data.baseColor = baseColorData.rgb;
    data.materialType = baseColorData.a;
    data.worldNormal = normalize(normalData.rgb);
    data.roughness = normalData.a;
    data.worldPosition = positionData.rgb;
    data.specularShininess = positionData.a;
    data.ambientOcclusion = quantizationData.b;
    data.edgeWeight = quantizationData.a;
    
    return data;
}

// Minnaert illumination model
vec3 calculateMinnaert(vec3 normal, vec3 lightDir, vec3 viewDir, float k) {
    float NdotL = max(dot(normal, lightDir), 0.0);
    float NdotV = max(dot(normal, viewDir), 0.0);
    return vec3(pow(max(NdotL * NdotV, 0.001), k));
}

// Oren-Nayar illumination model
vec3 calculateOrenNayar(vec3 normal, vec3 lightDir, vec3 viewDir, float roughness, vec3 albedo) {
    float sigma2 = roughness * roughness;
    float A = 1.0 - (sigma2 / (2.0 * (sigma2 + 0.33)));
    float B = 0.45 * sigma2 / (sigma2 + 0.09);
    
    float NdotL = max(dot(normal, lightDir), 0.0);
    float NdotV = max(dot(normal, viewDir), 0.0);
    
    vec3 lightProj = lightDir - normal * NdotL;
    vec3 viewProj = viewDir - normal * NdotV;
    
    if (length(lightProj) > 0.001) lightProj = normalize(lightProj);
    if (length(viewProj) > 0.001) viewProj = normalize(viewProj);
    
    float deltaAlpha = max(0.0, dot(lightProj, viewProj));
    float sinAlpha, tanBeta;
    if (NdotL < NdotV) {
        sinAlpha = sqrt(max(0.0, 1.0 - NdotL*NdotL));
        tanBeta = sqrt(max(0.0, 1.0 - NdotV*NdotV)) / max(NdotV, 0.001);
    } else {
        sinAlpha = sqrt(max(0.0, 1.0 - NdotV*NdotV));
        tanBeta = sqrt(max(0.0, 1.0 - NdotL*NdotL)) / max(NdotL, 0.001);
    }
    
    return albedo * NdotL * (A + B * deltaAlpha * sinAlpha * tanBeta);
}

// Ashikhmin-Shirley model
void determineTangentFrame(vec3 N, out vec3 T, out vec3 B) {
    vec3 up = abs(N.y) < 0.999999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    T = normalize(cross(up, N));
    B = cross(N, T);
}

vec3 calculateAshikhminShirley(vec3 N, vec3 L, vec3 V, float nu, float nv, vec3 albedo) {
    vec3 H = normalize(L + V);
    vec3 T, B;
    determineTangentFrame(N, T, B);
    
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);
    
    if (NdotL == 0.0 || NdotV == 0.0) return vec3(0.0);
    
    // Diffuse term
    float Rd = (28.0 * (1.0 - max(dot(N, L), 0.0) / 2.0) * (1.0 - max(dot(N, V), 0.0) / 2.0)) / (23.0 * 3.14159);
    vec3 diffuse = albedo * Rd * (1.0 - pow(1.0 - NdotL / 2.0, 5.0)) * (1.0 - pow(1.0 - NdotV / 2.0, 5.0));
    
    // Specular term
    float HdotT = dot(H, T);
    float HdotB = dot(H, B);
    
    float exponent = (nu * HdotT * HdotT + nv * HdotB * HdotB) / (1.0 - NdotH * NdotH);
    float numerator = sqrt((nu + 1.0) * (nv + 1.0)) * pow(NdotH, exponent);
    float denominator = 8.0 * 3.14159 * HdotV * max(NdotL, NdotV);
    
    // Fresnel term (Schlick approximation with magic numbers)
    vec3 F0 = vec3(0.04);
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - HdotV, 5.0);
    
    vec3 specular = (numerator / denominator) * F;
    
    return diffuse + specular; 
}

// Cook-Torrance lighting
vec3 calculateCookTorrance(vec3 N, vec3 L, vec3 V, float roughness, float F0_val, vec3 albedo) {
    vec3 H = normalize(L + V);
    
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    
    if (NdotL == 0.0) return vec3(0.0);
    
    // Normal Distribution Function (Beckmann)
    float NdotH = max(dot(N, H), 0.001);
    float NdotH2 = NdotH * NdotH;
    float m2 = roughness * roughness;
    float r1 = 1.0 / (4.0 * m2 * pow(NdotH, 4.0));
    float r2 = (NdotH2 - 1.0) / (m2 * NdotH2);
    float D = r1 * exp(r2);
    
    // Geometric Attenuation (Cook-Torrance)
    float HdotV = max(dot(H, V), 0.0);
    float NdotH_x2 = 2.0 * NdotH;
    float G1 = (NdotH_x2 * NdotV) / HdotV;
    float G2 = (NdotH_x2 * NdotL) / HdotV;
    float G = min(1.0, min(G1, G2));
    
    // Fresnel again for kS
    vec3 F0 = vec3(F0_val);
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - HdotV, 5.0);
    
    // Specular component
    vec3 numerator = D * F * G;
    float denominator = 4.0 * NdotV * NdotL + 0.001; 
    vec3 kS = numerator / denominator;
    
    // Theory note: energy conservation implies we subtract specular but, for simple integration, we don't and instead we compute how much light is not reflected and modulate intensity
    // TODO: should probably look into it since I always have to increase intensity multiplier when using this model otherwise it's too dark
    // Diffuse component (energy conservation)
    vec3 kD = vec3(1.0) - F; // F represents the ratio of light that gets reflected
    
    // Final color
    return (kD * albedo / 3.14159 + kS) * NdotL; 
}

uniform bool enableQuantization;
uniform int globalMaterialType;

vec3 calculateHybridCelShading(GBufferData gData, vec3 viewDir) {
    vec3 totalLighting = vec3(0.0);
    
    int materialType = int(round(gData.materialType));
    
    // Iterate through all lights
    for (int i = 0; i < numLights && i < 8; ++i) {
        Light light = lights[i];
        
        if (light.intensity <= 0.0) continue;
        
        vec3 lightContrib = vec3(0.0);
        vec3 lightDir;
        float attenuation = 1.0;
        float shadowFactor = 1.0;
        
        bool shouldCastShadows = light.castShadows;
        
        // Calculate light direction, attenuation, and shadows based on light type
        if (light.type == 0) { // Directional light
            lightDir = normalize(-light.direction);
            if (shouldCastShadows)
                shadowFactor = calculateDirectionalSpotShadow(i, gData.worldPosition, gData.worldNormal, lightDir);
        } else if (light.type == 1) { // Point light
            vec3 lightVec = light.position - gData.worldPosition;
            lightDir = normalize(lightVec);
            float distance = length(lightVec);
            attenuation = 1.0 / (light.constant + light.linear * distance + light.quadratic * distance * distance);
            if (shouldCastShadows)
                shadowFactor = calculatePointShadow(i, gData.worldPosition, light.position);
        } else if (light.type == 2) { // Spot light
            vec3 lightVec = light.position - gData.worldPosition;
            lightDir = normalize(lightVec);
            float distance = length(lightVec);
            attenuation = 1.0 / (light.constant + light.linear * distance + light.quadratic * distance * distance);
            float theta = dot(lightDir, normalize(-light.direction));
            float epsilon = light.cutOff - light.outerCutOff;
            float intensity = clamp((theta - light.outerCutOff) / epsilon, 0.0, 1.0);
            attenuation *= intensity;
            if (shouldCastShadows)
                shadowFactor = calculateDirectionalSpotShadow(i, gData.worldPosition, gData.worldNormal, lightDir);
        }
        
        // don't mind magic numbers. they work.
        if (attenuation <= 0.001) continue;
        
        float diffuseIntensity = 0.0;
        vec3 diffuseColor = vec3(0.0);
        float specularIntensity = 0.0;
        
        // Calculate diffuse based on material type
        if (materialType == 0) { // Lambertian
            float NdotL = max(dot(gData.worldNormal, lightDir), 0.0);
            diffuseIntensity = NdotL;
            diffuseColor = gData.baseColor * diffuseIntensity;
        } else if (materialType == 1) { // Minnaert
            vec4 extraParams = texture(gQuantization, TexCoords);
            float k = extraParams.r;
            vec3 minnaert = calculateMinnaert(gData.worldNormal, lightDir, viewDir, k);
            diffuseIntensity = minnaert.r; 
            diffuseColor = gData.baseColor * minnaert;
        } else if (materialType == 2) { // Oren-Nayar
            vec4 extraParams = texture(gQuantization, TexCoords);
            float roughness = extraParams.g;
            vec3 orenNayar = calculateOrenNayar(gData.worldNormal, lightDir, viewDir, roughness, gData.baseColor);
            diffuseIntensity = length(orenNayar) / length(gData.baseColor + 0.001); 
            diffuseColor = orenNayar;
        } else if (materialType == 3) { // Ashikhmin-Shirley
            // NOTE: Ashikhmin-Shirley and Cook-Torrance already include specular so we skip its calculation. This won't look very cartoonish but seems a good way to showcase this type of anisotropy
            vec4 extraParams = texture(gQuantization, TexCoords);
            float nu = extraParams.r;
            float nv = extraParams.g;
            vec3 ashikhmin = calculateAshikhminShirley(gData.worldNormal, lightDir, viewDir, nu, nv, gData.baseColor);
            diffuseIntensity = length(ashikhmin) / length(gData.baseColor + 0.001);
            diffuseColor = ashikhmin;
            specularIntensity = 0.0;
        } else if (materialType == 4) { // Cook-Torrance
            vec4 extraParams = texture(gQuantization, TexCoords);
            float roughness = extraParams.r;
            float F0 = extraParams.g;
            vec3 cookTorrance = calculateCookTorrance(gData.worldNormal, lightDir, viewDir, roughness, F0, gData.baseColor);
            
            diffuseIntensity = length(cookTorrance) / length(gData.baseColor + 0.001);
            diffuseColor = cookTorrance;
            specularIntensity = 0.0; // Included
        }

        diffuseIntensity *= light.intensity * attenuation;
        diffuseColor *= light.intensity * attenuation;
        
        if (materialType != 3 && materialType != 4) {
            specularIntensity = calculateSpecularIntensity(lightDir, viewDir, gData.worldNormal, gData.specularShininess) * light.intensity * attenuation * 0.3;
        }

        if (enableQuantization) {
             // cel shading
             vec3 celColor = hybridCelShading(gData.baseColor, diffuseIntensity, specularIntensity,
                                            diffuseQuantizationBands, specularThreshold1, specularThreshold2, shadowFactor);
             lightContrib = celColor * light.color;
        } else {
            // "photorealistic" rendering
            vec3 finalDiffuse = diffuseColor * shadowFactor;
            vec3 finalSpecular = vec3(specularIntensity * shadowFactor);
            lightContrib = (finalDiffuse + finalSpecular) * light.color;
        }
        
        totalLighting += lightContrib;
    }
    
    // Add ambient lighting on top of everything else
    vec3 ambient = gData.baseColor * 0.1 * gData.ambientOcclusion;
    totalLighting += ambient;
    
    return totalLighting;
}

void main() {
    GBufferData gData = sampleGBuffer(TexCoords);
    
    // Early exit for background pixels
    if (length(gData.worldNormal) < 0.1) {
        FragColor = vec4(0.05, 0.05, 0.1, 1.0);
        return;
    }
    
    vec3 viewDir = normalize(viewPos - gData.worldPosition);
    vec3 finalColor = calculateHybridCelShading(gData, viewDir);
    
    FragColor = vec4(finalColor, 1.0);
}
