#version 460 core

// G-Buffer
layout (location = 0) out vec4 gBaseColor;      // RGB: Base color, A: Material type
layout (location = 1) out vec4 gNormal;         // RGB: World normal, A: Roughness
layout (location = 2) out vec4 gPosition;       // RGB: World position, A: SpecularShininess
layout (location = 3) out vec4 gQuantization;   // R: Bands, G: Smoothness, B: AO, A: Edge weight

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;

// Model Textures
uniform sampler2D texture_diffuse1;
uniform sampler2D texture_normal1;
uniform sampler2D texture_roughness1;
uniform sampler2D texture_metallic1;
uniform sampler2D texture_ao1;

// Material properties
uniform bool hasTexture;
uniform bool hasNormalMap;
uniform bool hasRoughnessMap;
uniform bool hasMetallicMap;
uniform bool hasAOMap;

// Material parameters
uniform vec3 albedo;
uniform float roughness;
uniform float specularShininess;
uniform int materialType;        
uniform float minnaertK;         
uniform float orenNayarRoughness; 
uniform float ashikhminShirleyNu; 
uniform float ashikhminShirleyNv; 
uniform float cookTorranceRoughness; 
uniform float cookTorranceF0;        
uniform float intensityCorrection;
uniform float ambientOcclusion;

void main()
{
    // Sample base color from texture
    vec3 baseColor;
    if (hasTexture) {
        baseColor = texture(texture_diffuse1, TexCoords).rgb * intensityCorrection;
    } else {
        baseColor = albedo * intensityCorrection;
    }
    
    float materialRoughness = hasRoughnessMap ? texture(texture_roughness1, TexCoords).r : roughness;
    // float materialMetallic = hasMetallicMap ? texture(texture_metallic1, TexCoords).r : metallic; // logic unused now
    float materialAO = hasAOMap ? texture(texture_ao1, TexCoords).r : ambientOcclusion;

    
    // Normal mapping
    vec3 worldNormal = normalize(Normal);
    if (hasNormalMap) {
        // Basic normal mapping (would need tangent space for full implementation)
        vec3 normalMap = texture(texture_normal1, TexCoords).rgb * 2.0 - 1.0;
        // TODO: For now i'm not implementing tangent space calculation taht would go here
        worldNormal = normalize(Normal);
    }
    
    // Fill G-Buffer
    gBaseColor = vec4(baseColor, float(materialType));
    gNormal = vec4(worldNormal, materialRoughness);

    gPosition = vec4(FragPos, specularShininess);
    float param1 = minnaertK;
    float param2 = orenNayarRoughness;
    
    // Set correct parameters based on the active illum model
    if (materialType == 3) {
        param1 = ashikhminShirleyNu;
        param2 = ashikhminShirleyNv;
    }
    
    if (materialType == 4) {
        param1 = cookTorranceRoughness;
        param2 = cookTorranceF0;
    }

    gQuantization = vec4(
        param1,
        param2,
        materialAO,
        1.0  // Edge detection weight
    );
}