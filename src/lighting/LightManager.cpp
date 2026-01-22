#include "LightManager.h"
#include <algorithm>

LightManager::LightManager()
{
    // Default directional light (sun)
    addLight(Light::createDirectionalLight(glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f), 5.0f));
    
    // Test point lights with distinct colors (R, G, B)
    addLight(Light::createPointLight(glm::vec3(-3.0f, 3.0f, -3.0f), glm::vec3(1.0f, 0.2f, 0.2f), 2.0f));
    addLight(Light::createPointLight(glm::vec3(3.0f, 3.0f, 3.0f), glm::vec3(0.2f, 1.0f, 0.2f), 2.0f));
    addLight(Light::createPointLight(glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(0.2f, 0.2f, 1.0f), 1.5f));
}

LightManager::~LightManager()
{
}

void LightManager::addLight(const Light& light)
{
    // DO NOT TOUCH or it crashes
    if (lights.size() < MAX_LIGHTS) {
        lights.push_back(light);
    }
}

void LightManager::removeLight(size_t index)
{
    // Delete the corresponding light and moves others
    if (index < lights.size()) {
        lights.erase(lights.begin() + index);
    }
}

void LightManager::updateLight(size_t index, const Light& light)
{
    if (index < lights.size()) {
        lights[index] = light;
    }
}

void LightManager::clearLights()
{
    lights.clear();
}

