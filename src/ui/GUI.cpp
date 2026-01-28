//GUI Implementation
#include "GUI.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <iostream>

GUI::GUI(GLFWwindow* window) : window(window), renderer(nullptr)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 460");
}

GUI::~GUI()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void GUI::render()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    renderMainMenu();
    if (renderer) {
        if (showPerformance) renderPerformanceWindow();
        if (showLighting) renderLightingWindow();
        if (showMaterialParams) renderMaterialParamsWindow();
        if (showGlobalParams) renderGlobalParamsWindow();
        if (showShadows) renderShadowsWindow();
        if (showPresets) renderPresetsWindow();
    }
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void GUI::renderMainMenu()
{
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Windows")) {
            ImGui::MenuItem("Performance", nullptr, &showPerformance);
            ImGui::MenuItem("Lighting", nullptr, &showLighting);
            ImGui::MenuItem("Material Parameters", nullptr, &showMaterialParams);
            ImGui::MenuItem("Global Parameters", nullptr, &showGlobalParams);
            ImGui::MenuItem("Shadows", nullptr, &showShadows);
            ImGui::MenuItem("Presets", nullptr, &showPresets);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

// FPS counter and basic stats.
void GUI::renderPerformanceWindow()
{
    ImGui::SetNextWindowPos(ImVec2(10, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 150), ImGuiCond_FirstUseEver);
    ImGui::Begin("Performance", &showPerformance);
    
    // Frametime and FPS
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 
                1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
    
    // Renderer stats
    const auto& stats = renderer->getStats();
    ImGui::Text("Vertices: %u", stats.vertexCount);
    ImGui::Text("Draw calls: %u", stats.drawCalls);
    
    if (camera) {
        ImGui::Separator();
        ImGui::Text("Camera POS: X:%.2f Y:%.2f Z:%.2f", camera->Position.x, camera->Position.y, camera->Position.z);
        ImGui::Text("Yaw: %.1f Pitch: %.1f", camera->Yaw, camera->Pitch);
    }
    
    ImGui::Separator();
    ImGui::Text("Press TAB to toggle GUI/Camera mode");
    
    ImGui::End();
}

// Lights
void GUI::renderLightingWindow()
{
    if (!renderer) return;
    
    ImGui::SetNextWindowPos(ImVec2(300, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 450), ImGuiCond_FirstUseEver);
    ImGui::Begin("Lighting", &showLighting);
    
    auto& lightManager = renderer->getLightManager();
    
    // Iterate through all lights
    for (size_t i = 0; i < lightManager.getLightCount(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        
        auto& light = lightManager.getLight(i);
        
        std::string headerLabel = "Light " + std::to_string(i);
        
        const char* typeIndicator = "";
        switch(light.type) {
            case LightType::DIRECTIONAL: typeIndicator = " [DIR]"; break;
            case LightType::POINT: typeIndicator = " [POINT]"; break;
            case LightType::SPOT: typeIndicator = " [SPOT]"; break;
        }
        headerLabel += typeIndicator;
        
        if (ImGui::CollapsingHeader(headerLabel.c_str())) {
            ImGui::Checkbox("Cast Shadows", &light.castShadows);
            ImGui::Separator();
            
            const char* lightTypes[] = { "Directional", "Point", "Spot" };
            int currentType = static_cast<int>(light.type);
            if (ImGui::Combo("Type", &currentType, lightTypes, 3)) {
                light.type = static_cast<LightType>(currentType);
            }
            
            if (light.type != LightType::DIRECTIONAL) {
                ImGui::SliderFloat3("Position", &light.position.x, -10.0f, 10.0f);
            }
            
            if (light.type != LightType::POINT) {
                ImGui::SliderFloat3("Direction", &light.direction.x, -1.0f, 1.0f);
            }
            
            ImGui::ColorEdit3("Color", &light.color.x);
            
            // Special Effects Controls
            ImGui::Checkbox("Fire Flicker", &light.flicker);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Varies intensity and color to simulate fire");
            
            ImGui::SameLine();
            ImGui::Checkbox("Static (Cache Shadows)", &light.isStatic);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Assume fixed position for shadow optimization");
            
            ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 5.0f);
            
            // Attenuation for point and spot lights
            // TODO: Using standard quadratic attenuation: 1 / (c + l*d + q*d^2) but i may change this later
            if (light.type != LightType::DIRECTIONAL) {
                if (ImGui::TreeNode("Attenuation")) {
                    ImGui::SliderFloat("Constant", &light.constant, 0.1f, 2.0f);
                    ImGui::SliderFloat("Linear", &light.linear, 0.01f, 1.0f);
                    ImGui::SliderFloat("Quadratic", &light.quadratic, 0.001f, 1.0f);
                    ImGui::TreePop();
                }
            }
            
            // Cone parameters for spotlights
            if (light.type == LightType::SPOT) {
                if (ImGui::TreeNode("Spot Parameters")) {
                    ImGui::SliderFloat("Cut Off", &light.cutOff, 1.0f, 45.0f);
                    ImGui::SliderFloat("Outer Cut Off", &light.outerCutOff, light.cutOff, 45.0f);
                    ImGui::TreePop();
                }
            }
            
            if (ImGui::Button("Remove Light")) {
                lightManager.removeLight(i);
                ImGui::PopID(); // Must pop here before break because loop terminates
                break;
            }
        }
        
        ImGui::PopID(); // Standard pop
    }
    
    ImGui::Separator();
    
    // Buttons to add every type of light
    if (ImGui::Button("Add Point Light")) {
        lightManager.addLight(Light::createPointLight(glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(1.0f), 1.0f));
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Directional Light")) {
        lightManager.addLight(Light::createDirectionalLight(glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(1.0f), 0.5f));
    }
    if (ImGui::Button("Add Spot Light")) {
        lightManager.addLight(Light::createSpotLight(glm::vec3(0.0f, 3.0f, 0.0f), 
                                                    glm::vec3(0.0f, -1.0f, 0.0f), 
                                                    glm::vec3(1.0f), 12.5f, 15.0f, 1.0f));
    }
    
    ImGui::End();
}

// Per-model material tweaking. Modifies MaterialParams directly that is uploaded to shader per object.
void GUI::renderMaterialParamsWindow()
{
    if (!renderer) return;
    
    ImGui::SetNextWindowPos(ImVec2(660, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 600), ImGuiCond_FirstUseEver);
    ImGui::Begin("Material Parameters", &showMaterialParams);
    
    auto& globalParams = renderer->materialParams;
    
    ImGui::Text("Per-Model Material Parameters");
    ImGui::Separator();
    
    // Lambda function to avoidd huge code duplication
    auto renderModelMaterial = [&](Renderer::ModelMaterial& material, const char* name, bool hasTexture) {
        if (ImGui::CollapsingHeader(name, ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushID(name);
            
            // If no texture we can choose albedo color here.
            if (!hasTexture) {
                ImGui::ColorEdit3("Albedo Tint", &material.params.albedo.x);
            }
            
            //TODO: verify if this workaround for dark illumination models is okay
            ImGui::SliderFloat("Intensity Correction", &material.params.intensityCorrection, 0.1f, 5.0f);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Multiplier for base color/texture brightness");

            // Illumination Model Selector
            const char* illuminationModels[] = { "Lambertian", "Minnaert", "Oren-Nayar", "Ashikhmin-Shirley", "Cook-Torrance" };
            int currentModel = static_cast<int>(material.model);
            if (ImGui::Combo("Illumination Model", &currentModel, illuminationModels, 5)) {
                material.model = static_cast<IlluminationModel>(currentModel);
            }

            // Per-model parameter display thatonly show parameters for the selected illumination model.
            if (material.model == IlluminationModel::LAMBERTIAN) {
                ImGui::SliderFloat("Specular Shininess", &material.params.specularShininess, 1.0f, 256.0f);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Higher = smaller, sharper highlights");
            } else if (material.model == IlluminationModel::MINNAERT) {
                 ImGui::SliderFloat("Roughness (k)", &material.params.minnaertK, 0.0f, 2.0f);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Controls limb darkening/brightening. 1.0 = Lambertian, <1.0 = Velvet");
            } else if (material.model == IlluminationModel::OREN_NAYAR) {
                ImGui::SliderFloat("Roughness", &material.params.orenNayarRoughness, 0.0f, 1.0f);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Surface roughness. Higher = flatter look");
            } else if (material.model == IlluminationModel::ASHIKHMIN_SHIRLEY) {
                ImGui::SliderFloat("Anisotropic Nu", &material.params.ashikhminShirleyNu, 1.0f, 1000.0f);
                ImGui::SliderFloat("Anisotropic Nv", &material.params.ashikhminShirleyNv, 1.0f, 1000.0f);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Higher values = sharper, more stretched highlights");
            } else if (material.model == IlluminationModel::COOK_TORRANCE) {
                ImGui::SliderFloat("Roughness (m)", &material.params.cookTorranceRoughness, 0.01f, 1.0f);
                ImGui::SliderFloat("Fresnel (F0)", &material.params.cookTorranceF0, 0.0f, 1.0f);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Roughness: highlight spread\nF0: reflectivity at normal incidence");
            }
            
            ImGui::PopID();
            ImGui::Spacing();
        }
    };
    
    ImGui::Separator();
    
    // Materials are divided into dropdown groups so taht ui is not a total mess
    
    if (ImGui::CollapsingHeader("Architecture", ImGuiTreeNodeFlags_DefaultOpen)) {
        renderModelMaterial(renderer->wallMaterial, "Walls", true);
        renderModelMaterial(renderer->ceilingMaterial, "Second Floor Ceiling", true);
        renderModelMaterial(renderer->floorMaterial, "First Floor (Stone)", true);
        renderModelMaterial(renderer->woodFloorMaterial, "Second Floor (Wood)", true);
        renderModelMaterial(renderer->stairMaterial, "Stairs", true);
        renderModelMaterial(renderer->dirtMaterial, "Dirt", true);
    }
    
    if (ImGui::CollapsingHeader("Decorations", ImGuiTreeNodeFlags_DefaultOpen)) {
        renderModelMaterial(renderer->torchMaterial, "Torch", true);
        renderModelMaterial(renderer->tableMaterial, "Table", true);
        renderModelMaterial(renderer->chairMaterial, "Chair", true);
        renderModelMaterial(renderer->stoolMaterial, "Stool", true);
        renderModelMaterial(renderer->bedMaterial, "Bed", true);
        renderModelMaterial(renderer->chestMaterial, "Chest", true);
        renderModelMaterial(renderer->bannerMaterial, "Banner", true);
        renderModelMaterial(renderer->swordShieldMaterial, "Sword & Shield", true);
        renderModelMaterial(renderer->barrelMaterial, "Barrel", true);
        renderModelMaterial(renderer->crateMaterial, "Crates", true);
        renderModelMaterial(renderer->shelfMaterial, "Shelf", true);
        renderModelMaterial(renderer->candleMaterial, "Candles", true);
        renderModelMaterial(renderer->woodPalletMaterial, "Wood Pallet", true);
        
        ImGui::Text("Pallet Resources");
        renderModelMaterial(renderer->woodPlanksMaterial, "Wood Planks", true);
        renderModelMaterial(renderer->stoneStackMaterial, "Stone Stack", true);
        renderModelMaterial(renderer->goldBarsMaterial, "Gold Bars", true);
        renderModelMaterial(renderer->metalPartsMaterial, "Metal Parts", true);
        renderModelMaterial(renderer->textilesMaterial, "Textiles", true);
    }
    
    ImGui::End();
}

// Global Parameters that control whether toon effect and/or edge detection are enabled.
void GUI::renderGlobalParamsWindow()
{
    if (!renderer) return;
    
    ImGui::SetNextWindowPos(ImVec2(660, 500), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiCond_FirstUseEver);
    ImGui::Begin("Global Parameters", &showGlobalParams);
    
    auto& globalParams = renderer->materialParams;
    auto& edgeParams = renderer->edgeParams;
    int edgeFlags = renderer->getEdgeDetectionFlags();

    // Toon shading parameters.
    if (ImGui::Checkbox("Crazy Mode", &renderer->isCrazyMode)) {
        renderer->setCrazyMode(renderer->isCrazyMode);
    }
    
    ImGui::Checkbox("Enable Toon Shading", &globalParams.enableQuantization);

    if (globalParams.enableQuantization) {
        if (ImGui::CollapsingHeader("Toon Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
             ImGui::SliderInt("Diffuse Bands", &globalParams.diffuseQuantizationBands, 3, 8);
             ImGui::Text("Specular Highlights (3 levels: Off/Medium/Bright):");
             ImGui::SliderFloat("Off->Medium Threshold", &globalParams.specularThreshold1, 0.1f, 0.5f);
             ImGui::SliderFloat("Medium->Bright Threshold", &globalParams.specularThreshold2, 0.5f, 0.9f);
        }
    }

    ImGui::Separator();

    // Edge detection parameters.
    ImGui::Checkbox("Enable Edge Detection", &edgeParams.enableOutlining);

    if (edgeParams.enableOutlining) {
        if (ImGui::CollapsingHeader("Edge Detection Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            // Edge detection toggles
            // bitflags to quickly know which effect to enable in shader.
            bool depthBased = (edgeFlags & static_cast<int>(EdgeDetectionType::DEPTH_BASED)) != 0;
            bool normalBased = (edgeFlags & static_cast<int>(EdgeDetectionType::NORMAL_BASED)) != 0;
            bool sobel = (edgeFlags & static_cast<int>(EdgeDetectionType::SOBEL)) != 0;
            bool colorBased = (edgeFlags & static_cast<int>(EdgeDetectionType::COLOR_BASED)) != 0;
            bool laplacian = (edgeFlags & static_cast<int>(EdgeDetectionType::LAPLACIAN)) != 0;
            
            ImGui::Text("Techniques:");
            if (ImGui::Checkbox("Depth-based", &depthBased)) {
                if (depthBased) edgeFlags |= static_cast<int>(EdgeDetectionType::DEPTH_BASED);
                else edgeFlags &= ~static_cast<int>(EdgeDetectionType::DEPTH_BASED);
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("Normal-based", &normalBased)) {
                if (normalBased) edgeFlags |= static_cast<int>(EdgeDetectionType::NORMAL_BASED);
                else edgeFlags &= ~static_cast<int>(EdgeDetectionType::NORMAL_BASED);
            }
            
            if (ImGui::Checkbox("Sobel", &sobel)) {
                if (sobel) edgeFlags |= static_cast<int>(EdgeDetectionType::SOBEL);
                else edgeFlags &= ~static_cast<int>(EdgeDetectionType::SOBEL);
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("Color-based", &colorBased)) {
                if (colorBased) edgeFlags |= static_cast<int>(EdgeDetectionType::COLOR_BASED);
                else edgeFlags &= ~static_cast<int>(EdgeDetectionType::COLOR_BASED);
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("Laplacian", &laplacian)) {
                 if (laplacian) edgeFlags |= static_cast<int>(EdgeDetectionType::LAPLACIAN);
                 else edgeFlags &= ~static_cast<int>(EdgeDetectionType::LAPLACIAN);
            }
            
            renderer->setEdgeDetectionFlags(edgeFlags);
            
            ImGui::Spacing();
            ImGui::Text("Thresholds:");
            // Tweak sensitivity for each algorithm
            ImGui::SliderFloat("Depth Threshold", &edgeParams.depthThreshold, 0.001f, 1.0f);
            ImGui::SliderFloat("Normal Threshold", &edgeParams.normalThreshold, 0.1f, 1.0f);
            ImGui::SliderFloat("Sobel Threshold", &edgeParams.sobelThreshold, 0.01f, 1.0f);
            ImGui::SliderFloat("Color Threshold", &edgeParams.colorThreshold, 0.01f, 1.0f);
            ImGui::SliderFloat("Laplacian Threshold", &edgeParams.laplacianThreshold, 0.01f, 1.0f);
            ImGui::SliderFloat("Laplacian Scale", &edgeParams.laplacianScale, 0.1f, 100.0f);
            ImGui::ColorEdit3("Edge Color", &edgeParams.edgeColor.x);
            
            ImGui::Separator();
            ImGui::Text("Advanced:");
            ImGui::SliderFloat("Depth Exponent", &edgeParams.depthExponent, 0.1f, 5.0f);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Controls depth linearization. 1.0 = Linear/Raw");
            
            ImGui::SliderFloat("Normal Split", &edgeParams.normalSplit, 0.0f, 1.0f);
             if (ImGui::IsItemHovered()) ImGui::SetTooltip("Threshold between Dot Product vs Sobel for Normals");

            ImGui::SliderFloat("Sobel Scale", &edgeParams.sobelScale, 0.1f, 5.0f);
            ImGui::SliderFloat("Smooth Width", &edgeParams.smoothWidth, 0.0f, 5.0f);
             if (ImGui::IsItemHovered()) ImGui::SetTooltip("Controls anti-aliasing width (pixels)");
        }
    }
    
    ImGui::End();
}


/*
void GUI::renderEdgeDetectionWindow()
{
    // Edge detection is now in Global Params
}
*/

// Shadow settings window.
// Controls shadow map resolution, filtering, and bias.
void GUI::renderShadowsWindow()
{
    if (!renderer) return;
    
    ImGui::SetNextWindowPos(ImVec2(1070, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 480), ImGuiCond_FirstUseEver);
    ImGui::Begin("Shadow Settings", &showShadows);
    
    auto& shadowParams = renderer->shadowParams;
    
    // Shadow map resolution.
    if (ImGui::CollapsingHeader("Shadow Map Resolution", ImGuiTreeNodeFlags_DefaultOpen)) {
        
        const char* resolutions[] = { "512", "1024", "2048", "4096" };
        int currentDirRes = 0;
        if (shadowParams.shadowMapSize == 512) currentDirRes = 0;
        else if (shadowParams.shadowMapSize == 1024) currentDirRes = 1;
        else if (shadowParams.shadowMapSize == 2048) currentDirRes = 2;
        else if (shadowParams.shadowMapSize == 4096) currentDirRes = 3;
        
        if (ImGui::Combo("Dir/Spot Resolution", &currentDirRes, resolutions, 4)) {
            shadowParams.shadowMapSize = 512 * (1 << currentDirRes);
        }
        
        const char* cubeResolutions[] = { "256", "512", "1024", "2048" };
        int currentCubeRes = 0;
        if (shadowParams.cubeShadowMapSize == 256) currentCubeRes = 0;
        else if (shadowParams.cubeShadowMapSize == 512) currentCubeRes = 1;
        else if (shadowParams.cubeShadowMapSize == 1024) currentCubeRes = 2;
        else if (shadowParams.cubeShadowMapSize == 2048) currentCubeRes = 3;
        
        // Point lights use Cubemaps, so memory cost is very high compared to 2D maps.
        if (ImGui::Combo("Point Light Resolution", &currentCubeRes, cubeResolutions, 4)) {
            shadowParams.cubeShadowMapSize = 256 * (1 << currentCubeRes);
        }
        
        ImGui::Text("Current: Dir/Spot %dx%d", shadowParams.shadowMapSize, shadowParams.shadowMapSize);
        ImGui::Text("Current: Point %dx%d (x6 faces)", shadowParams.cubeShadowMapSize, shadowParams.cubeShadowMapSize);
    }
    
    ImGui::Separator();
    
    // PCF shadows implementation
    if (ImGui::CollapsingHeader("Shadow Quality", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enable PCF (Soft Shadows)", &shadowParams.enablePCF);
        
        if (shadowParams.enablePCF) {
            ImGui::SliderInt("PCF Kernel Size", &shadowParams.shadowPCFSamples, 0, 4);
            int kernelSize = shadowParams.shadowPCFSamples * 2 + 1;
            ImGui::Text("Sampling %dx%d kernel", kernelSize, kernelSize);
            
            if (shadowParams.shadowPCFSamples == 0) {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "No filtering");
            } else if (shadowParams.shadowPCFSamples == 1) {
                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Balanced");
            } else if (shadowParams.shadowPCFSamples >= 3) {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "High quality, low FPS");
            }
        } else {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Low quality, high FPS");
        }
    }
    
    ImGui::Separator();
    
    // Shadow intensity
    if (ImGui::CollapsingHeader("Shadow Appearance", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Shadow Darkness", &shadowParams.shadowIntensity, 0.0f, 1.0f, "%.2f");
        
        if (shadowParams.shadowIntensity < 0.3f) {
            ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.3f, 1.0f), "Very dark shadows");
        } else if (shadowParams.shadowIntensity > 0.8f) {
            ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Very light shadows");
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Balanced shadows");
        }
        
        ImGui::Text("0 = Black shadows, 1 = No shadows");
    }
    
    ImGui::Separator();
    
    // Bias (For fixing shadow acne)
    if (ImGui::CollapsingHeader("Bias Settings")) {
        ImGui::Text("Adjust to fix shadow acne");
        
        ImGui::SliderFloat("Depth Bias", &shadowParams.shadowBias, 0.0001f, 0.05f, "%.5f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Increase if you see shadow acne\nDecrease if shadows detach from objects");
        }
        
        ImGui::SliderFloat("Normal Bias", &shadowParams.shadowNormalBias, 0.001f, 0.1f, "%.4f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Offsets shadow based on surface normal\nHelps with curved surfaces");
        }
    }
    
    ImGui::Separator();
    
    // Memory usage estimate
    if (ImGui::CollapsingHeader("Memory usage for shadows")) {
        auto& lightManager = renderer->getLightManager();
        size_t lightCount = lightManager.getLightCount();
        
        size_t dirLights = 0, pointLights = 0, spotLights = 0;
        for (size_t i = 0; i < lightCount; ++i) {
            switch (lightManager.getLight(i).type) {
                case LightType::DIRECTIONAL: dirLights++; break;
                case LightType::POINT: pointLights++; break;
                case LightType::SPOT: spotLights++; break;
            }
        }
        size_t memUsage = 0;
        memUsage += (dirLights + spotLights) * shadowParams.shadowMapSize * shadowParams.shadowMapSize * 4; // depth buffer bytes
        memUsage += pointLights * 6 * shadowParams.cubeShadowMapSize * shadowParams.cubeShadowMapSize * 4;
        
        float memUsageMB = memUsage / (1024.0f * 1024.0f);
        ImGui::Text("Estimation: %.1f MB", memUsageMB);
    }
    
    ImGui::Separator();
    
    ImGui::End();
}

// Save/Load configuration presets.
// TODO: basic JSON serialization for persistance
void GUI::renderPresetsWindow()
{
    if (!renderer) return;

    ImGui::SetNextWindowPos(ImVec2(1070, 520), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 250), ImGuiCond_FirstUseEver);
    ImGui::Begin("Presets", &showPresets);

    ImGui::Text("Save/Load Presets");
    ImGui::TextWrapped("Saves: Materials, Shadows, Global.\nExcludes: Lights.");
    ImGui::Separator();

    for (int i = 0; i < 5; ++i) {
        ImGui::PushID(i);
        ImGui::Text("Preset %d", i + 1);
        ImGui::SameLine(80);
        
        if (ImGui::Button("Load")) {
            renderer->loadPreset(i);
        }
        ImGui::SameLine();
        if (ImGui::Button("Save")) {
            renderer->savePreset(i);
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    if (ImGui::Button("Reset All to Defaults", ImVec2(-1, 0))) {
        renderer->resetParamsToDefaults();
    }

    ImGui::End();
}
