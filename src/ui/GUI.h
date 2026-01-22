#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "../renderer/Renderer.h"
#include "../camera/Camera.h"

class GUI
{
public:
    GUI(GLFWwindow* window);
    ~GUI();

    // Main render loop for the UI.
    // Must be called after the main scene render but before buffer swap.
    void render();
    
    // Dependency Injection
    void setRenderer(Renderer* rendererPtr) { this->renderer = rendererPtr; }
    void setCamera(Camera* cameraPtr) { this->camera = cameraPtr; }

    // Sub-window render functions
    void renderMainMenu();
    void renderPerformanceWindow();
    void renderLightingWindow();
    void renderMaterialParamsWindow();
    void renderGlobalParamsWindow();
    void renderShadowsWindow();
    void renderPresetsWindow();

private:
    GLFWwindow* window;    // Handle to the OS window (for input forwarding)
    Renderer* renderer;    // Reference to the renderer
    Camera* camera = nullptr; // Reference to camera for position display

    // UI states
    bool showPerformance = true;
    bool showLighting = false;
    bool showMaterialParams = false;
    bool showGlobalParams = false;
    bool showShadows = false;
    bool showPresets = false;
};
