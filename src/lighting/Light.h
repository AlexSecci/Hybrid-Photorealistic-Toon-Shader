#pragma once

#include <glm/glm.hpp>

enum class LightType {
    DIRECTIONAL, 
    POINT,       
    SPOT         
};

struct Light {
    LightType type;
    glm::vec3 position;
    glm::vec3 direction;
    glm::vec3 color;
    float intensity;
    
    // Attenuation formula used: 1.0 / (constant + linear * dist + quadratic * dist * dist)
    float constant;
    float linear;
    float quadratic;
    
    // Spotlights.
    float cutOff;      // Inner angle (full brightness)
    float outerCutOff; // Outer angle (fades to zero)
    
    // Shadow Mapping configuration.
    bool castShadows;
    
    Light() 
        : type(LightType::POINT), 
          position(0.0f), 
          direction(0.0f, -1.0f, 0.0f),
          color(1.0f), 
          intensity(1.0f),
          constant(1.0f), 
          linear(0.09f), 
          quadratic(0.032f),
          cutOff(12.5f), 
          outerCutOff(15.0f),
          castShadows(true),
          isStatic(false),
          flicker(false),
          baseIntensity(1.0f),
          baseColor(1.0f)
    {}

    bool isStatic;         
    bool flicker;          
    float baseIntensity;   
    glm::vec3 baseColor;   
    
    // Method for creating a directional light source.
    static Light createDirectionalLight(const glm::vec3& direction, const glm::vec3& color, float intensity = 1.0f) {
        Light light;
        light.type = LightType::DIRECTIONAL;
        light.direction = glm::normalize(direction);
        light.color = color;
        light.intensity = intensity;
        light.baseIntensity = intensity;
        light.baseColor = color;
        light.castShadows = true;
        return light;
    }
    
    // Method for creating a point light.
    static Light createPointLight(const glm::vec3& position, const glm::vec3& color, float intensity = 1.0f) {
        Light light;
        light.type = LightType::POINT;
        light.position = position;
        light.color = color;
        light.intensity = intensity;
        light.baseIntensity = intensity;
        light.baseColor = color;
        return light;
    }
    
    // Method for creating a spotlight.
    static Light createSpotLight(const glm::vec3& position, const glm::vec3& direction, 
                                const glm::vec3& color, float cutOff, float outerCutOff, float intensity = 1.0f) {
        Light light;
        light.type = LightType::SPOT;
        light.position = position;
        light.direction = glm::normalize(direction);
        light.color = color;
        light.intensity = intensity;
        light.baseIntensity = intensity;
        light.baseColor = color;
        light.cutOff = cutOff;
        light.outerCutOff = outerCutOff;
        return light;
    }
};