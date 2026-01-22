#pragma once

#include <vector>
#include <memory>
#include "Light.h"

class LightManager
{
public:
    LightManager();
    ~LightManager();

    // Add a new light.
    void addLight(const Light& light);
    
    // Remove a light with index.
    void removeLight(size_t index);
    
    // Modify light properties (color, position, ...).
    void updateLight(size_t index, const Light& light);
    
    // Remove all lights.
    void clearLights();

    const std::vector<Light>& getLights() const { return lights; }
    std::vector<Light>& getLightsReference() { return lights; } // Direct access for dynamic updates (e.g. flickering effects).
    size_t getLightCount() const { return lights.size(); }
    Light& getLight(size_t index) { return lights[index]; }


private:
    std::vector<Light> lights;
    
    // This is the maximum lights but not the maximum light to cast shadow. that is 8.
    static const size_t MAX_LIGHTS = 32;
};