// Renderer Implementation
// Shadows -> ShadowMap Shader
// Geometry -> Geometry Shader
// Lighting -> Hybrid Cel Shader
// Edges -> Edge Detection Shader
// Composite -> Composite Shader

#include "Renderer.h"
#include <iostream>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

const size_t Renderer::MAX_SHADOW_CASTING_LIGHTS;

Renderer::Renderer(unsigned int width, unsigned int height) 
    : width(width), height(height), edgeDetectionFlags(static_cast<int>(EdgeDetectionType::DEPTH_BASED)),
      lightingFBO(0), lightingTexture(0), edgeFBO(0), edgeTexture(0), 
      quadVAO(0), quadVBO(0)
{
    // G-Buffer for deferred shading
    gBuffer = std::make_unique<GBuffer>();
    if (!gBuffer->init(width, height)) {
        std::cerr << "Failed to initialize G-Buffer" << std::endl;
    }

    // Initialize the LightManager to handle scene lights.
    lightManager = std::make_unique<LightManager>();
    
    // Initialize framebuffers/textures for intermediate render passes.
    initializeRenderTargets();
    initializeShadowMapping(); // Allocates shadow map textures
    initializeQuad();          // Sets up the fullscreen quad
    
    // Load scene assets.
    initializeLights();
    loadModels();
    initializeShaders();
    
    // Set up material properties for all loaded models.
    initializeModelMaterials();
}

Renderer::~Renderer()
{
    cleanup();
}

void Renderer::render(const Camera& camera)
{
    // Reset frame stats.
    resetStats();
    
    // 1. Shadow Update Pass:
    // Check if lights have moved or changed state, and re-allocate shadow maps if necessary.
    updateShadowMaps();
    
    // Update for flickering.
    updateLights(0.016f);
    
    // 2. Shadow Map Pass - render depth from each light perspective
    shadowMapPass();
    
    // 3. Geometry Pass - fill the G-Buffer.
    geometryPass(camera);
    
    // 4. Lighting Pass - calculate lighting using G-Buffer.
    lightingPass(camera);
    
    // 5. Edge Detection Pass - generate outlines.
    edgeDetectionPass();
    
    // 6. Composite Pass - combine lighting and edges.
    compositePass();
}

void Renderer::resize(unsigned int newWidth, unsigned int newHeight)
{
    width = newWidth;
    height = newHeight;
    
    // Resize G-Buffer textures.
    gBuffer->resize(width, height);
    
    // Reallocate used textures.
    glBindTexture(GL_TEXTURE_2D, lightingTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
    
    // Edge texture.
    glBindTexture(GL_TEXTURE_2D, edgeTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    
    // Update the OpenGL state.
    glViewport(0, 0, width, height);
}

// Compile and link all shader programs.
void Renderer::initializeShaders()
{
    try {
        // Geometry Pass - transforms vertices and outputs to G-Buffer
        geometryShader = std::make_unique<Shader>("assets/shaders/geometry.vert", "assets/shaders/geometry.frag");
        std::cout << "Geometry shader compiled successfully" << std::endl;
        
        // Lighting Pass - deferred lighting on full-screen quad
        hybridCelShader = std::make_unique<Shader>("assets/shaders/quad.vert", "assets/shaders/lighting/hybrid_cel_lighting.frag");
        std::cout << "Hybrid cel shading shader compiled successfully" << std::endl;
        
        // Edge Detection - post-process outline detection
        edgeDetectionShader = std::make_unique<Shader>("assets/shaders/quad.vert", "assets/shaders/edge_detection.frag");
        std::cout << "Edge detection shader compiled successfully" << std::endl;
        
        // Composite - merge lit scene with edges
        compositeShader = std::make_unique<Shader>("assets/shaders/quad.vert", "assets/shaders/composite.frag");
        std::cout << "Composite shader compiled successfully" << std::endl;
        
        // 5. Shadow Shaders
        // Shadow Mapping uses shaders to render depth from light perspective.
        try {
            // Dir/Spot shadow map (depth only)
            shadowMapShader = std::make_unique<Shader>("assets/shaders/shadow_map.vert", "assets/shaders/shadow_map.frag");
            std::cout << "Directional/Spot shadow shader compiled successfully" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Failed to load shadow shaders: " << e.what() << std::endl;
            shadowMapShader = nullptr;
        }
        
        try {
            // Point light cubemap shadow (uses geometry shader for 6-face render)
            pointShadowShader = std::make_unique<Shader>("assets/shaders/point_shadow.vert", 
                                                        "assets/shaders/point_shadow.frag",
                                                        "assets/shaders/point_shadow.geom");
            std::cout << "Point light shadow shader compiled successfully" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Failed to load point shadow shaders: " << e.what() << std::endl;
            pointShadowShader = nullptr;
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to load shaders: " << e.what() << std::endl;
    }
}

// Configure FBOs for intermediate passes.
void Renderer::initializeRenderTargets()
{
    // Lighting FBO - stores lit scene before edge detection
    glGenFramebuffers(1, &lightingFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, lightingFBO);
    
    glGenTextures(1, &lightingTexture);
    glBindTexture(GL_TEXTURE_2D, lightingTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, lightingTexture, 0);
    
    // Edge FBO - stores edge map
    glGenFramebuffers(1, &edgeFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, edgeFBO);
    
    glGenTextures(1, &edgeTexture);
    glBindTexture(GL_TEXTURE_2D, edgeTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, edgeTexture, 0);
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// Pre-allocate shadow map data. GPU resources allocated wheen needed in updateShadowMaps().
void Renderer::initializeShadowMapping()
{
    shadowMaps.resize(MAX_SHADOW_CASTING_LIGHTS);
}

// Check which lights need shadow maps (they have isCastingShadows = true) and allocate them.
void Renderer::updateShadowMaps()
{
    const auto& lights = lightManager->getLights();
    size_t lightCount = std::min(lights.size(), MAX_SHADOW_CASTING_LIGHTS);
    
    // Manage the shadow map.
    for (size_t i = 0; i < lightCount; ++i) {
        const auto& light = lights[i];
        auto& shadowData = shadowMaps[i];
        
        // Point lights need cubemaps, others use 2D maps
        int requiredSize = (light.type == LightType::POINT) ? shadowParams.cubeShadowMapSize : shadowParams.shadowMapSize;

        // Reallocate if type or size changed
        if (!shadowData.isActive || shadowData.type != light.type || shadowData.size != requiredSize || !light.castShadows) {
            if (shadowData.isActive) {
                cleanupShadowMap(shadowData);
            }
            
            if (!light.castShadows) {
                continue;
            }
            glGenFramebuffers(1, &shadowData.FBO);
            
            if (light.type == LightType::POINT) {
                // Cubemap for point lights (6 faces)
                glGenTextures(1, &shadowData.depthCubemap);
                glBindTexture(GL_TEXTURE_CUBE_MAP, shadowData.depthCubemap);
                
                for (unsigned int i = 0; i < 6; ++i) {
                    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_DEPTH_COMPONENT,
                                requiredSize, requiredSize, 0, 
                                GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
                }
                
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
                
                // Attach depth cubemap to the FBO.
                glBindFramebuffer(GL_FRAMEBUFFER, shadowData.FBO);
                glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, shadowData.depthCubemap, 0);
                glDrawBuffer(GL_NONE); // No color buffer needed (depth only)
                glReadBuffer(GL_NONE);
                
            } else {
                // 2D depth texture for directional/spot lights
                glGenTextures(1, &shadowData.depthMap);
                glBindTexture(GL_TEXTURE_2D, shadowData.depthMap);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, 
                            requiredSize, requiredSize, 0, 
                            GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                // Clamp to border with white (max depth = no shadow outside map)
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
                float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f }; // White border = max depth (no shadow outside map)
                glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
                
                glBindFramebuffer(GL_FRAMEBUFFER, shadowData.FBO);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowData.depthMap, 0);
                glDrawBuffer(GL_NONE);
                glReadBuffer(GL_NONE);
            }
            
            shadowData.type = light.type;
            shadowData.size = requiredSize;
            shadowData.isActive = true;
            
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
    }
    
    // Remove unused shadow maps for performance.
    for (size_t i = lightCount; i < shadowMaps.size(); ++i) {
        if (shadowMaps[i].isActive) {
            cleanupShadowMap(shadowMaps[i]);
        }
    }
}

// Execute the shadow render pass for all active lights.
void Renderer::shadowMapPass()
{
    if (!shadowMapShader || !pointShadowShader) return;
    
    const auto& lights = lightManager->getLights();
    size_t lightCount = std::min(lights.size(), MAX_SHADOW_CASTING_LIGHTS);
    
    for (size_t i = 0; i < lightCount; ++i) {
        const auto& light = lights[i];
        auto& shadowData = shadowMaps[i];
        
        if (!shadowData.isActive) continue;
        
        // Skip static lights that have already been rendered
        if (light.isStatic && shadowData.hasRendered) {
             continue;
        }

        renderShadowMapForLight(i, light, shadowData);
        
        shadowData.hasRendered = true;
    }
    
    // Reset viewport to match the screen size for the next pass.
    glViewport(0, 0, width, height);
}

// Dispatcher for specific shadow render functions.
void Renderer::renderShadowMapForLight(size_t lightIndex, const Light& light, ShadowMapData& shadowData)
{
    switch (light.type) {
        case LightType::DIRECTIONAL:
            renderDirectionalShadow(light, shadowData);
            break;
        case LightType::POINT:
            renderPointShadow(light, shadowData);
            break;
        case LightType::SPOT:
            renderSpotShadow(light, shadowData);
            break;
    }
}

// Render shadow map for sun/moon.
void Renderer::renderDirectionalShadow(const Light& light, ShadowMapData& shadowData)
{
    glViewport(0, 0, shadowParams.shadowMapSize, shadowParams.shadowMapSize);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowData.FBO);
    glClear(GL_DEPTH_BUFFER_BIT);
    
    shadowMapShader->use();
    
    // Orthographic projection for parallel light rays
    glm::mat4 lightProjection = glm::ortho(-shadowParams.orthoSize, shadowParams.orthoSize, 
                                           -shadowParams.orthoSize, shadowParams.orthoSize, 
                                           shadowParams.nearPlane, shadowParams.farPlane);
    
    glm::vec3 lightDir = glm::normalize(light.direction);
    // Position the virtual light camera
    glm::vec3 lightPos = -lightDir * (shadowParams.farPlane * 0.5f); 
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    
    // Handle gimbal lock to avoid artifacts
    if (abs(glm::dot(lightDir, up)) > 0.99f) {
        up = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    
    glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0.0f), up);
    shadowData.lightSpaceMatrix = lightProjection * lightView;
    
    shadowMapShader->setMat4("lightSpaceMatrix", shadowData.lightSpaceMatrix);
    
    // Render scene depth from light's view
    renderScene(shadowMapShader.get());
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// Render 6 faces of a cubemap for point lights.
void Renderer::renderPointShadow(const Light& light, ShadowMapData& shadowData)
{
    if (!pointShadowShader) return;
    
    glViewport(0, 0, shadowParams.cubeShadowMapSize, shadowParams.cubeShadowMapSize);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowData.FBO);
    glClear(GL_DEPTH_BUFFER_BIT);
    
    pointShadowShader->use();
    
    // FOV has to be 90 degrees for cubemaps or we will see artifacts.
    float aspect = 1.0f;
    glm::mat4 shadowProj = glm::perspective(glm::radians(90.0f), aspect, 
                                            shadowParams.nearPlane, shadowParams.farPlane);
    
    // Calculate view matrices for all 6 faces of the cube map.
    // +X, -X, +Y, -Y, +Z, -Z
    shadowData.shadowTransforms[0] = shadowProj * glm::lookAt(light.position, 
        light.position + glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));
    shadowData.shadowTransforms[1] = shadowProj * glm::lookAt(light.position, 
        light.position + glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));
    shadowData.shadowTransforms[2] = shadowProj * glm::lookAt(light.position, 
        light.position + glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    shadowData.shadowTransforms[3] = shadowProj * glm::lookAt(light.position, 
        light.position + glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    shadowData.shadowTransforms[4] = shadowProj * glm::lookAt(light.position, 
        light.position + glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, -1.0f, 0.0f));
    shadowData.shadowTransforms[5] = shadowProj * glm::lookAt(light.position, 
        light.position + glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, -1.0f, 0.0f));
    
    // Upload to the Geometry Shader (which replicates the geometry to 6 faces).
    for (unsigned int i = 0; i < 6; ++i) {
        pointShadowShader->setMat4("shadowMatrices[" + std::to_string(i) + "]", shadowData.shadowTransforms[i]);
    }
    pointShadowShader->setVec3("lightPos", light.position);
    pointShadowShader->setFloat("farPlane", shadowParams.farPlane);
    
    renderScene(pointShadowShader.get());
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// Render shadowed scene from spotlight's perspective
void Renderer::renderSpotShadow(const Light& light, ShadowMapData& shadowData)
{
    glViewport(0, 0, shadowParams.shadowMapSize, shadowParams.shadowMapSize);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowData.FBO);
    glClear(GL_DEPTH_BUFFER_BIT);
    
    shadowMapShader->use();
    
    // Perspective projection matching spotlight cone
    float spotAngle = glm::degrees(acos(light.cutOff)) * 2.0f;
    glm::mat4 lightProjection = glm::perspective(glm::radians(spotAngle), 1.0f, 
                                                 shadowParams.nearPlane, shadowParams.farPlane);
    
    glm::vec3 lightTarget = light.position + light.direction;
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    
    if (abs(glm::dot(glm::normalize(light.direction), up)) > 0.99f) {
        up = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    
    glm::mat4 lightView = glm::lookAt(light.position, lightTarget, up);
    shadowData.lightSpaceMatrix = lightProjection * lightView;
    
    shadowMapShader->setMat4("lightSpaceMatrix", shadowData.lightSpaceMatrix);
    
    renderScene(shadowMapShader.get());
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// Static scene setup that draws all objects. Used by shadow pass and geometry pass.
void Renderer::renderScene(Shader* shader, const glm::mat4& viewProjection)
{
    // 5x5 floor grid centered at 0,0
    const int gridSize = 5;
    const float tileSpacing = 4.0f;
    const float startX = -(gridSize - 1) * tileSpacing * 0.5f;
    const float startZ = -(gridSize - 1) * tileSpacing * 0.5f;
    
    for (int x = 0; x < gridSize; ++x) {
        for (int z = 0; z < gridSize; ++z) {
            glm::mat4 tileMatrix = glm::mat4(1.0f);
            tileMatrix = glm::translate(tileMatrix, glm::vec3(
                startX + x * tileSpacing,
                -1.0f, // Floor height
                startZ + z * tileSpacing
            ));
            renderModel(floorTileModel, tileMatrix, shader);
        }
    }

    // Walls and corners at +/- 10.0
    float wallY = -1.0f;
    float wallOffset = 10.0f; 
    float cornerOffset = 10.0f; 
    
    // -10, -10
    glm::mat4 m = glm::mat4(1.0f);
    m = glm::translate(m, glm::vec3(-cornerOffset, wallY, -cornerOffset));
    m = glm::rotate(m, glm::radians(90.0f), glm::vec3(0,1,0));
    renderModel(cornerModel, m, shader);
    
    // 10, -10
    m = glm::mat4(1.0f);
    m = glm::translate(m, glm::vec3(cornerOffset, wallY, -cornerOffset));
    renderModel(cornerModel, m, shader);
    
    // -10, 10
    m = glm::mat4(1.0f);
    m = glm::translate(m, glm::vec3(-cornerOffset, wallY, cornerOffset));
    m = glm::rotate(m, glm::radians(180.0f), glm::vec3(0,1,0));
    renderModel(cornerModel, m, shader);
    
    // 10, 10
    m = glm::mat4(1.0f);
    m = glm::translate(m, glm::vec3(cornerOffset, wallY, cornerOffset));
    m = glm::rotate(m, glm::radians(-90.0f), glm::vec3(0,1,0));
    renderModel(cornerModel, m, shader);
    
    // Position of center of walls between corners (4 per side)
    float xPositions[] = { -6.0f, -2.0f, 2.0f, 6.0f };
    
    // -Z Wall
    for (int i = 0; i < 4; ++i) {
        glm::vec3 pos(xPositions[i], wallY, -wallOffset);
        glm::mat4 m = glm::mat4(1.0f);
        m = glm::translate(m, pos);
        
        // Door, Window, or Wall
        if (i == 2) {
             renderModel(doorwayModel, m, shader);
        } else if (i == 1) {
             renderModel(windowOpenModel, m, shader);
        } else {
             renderModel(wallModel, m, shader);
        }
        
        // Add a torch to the first wall segment
        if (i == 0) {
            glm::mat4 t = glm::mat4(1.0f);
            t = glm::translate(t, pos + glm::vec3(0.0f, 2.3f, 0.4f)); 
            renderModel(torchModel, t, shader);
        }
    }
    
    // +Z Wall rotated 180 degrees
    for (int i = 0; i < 4; ++i) {
        glm::vec3 pos(xPositions[i], wallY, wallOffset);
        glm::mat4 m = glm::mat4(1.0f);
        m = glm::translate(m, pos);
        m = glm::rotate(m, glm::radians(180.0f), glm::vec3(0,1,0));
        
        if (i == 0) {
            renderModel(windowClosedModel, m, shader);
        } else if (i == 2) {
            renderModel(windowOpenModel, m, shader);
        } else {
            renderModel(wallModel, m, shader);
        }
    }
    
    // -X Wall rotated 90 degrees
    for (int i = 0; i < 4; ++i) {
        glm::vec3 pos(-wallOffset, wallY, xPositions[i]);
        glm::mat4 m = glm::mat4(1.0f);
        m = glm::translate(m, pos);
        m = glm::rotate(m, glm::radians(90.0f), glm::vec3(0,1,0));
        
        if (i == 1) {
            renderModel(windowOpenModel, m, shader);
        } else {
            renderModel(wallModel, m, shader);
        }
        
        if (i == 0) {
            glm::mat4 t = m;
            t = glm::translate(t, glm::vec3(0.0f, 2.3f, 0.4f)); 
            renderModel(torchModel, t, shader);
        }
    }
    
    // +X Wall rotated -90 degrees
    for (int i = 0; i < 4; ++i) {
        glm::vec3 pos(wallOffset, wallY, xPositions[i]);
        glm::mat4 m = glm::mat4(1.0f);
        m = glm::translate(m, pos);
        m = glm::rotate(m, glm::radians(-90.0f), glm::vec3(0,1,0));
        
        if (i == 2) {
            renderModel(windowClosedModel, m, shader);
        } else {
            renderModel(wallModel, m, shader);
        }
        
        if (i == 3) {
             glm::mat4 t = m;
             t = glm::translate(t, glm::vec3(0.0f, 2.3f, 0.4f));
             renderModel(torchModel, t, shader);
        }
    }
    
    // Second Floor
    float floor2Y = wallY + 4.0f; // Because wall models are exactly 4 units tall

    // 1. Ceiling
    // Skip the corner tiles (X=4, Z=4) to create a space for the stairs.
    for (int x = 0; x < gridSize; ++x) {
        for (int z = 0; z < gridSize; ++z) {
            if (x == 4 && (z == 4 || z == 3)) continue;
            glm::mat4 tileMatrix = glm::mat4(1.0f);
            tileMatrix = glm::translate(tileMatrix, glm::vec3(
                startX + x * tileSpacing,
                floor2Y, 
                startZ + z * tileSpacing
            ));
            renderModel(ceilingModel, tileMatrix, shader);
        }
    }

    // 2. Wood Floor
    // Placed on top of the ceiling tiles (y + 0.1) to avoid z-fighting.
    for (int x = 0; x < gridSize; ++x) {
        for (int z = 0; z < gridSize; ++z) {
            if (x == 4 && (z == 4 || z == 3)) continue;
            glm::mat4 tileMatrix = glm::mat4(1.0f);
            tileMatrix = glm::translate(tileMatrix, glm::vec3(
                startX + x * tileSpacing,
                floor2Y + 0.1f, 
                startZ + z * tileSpacing
            ));
            renderModel(woodFloorModel, tileMatrix, shader);
        }
    }
    
    // 3. Stairs
    glm::mat4 stairMatrix = glm::mat4(1.0f);
    glm::vec3 stairPos(8.0f, -1.0f, 10.0f);
    glm::vec3 stairAxis(0.0f, 1.0f, 0.0f);
    stairMatrix = glm::translate(stairMatrix, stairPos);
    stairMatrix = glm::rotate(stairMatrix, glm::radians(180.0f), stairAxis); 
    renderModel(stairModel, stairMatrix, shader);
    
    // 4. Second Floor Room
    for(int i=0; i<2; ++i) {
        glm::mat4 m = glm::mat4(1.0f);
        float xPos = 2.0f + (float)i * 4.0f;
        glm::vec3 pos(xPos, floor2Y, 2.0f);
        m = glm::translate(m, pos);
        renderModel(wallModel, m, shader);
    }
    
    for(int i=0; i<2; ++i) {
        glm::mat4 m = glm::mat4(1.0f);
        float xPos = 2.0f + (float)i * 4.0f;
        glm::vec3 pos(xPos, floor2Y, 10.0f);
        glm::vec3 axis(0.0f, 1.0f, 0.0f);
        m = glm::translate(m, pos);
        m = glm::rotate(m, glm::radians(180.0f), axis);
        renderModel(wallModel, m, shader);
        
        if (i == 1) {
            glm::mat4 t = m;
            t = glm::translate(t, glm::vec3(0.0f, 2.3f, 0.4f)); 
            renderModel(torchModel, t, shader);
        }
    }
    
    {
        glm::mat4 m = glm::mat4(1.0f);
        glm::vec3 pos(-2.0f, floor2Y, 6.0f);
        glm::vec3 axis(0.0f, 1.0f, 0.0f);
        m = glm::translate(m, pos);
        m = glm::rotate(m, glm::radians(90.0f), axis);
        renderModel(wallModel, m, shader);
    }
    
    {
        glm::mat4 m = glm::mat4(1.0f);
        glm::vec3 pos(10.0f, floor2Y, 6.0f);
        glm::vec3 axis(0.0f, 1.0f, 0.0f);
        m = glm::translate(m, pos);
        m = glm::rotate(m, glm::radians(-90.0f), axis);
        renderModel(windowOpenModel, m, shader);
    }
    
    // 5. Corners for Second Floor
    glm::mat4 m2 = glm::mat4(1.0f);
    {
        glm::vec3 pos(-2.0f, floor2Y, 2.0f);
        glm::vec3 axis(0.0f, 1.0f, 0.0f);
        m2 = glm::translate(m2, pos);
        m2 = glm::rotate(m2, glm::radians(90.0f), axis);
        renderModel(cornerModel, m2, shader);
    }
    
    m2 = glm::mat4(1.0f);
    {
        glm::vec3 pos(10.0f, floor2Y, 2.0f);
        m2 = glm::translate(m2, pos);
        renderModel(cornerModel, m2, shader);
    }
    
    m2 = glm::mat4(1.0f);
    {
        glm::vec3 pos(-2.0f, floor2Y, 10.0f);
        glm::vec3 axis(0.0f, 1.0f, 0.0f);
        m2 = glm::translate(m2, pos);
        m2 = glm::rotate(m2, glm::radians(180.0f), axis);
        renderModel(cornerModel, m2, shader);
    }
    
    m2 = glm::mat4(1.0f);
    {
        glm::vec3 pos(10.0f, floor2Y, 10.0f);
        glm::vec3 axis(0.0f, 1.0f, 0.0f);
        m2 = glm::translate(m2, pos);
        m2 = glm::rotate(m2, glm::radians(-90.0f), axis);
        renderModel(cornerModel, m2, shader);
    }
    
    float ceilingHeight = floor2Y + 4.0f;
    for (int x = 2; x <= 4; ++x) {
        for (int z = 3; z <= 4; ++z) {
            glm::mat4 tileMatrix = glm::mat4(1.0f);
            tileMatrix = glm::translate(tileMatrix, glm::vec3(
                startX + x * tileSpacing,
                ceilingHeight, 
                startZ + z * tileSpacing
            ));
            renderModel(ceilingModel, tileMatrix, shader);
        }
    }
    
    // 7. Extra Torches beacuse the scene was too dark
    {
            glm::mat4 t = glm::mat4(1.0f);
            t = glm::translate(t, glm::vec3(-9.6f, 1.3f, 6.0f)); 
            renderModel(torchModel, t, shader);
    }
    {
            glm::vec3 pos(-2.0f, floor2Y, 6.0f);
            glm::mat4 m = glm::mat4(1.0f); 
            m = glm::translate(m, pos);
            m = glm::rotate(m, glm::radians(90.0f), glm::vec3(0,1,0));
            
            glm::mat4 t = m;
            t = glm::translate(t, glm::vec3(0.0f, 2.3f, 0.4f));
            renderModel(torchModel, t, shader);
    }

    // Terrain outside building (procedural-ish)
    for (float x = -22.0f; x < 22.0f; x += 4.0f) {
        for (float z = -22.0f; z < 22.0f; z += 4.0f) {
            // Skip existing stone floor area (from -10 to 10)
            if (x >= -10.0f && x < 10.0f && z >= -10.0f && z < 10.0f) continue;

            int seed = (int)(x * 31.0f + z * 17.0f); // random values that I hardcoded
            int choice = std::abs(seed) % 100;

            glm::mat4 model;
            if (choice < 40) {
                 model = glm::mat4(1.0f);
                 model = glm::translate(model, glm::vec3(x + 2.0f, -1.0f, z + 2.0f));
                 renderModel(floorDirtLargeModel, model, shader);
            } else if (choice < 70) {
                 model = glm::mat4(1.0f);
                 model = glm::translate(model, glm::vec3(x + 2.0f, -1.0f, z + 2.0f));
                 renderModel(floorDirtLargeRockyModel, model, shader);
            } else {
                 // 4 Small tiles per grid cell
                 for (float sx = 0.0f; sx < 4.0f; sx += 2.0f) {
                     for (float sz = 0.0f; sz < 4.0f; sz += 2.0f) {
                         int subSeed = (int)((x + sx) * 53.0f + (z + sz) * 29.0f);
                         int tileType = std::abs(subSeed) % 5;
                         
                         glm::mat4 subModel = glm::mat4(1.0f);
                         subModel = glm::translate(subModel, glm::vec3(x + sx + 1.0f, -1.0f, z + sz + 1.0f));
                         
                         if (tileType == 0) renderModel(floorDirtSmallAModel, subModel, shader);
                         else if (tileType == 1) renderModel(floorDirtSmallBModel, subModel, shader);
                         else if (tileType == 2) renderModel(floorDirtSmallCModel, subModel, shader);
                         else if (tileType == 3) renderModel(floorDirtSmallDModel, subModel, shader);
                         else renderModel(floorDirtSmallWeedsModel, subModel, shader);
                     }
                 }
            }
        }
    }
    
    renderDecorations(shader);
}

// Helpers for fullscreen post-processing.
void Renderer::initializeQuad()
{
    float quadVertices[] = {
        -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
         1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
    };
    
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
}

// Geometry Pass - fill G-Buffer with position, normals, albedo, depth
void Renderer::geometryPass(const Camera& camera)
{
    gBuffer->bind();
    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    if (geometryShader) {
        geometryShader->use();
        
        glm::mat4 projection = camera.getProjectionMatrix(static_cast<float>(width) / height);
        glm::mat4 view = camera.getViewMatrix();
        
        geometryShader->setMat4("projection", projection);
        geometryShader->setMat4("view", view);
        
        // Basically the same as renderScene() but with materials
        const int gridSize = 5;
        const float tileSpacing = 4.0f;
        const float startX = -(gridSize - 1) * tileSpacing * 0.5f;
        const float startZ = -(gridSize - 1) * tileSpacing * 0.5f;
        
        // Render floor tiles with floor material
        setModelMaterial(floorMaterial);
        
        for (int x = 0; x < gridSize; ++x) {
            for (int z = 0; z < gridSize; ++z) {
                glm::mat4 tileMatrix = glm::mat4(1.0f);
                tileMatrix = glm::translate(tileMatrix, glm::vec3(
                    startX + x * tileSpacing,
                    -1.0f,
                    startZ + z * tileSpacing
                ));
                renderModel(floorTileModel, tileMatrix, geometryShader.get());
            }
        }

        // Render Dirt Floor (Grass/Dirt)
        setModelMaterial(dirtMaterial);
        for (float x = -22.0f; x < 22.0f; x += 4.0f) {
            for (float z = -22.0f; z < 22.0f; z += 4.0f) {
                // Skip existing stone floor area (from -10 to 10)
                if (x >= -10.0f && x < 10.0f && z >= -10.0f && z < 10.0f) continue;

                int seed = (int)(x * 31.0f + z * 17.0f);
                int choice = std::abs(seed) % 100;

                glm::mat4 model;
                if (choice < 40) {
                     // Large Dirt
                     model = glm::mat4(1.0f);
                     model = glm::translate(model, glm::vec3(x + 2.0f, -1.0f, z + 2.0f));
                     renderModel(floorDirtLargeModel, model, geometryShader.get());
                } else if (choice < 70) {
                     // Large Rocky
                     model = glm::mat4(1.0f);
                     model = glm::translate(model, glm::vec3(x + 2.0f, -1.0f, z + 2.0f));
                     renderModel(floorDirtLargeRockyModel, model, geometryShader.get());
                } else {
                     // 4 Small tiles
                     for (float sx = 0.0f; sx < 4.0f; sx += 2.0f) {
                         for (float sz = 0.0f; sz < 4.0f; sz += 2.0f) {
                             int subSeed = (int)((x + sx) * 53.0f + (z + sz) * 29.0f);
                             int tileType = std::abs(subSeed) % 5;
                             
                             model = glm::mat4(1.0f);
                             model = glm::translate(model, glm::vec3(x + sx + 1.0f, -1.0f, z + sz + 1.0f));
                             
                             if (tileType == 0) renderModel(floorDirtSmallAModel, model, geometryShader.get());
                             else if (tileType == 1) renderModel(floorDirtSmallBModel, model, geometryShader.get());
                             else if (tileType == 2) renderModel(floorDirtSmallCModel, model, geometryShader.get());
                             else if (tileType == 3) renderModel(floorDirtSmallDModel, model, geometryShader.get());
                             else renderModel(floorDirtSmallWeedsModel, model, geometryShader.get());
                         }
                     }
                }
            }
        }
        
        
        // Render Walls
        float wallY = -1.0f;
        float wallOffset = 10.0f;
        float cornerOffset = 10.0f;
        
        // Place Corners
        setModelMaterial(wallMaterial);
        glm::mat4 m = glm::mat4(1.0f);
        m = glm::translate(m, glm::vec3(-cornerOffset, wallY, -cornerOffset));
        m = glm::rotate(m, glm::radians(90.0f), glm::vec3(0,1,0));
        renderModel(cornerModel, m, geometryShader.get());
        
        m = glm::mat4(1.0f);
        m = glm::translate(m, glm::vec3(cornerOffset, wallY, -cornerOffset));
        renderModel(cornerModel, m, geometryShader.get());
        
        m = glm::mat4(1.0f);
        m = glm::translate(m, glm::vec3(-cornerOffset, wallY, cornerOffset));
        m = glm::rotate(m, glm::radians(180.0f), glm::vec3(0,1,0));
        renderModel(cornerModel, m, geometryShader.get());
        
        m = glm::mat4(1.0f);
        m = glm::translate(m, glm::vec3(cornerOffset, wallY, cornerOffset));
        m = glm::rotate(m, glm::radians(-90.0f), glm::vec3(0,1,0));
        renderModel(cornerModel, m, geometryShader.get());
        
        // Place Walls
        float xPositions[] = { -6.0f, -2.0f, 2.0f, 6.0f };
        
        for (int i = 0; i < 4; ++i) {
            glm::vec3 pos(xPositions[i], wallY, -wallOffset);
            glm::mat4 m = glm::mat4(1.0f);
            m = glm::translate(m, pos);
            
            if (i == 2) {
                 setModelMaterial(wallMaterial); 
                 renderModel(doorwayModel, m, geometryShader.get());
            } else if (i == 1) {
                 setModelMaterial(wallMaterial); 
                 renderModel(windowOpenModel, m, geometryShader.get());
            } else {
                 setModelMaterial(wallMaterial);
                 renderModel(wallModel, m, geometryShader.get());
            }
            
            if (i == 0) {
                setModelMaterial(torchMaterial);
                glm::mat4 t = glm::mat4(1.0f);
                t = glm::translate(t, pos + glm::vec3(0.0f, 2.3f, 0.4f)); 
                renderModel(torchModel, t, geometryShader.get());
            }
        }
        
        for (int i = 0; i < 4; ++i) {
            glm::vec3 pos(xPositions[i], wallY, wallOffset);
            glm::mat4 m = glm::mat4(1.0f);
            m = glm::translate(m, pos);
            m = glm::rotate(m, glm::radians(180.0f), glm::vec3(0,1,0));
            
            if (i == 0) {
                setModelMaterial(wallMaterial); 
                renderModel(windowClosedModel, m, geometryShader.get());
            } else if (i == 2) {
                setModelMaterial(wallMaterial); 
                renderModel(windowOpenModel, m, geometryShader.get());
            } else {
                setModelMaterial(wallMaterial);
                renderModel(wallModel, m, geometryShader.get());
            }
        }
        
        for (int i = 0; i < 4; ++i) {
            glm::vec3 pos(-wallOffset, wallY, xPositions[i]);
            glm::mat4 m = glm::mat4(1.0f);
            m = glm::translate(m, pos);
            m = glm::rotate(m, glm::radians(90.0f), glm::vec3(0,1,0));
            
            if (i == 1) {
                setModelMaterial(wallMaterial); 
                renderModel(windowOpenModel, m, geometryShader.get());
            } else {
                setModelMaterial(wallMaterial);
                renderModel(wallModel, m, geometryShader.get());
            }
            
            if (i == 0) {
                setModelMaterial(torchMaterial);
                glm::mat4 t = m;
                t = glm::translate(t, glm::vec3(0.0f, 2.3f, 0.4f)); 
                renderModel(torchModel, t, geometryShader.get());
            }
        }
        
        for (int i = 0; i < 4; ++i) {
            glm::vec3 pos(wallOffset, wallY, xPositions[i]);
            glm::mat4 m = glm::mat4(1.0f);
            m = glm::translate(m, pos);
            m = glm::rotate(m, glm::radians(-90.0f), glm::vec3(0,1,0));
            
            if (i == 2) {
                setModelMaterial(wallMaterial); 
                renderModel(windowClosedModel, m, geometryShader.get());
            } else {
                setModelMaterial(wallMaterial);
                renderModel(wallModel, m, geometryShader.get());
            }
            
            if (i == 3) {
                 setModelMaterial(torchMaterial);
                 glm::mat4 t = m;
                 t = glm::translate(t, glm::vec3(0.0f, 2.6f, 0.4f));
                 renderModel(torchModel, t, geometryShader.get());
            }
        }

        // Second Floor
        float floor2Y = wallY + 4.0f; // First floor wall height
        
        // 1. Ceiling of first floor and wood floor of second floor
        setModelMaterial(ceilingMaterial);
        for (int x = 0; x < gridSize; ++x) {
            for (int z = 0; z < gridSize; ++z) {
                // Stair opening
                if (x == 4 && (z == 4 || z == 3)) continue;
                
                glm::mat4 tileMatrix = glm::mat4(1.0f);
                tileMatrix = glm::translate(tileMatrix, glm::vec3(
                    startX + x * tileSpacing,
                    floor2Y, 
                    startZ + z * tileSpacing
                ));
                renderModel(ceilingModel, tileMatrix, geometryShader.get());
            }
        }

        setModelMaterial(woodFloorMaterial);
        for (int x = 0; x < gridSize; ++x) {
            for (int z = 0; z < gridSize; ++z) {
                if (x == 4 && (z == 4 || z == 3)) continue;
                
                glm::mat4 tileMatrix = glm::mat4(1.0f);
                tileMatrix = glm::translate(tileMatrix, glm::vec3(
                    startX + x * tileSpacing,
                    floor2Y + 0.1f, 
                    startZ + z * tileSpacing
                ));
                renderModel(woodFloorModel, tileMatrix, geometryShader.get());
            }
        }
        
        // 2. Stairs
        setModelMaterial(stairMaterial);
        glm::mat4 stairMatrix = glm::mat4(1.0f);
        glm::vec3 stairPos(8.0f, -1.0f, 10.0f);
        glm::vec3 stairAxis(0.0f, 1.0f, 0.0f);
        stairMatrix = glm::translate(stairMatrix, stairPos);
        stairMatrix = glm::rotate(stairMatrix, glm::radians(180.0f), stairAxis); 
        renderModel(stairModel, stairMatrix, geometryShader.get());
        
        // 3. Second Floor Walls
        setModelMaterial(wallMaterial); 
        
        float f2WallY = floor2Y;
        
        for(int i=0; i<2; ++i) {
            glm::mat4 m = glm::mat4(1.0f);
            float xPos = 2.0f + (float)i * 4.0f;
            glm::vec3 pos(xPos, f2WallY, 2.0f);
            m = glm::translate(m, pos);
            renderModel(wallModel, m, geometryShader.get());
        }
        
        for(int i=0; i<2; ++i) {
            glm::mat4 m = glm::mat4(1.0f);
            float xPos = 2.0f + (float)i * 4.0f;
            glm::vec3 pos(xPos, f2WallY, 10.0f);
            glm::vec3 axis(0.0f, 1.0f, 0.0f);
            m = glm::translate(m, pos);
            m = glm::rotate(m, glm::radians(180.0f), axis);
            setModelMaterial(wallMaterial);
            renderModel(wallModel, m, geometryShader.get());
            
            if (i == 1) {
                setModelMaterial(torchMaterial);
                glm::mat4 t = m;
                t = glm::translate(t, glm::vec3(0.0f, 2.3f, 0.4f));
                renderModel(torchModel, t, geometryShader.get());
            }
        }
        
        {
            setModelMaterial(wallMaterial);
            glm::mat4 m = glm::mat4(1.0f);
            glm::vec3 pos(-2.0f, f2WallY, 6.0f);
            glm::vec3 axis(0.0f, 1.0f, 0.0f);
            m = glm::translate(m, pos);
            m = glm::rotate(m, glm::radians(90.0f), axis);
            renderModel(wallModel, m, geometryShader.get());
        }
        
        {
            setModelMaterial(wallMaterial); 
            glm::mat4 m = glm::mat4(1.0f);
            glm::vec3 pos(10.0f, f2WallY, 6.0f);
            glm::vec3 axis(0.0f, 1.0f, 0.0f);
            m = glm::translate(m, pos);
            m = glm::rotate(m, glm::radians(-90.0f), axis);
            renderModel(windowOpenModel, m, geometryShader.get());
        }
        
        // 4. Ceiling for Second Floor Room
        setModelMaterial(ceilingMaterial);
        float ceilingHeight = f2WallY + 4.0f; // Top of second floor
        
        for (int x = 2; x <= 4; ++x) {
            for (int z = 3; z <= 4; ++z) {
                 glm::mat4 tileMatrix = glm::mat4(1.0f);
                 tileMatrix = glm::translate(tileMatrix, glm::vec3(
                     startX + x * tileSpacing,
                     ceilingHeight, 
                     startZ + z * tileSpacing
                 ));
                 renderModel(ceilingModel, tileMatrix, geometryShader.get());
            }
        }
        
        // 5. Torches on Second Floor
        
        {
             glm::mat4 t = glm::mat4(1.0f);
             t = glm::translate(t, glm::vec3(-9.6f, 1.3f, 6.0f)); 
             setModelMaterial(torchMaterial);
             renderModel(torchModel, t, geometryShader.get());
        }
        
        {
             glm::vec3 pos(-2.0f, f2WallY, 6.0f);
             glm::mat4 m = glm::mat4(1.0f); 
             m = glm::translate(m, pos);
             m = glm::rotate(m, glm::radians(90.0f), glm::vec3(0,1,0));
             
             glm::mat4 t = m;
             t = glm::translate(t, glm::vec3(0.0f, 2.3f, 0.4f));
             setModelMaterial(torchMaterial);
             renderModel(torchModel, t, geometryShader.get());
        }

        setModelMaterial(wallMaterial);
        
        m = glm::mat4(1.0f);
        {
            glm::vec3 pos(-2.0f, f2WallY, 2.0f);
            glm::vec3 axis(0.0f, 1.0f, 0.0f);
            m = glm::translate(m, pos);
            m = glm::rotate(m, glm::radians(90.0f), axis);
            renderModel(cornerModel, m, geometryShader.get());
        }
        
        m = glm::mat4(1.0f);
        {
            glm::vec3 pos(10.0f, f2WallY, 2.0f);
            m = glm::translate(m, pos);
            renderModel(cornerModel, m, geometryShader.get());
        }
        
        m = glm::mat4(1.0f);
        {
            glm::vec3 pos(-2.0f, f2WallY, 10.0f);
            glm::vec3 axis(0.0f, 1.0f, 0.0f);
            m = glm::translate(m, pos);
            m = glm::rotate(m, glm::radians(180.0f), axis);
            renderModel(cornerModel, m, geometryShader.get());
        }
        
        m = glm::mat4(1.0f);
        {
            glm::vec3 pos(10.0f, f2WallY, 10.0f);
            glm::vec3 axis(0.0f, 1.0f, 0.0f);
            m = glm::translate(m, pos);
            m = glm::rotate(m, glm::radians(-90.0f), axis);
            renderModel(cornerModel, m, geometryShader.get());
        }
        renderDecorations(geometryShader.get());
    }
    
    gBuffer->unbind();
}

// Deferred Lighting Pass
// This is the core of the pipeline. It takes the G-Buffer data and generates the final image by applying lighting equations, shadow mapping, and cel shading logic.
// All calculations are done in screen-space.
void Renderer::lightingPass(const Camera& camera)
{
    // Render lightingFBO.
    // The result will be used as an input texture for the subsequent edge detection and composite passes.
    glBindFramebuffer(GL_FRAMEBUFFER, lightingFBO);
    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT);
    
    if (hybridCelShader) {
        hybridCelShader->use();
        
        // Bind G-Buffer textures
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gBuffer->getBaseColorTexture());
        hybridCelShader->setInt("gBaseColor", 0);
        
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, gBuffer->getNormalTexture());
        hybridCelShader->setInt("gNormal", 1);
        
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, gBuffer->getPositionTexture());
        hybridCelShader->setInt("gPosition", 2);
        
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, gBuffer->getQuantizationTexture());
        hybridCelShader->setInt("gQuantization", 3);
        
        // Bind shadow maps (cubemaps for point, 2D for dir/spot)
        const auto& lights = lightManager->getLights();
        size_t lightCount = std::min(lights.size(), MAX_SHADOW_CASTING_LIGHTS);
        
        for (size_t i = 0; i < lightCount; ++i) {
            const auto& light = lights[i];
            const auto& shadowData = shadowMaps[i];
            
            if (!shadowData.isActive) continue;
            
            // Texture units: 4+ reserved for shadow maps
            int textureUnit = 4 + i * 2; 
            
            if (light.type == LightType::POINT) {
                // Cube Map for point light shadows
                glActiveTexture(GL_TEXTURE0 + textureUnit);
                glBindTexture(GL_TEXTURE_CUBE_MAP, shadowData.depthCubemap);
                hybridCelShader->setInt("shadowCubeMaps[" + std::to_string(i) + "]", textureUnit);
            } else {
                // 2D Shadow Map for directional/spot lights
                glActiveTexture(GL_TEXTURE0 + textureUnit);
                glBindTexture(GL_TEXTURE_2D, shadowData.depthMap);
                hybridCelShader->setInt("shadowMaps[" + std::to_string(i) + "]", textureUnit);
                // Matrix to transform world position to light-space
                hybridCelShader->setMat4("lightSpaceMatrices[" + std::to_string(i) + "]", shadowData.lightSpaceMatrix);
            }
        }
        
        // Upload light properties to shader
        hybridCelShader->setInt("numLights", lightCount);
        
        for (size_t i = 0; i < lightCount; ++i) {
            const auto& light = lights[i];
            std::string base = "lights[" + std::to_string(i) + "]";
            hybridCelShader->setInt(base + ".type", static_cast<int>(light.type));
            hybridCelShader->setVec3(base + ".position", light.position);
            hybridCelShader->setVec3(base + ".direction", light.direction);
            hybridCelShader->setVec3(base + ".color", light.color);
            hybridCelShader->setFloat(base + ".intensity", light.intensity);
            hybridCelShader->setFloat(base + ".constant", light.constant);
            hybridCelShader->setFloat(base + ".linear", light.linear);
            hybridCelShader->setFloat(base + ".quadratic", light.quadratic);
            hybridCelShader->setFloat(base + ".cutOff", light.cutOff);
            hybridCelShader->setFloat(base + ".outerCutOff", light.outerCutOff);
            hybridCelShader->setBool(base + ".castShadows", light.castShadows);
        }
        
        // Shadow and camera settings
        hybridCelShader->setFloat("shadowBias", shadowParams.shadowBias);
        hybridCelShader->setFloat("shadowNormalBias", shadowParams.shadowNormalBias);
        hybridCelShader->setInt("shadowPCFSamples", shadowParams.shadowPCFSamples);
        hybridCelShader->setFloat("shadowIntensity", shadowParams.shadowIntensity);
        hybridCelShader->setBool("enablePCF", shadowParams.enablePCF);
        hybridCelShader->setFloat("shadowFarPlane", shadowParams.farPlane);
        
        hybridCelShader->setVec3("viewPos", camera.Position);
        hybridCelShader->setMat4("view", camera.getViewMatrix());
        hybridCelShader->setMat4("projection", camera.getProjectionMatrix(static_cast<float>(width) / height));
        
        // Material/toon settings
        hybridCelShader->setBool("enableQuantization", materialParams.enableQuantization);
        hybridCelShader->setInt("diffuseQuantizationBands", materialParams.diffuseQuantizationBands);
        hybridCelShader->setFloat("specularThreshold1", materialParams.specularThreshold1);
        hybridCelShader->setFloat("specularThreshold2", materialParams.specularThreshold2);
        hybridCelShader->setInt("globalMaterialType", static_cast<int>(globalIlluminationModel));
        
        renderQuad();
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// Edge Detection Pass - find depth/normal/color discontinuities for outlines
void Renderer::edgeDetectionPass()
{
    glBindFramebuffer(GL_FRAMEBUFFER, edgeFBO);
    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT); // Clear buffer
    
    if (edgeDetectionShader) {
        edgeDetectionShader->use();
        
        // Feed the G-Buffer into the edge detector.
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gBuffer->getPositionTexture());
        edgeDetectionShader->setInt("gPosition", 0);
        
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, gBuffer->getNormalTexture());
        edgeDetectionShader->setInt("gNormal", 1);
        
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, gBuffer->getDepthTexture());
        edgeDetectionShader->setInt("gDepth", 2);
        
        // Also feed the lighting result for color-based edge detection.
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, lightingTexture);
        edgeDetectionShader->setInt("colorTexture", 3);
        
        // Threshold settings (lower = more sensitive)
        edgeDetectionShader->setInt("edgeFlags", edgeDetectionFlags);
        edgeDetectionShader->setFloat("depthThreshold", edgeParams.depthThreshold);
        edgeDetectionShader->setFloat("normalThreshold", edgeParams.normalThreshold);
        edgeDetectionShader->setFloat("sobelThreshold", edgeParams.sobelThreshold);
        edgeDetectionShader->setFloat("colorThreshold", edgeParams.colorThreshold);
        edgeDetectionShader->setVec3("edgeColor", edgeParams.edgeColor);
        edgeDetectionShader->setVec2("screenSize", glm::vec2(width, height));
        // Extra parameters
        edgeDetectionShader->setFloat("depthExponent", edgeParams.depthExponent);
        edgeDetectionShader->setFloat("normalSplit", edgeParams.normalSplit);
        edgeDetectionShader->setFloat("sobelScale", edgeParams.sobelScale);
        edgeDetectionShader->setFloat("smoothWidth", edgeParams.smoothWidth);
        edgeDetectionShader->setFloat("laplacianThreshold", edgeParams.laplacianThreshold);
        edgeDetectionShader->setFloat("laplacianScale", edgeParams.laplacianScale);
        
        renderQuad();
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// Composite Pass - combine lit scene with edge outlines, render to screen
void Renderer::compositePass()
{
    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    if (compositeShader) {
        compositeShader->use();
        
        // Input 1: lit scene.
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, lightingTexture);
        compositeShader->setInt("lightingTexture", 0);
        
        // Input 2: edges map (black lines on transparent background).
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, edgeTexture);
        compositeShader->setInt("edgeTexture", 1);
        
        // Toggle outlining on/off.
        compositeShader->setBool("enableOutlining", edgeParams.enableOutlining);
        
        renderQuad();
    }
}

// Fullscreen quad for post-processing
void Renderer::renderQuad()
{
    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}
void Renderer::loadModels()
{
    // Load mesh assets with error handling

    try {
        floorTileModel = std::make_unique<Model>("assets/models/floor_tile_large.obj");
        std::cout << "Successfully loaded floor_tile_large.obj" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load floor tile model: " << e.what() << std::endl;
    }

    try {
        wallModel = std::make_unique<Model>("assets/models/wall.obj");
        std::cout << "Successfully loaded wall.obj" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load wall model: " << e.what() << std::endl;
    }

    try {
        cornerModel = std::make_unique<Model>("assets/models/wall_corner.obj");
        std::cout << "Successfully loaded wall_corner.obj" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load corner model: " << e.what() << std::endl;
    }

    try {
        doorwayModel = std::make_unique<Model>("assets/models/wall_doorway.obj");
        std::cout << "Successfully loaded wall_doorway.obj" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load doorway model: " << e.what() << std::endl;
    }

    try {
        windowOpenModel = std::make_unique<Model>("assets/models/wall_window_open.obj");
        std::cout << "Successfully loaded wall_window_open.obj" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load window open model: " << e.what() << std::endl;
    }

    try {
        windowClosedModel = std::make_unique<Model>("assets/models/wall_window_closed.obj");
        std::cout << "Successfully loaded wall_window_closed.obj" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load window closed model: " << e.what() << std::endl;
    }

    try {
        ceilingModel = std::make_unique<Model>("assets/models/ceiling_tile.obj");
        std::cout << "Successfully loaded ceiling_tile.obj" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load ceiling model: " << e.what() << std::endl;
    }

    try {
        woodFloorModel = std::make_unique<Model>("assets/models/floor_wood_large.obj");
        std::cout << "Successfully loaded floor_wood_large.obj" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load wood floor model: " << e.what() << std::endl;
    }

    try {
        stairModel = std::make_unique<Model>("assets/models/stairs_wood_decorated.obj");
        std::cout << "Successfully loaded stairs_wood_decorated.obj" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load stair model: " << e.what() << std::endl;
    }

    try {
        torchModel = std::make_unique<Model>("assets/models/torch_lit.obj");
        std::cout << "Successfully loaded torch_lit.obj" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load torch model: " << e.what() << std::endl;
    }

    // Load Extended Ground Models
    try {
        floorDirtLargeModel = std::make_unique<Model>("assets/models/floor_dirt_large.obj");
        std::cout << "Successfully loaded floor_dirt_large.obj" << std::endl;
    } catch (const std::exception& e) { std::cerr << "Failed to load floor_dirt_large: " << e.what() << std::endl; }

    try {
        floorDirtLargeRockyModel = std::make_unique<Model>("assets/models/floor_dirt_large_rocky.obj");
        std::cout << "Successfully loaded floor_dirt_large_rocky.obj" << std::endl;
    } catch (const std::exception& e) { std::cerr << "Failed to load floor_dirt_large_rocky: " << e.what() << std::endl; }

    try {
        floorDirtSmallAModel = std::make_unique<Model>("assets/models/floor_dirt_small_A.obj");
        std::cout << "Successfully loaded floor_dirt_small_A.obj" << std::endl;
    } catch (const std::exception& e) { std::cerr << "Failed to load floor_dirt_small_A: " << e.what() << std::endl; }

    try {
        floorDirtSmallBModel = std::make_unique<Model>("assets/models/floor_dirt_small_B.obj");
        std::cout << "Successfully loaded floor_dirt_small_B.obj" << std::endl;
    } catch (const std::exception& e) { std::cerr << "Failed to load floor_dirt_small_B: " << e.what() << std::endl; }

    try {
        floorDirtSmallCModel = std::make_unique<Model>("assets/models/floor_dirt_small_C.obj");
        std::cout << "Successfully loaded floor_dirt_small_C.obj" << std::endl;
    } catch (const std::exception& e) { std::cerr << "Failed to load floor_dirt_small_C: " << e.what() << std::endl; }

    try {
        floorDirtSmallDModel = std::make_unique<Model>("assets/models/floor_dirt_small_D.obj");
        std::cout << "Successfully loaded floor_dirt_small_D.obj" << std::endl;
    } catch (const std::exception& e) { std::cerr << "Failed to load floor_dirt_small_D: " << e.what() << std::endl; }

    try {
        floorDirtSmallWeedsModel = std::make_unique<Model>("assets/models/floor_dirt_small_weeds.obj");
        std::cout << "Successfully loaded floor_dirt_small_weeds.obj" << std::endl;
    } catch (const std::exception& e) { std::cerr << "Failed to load floor_dirt_small_weeds: " << e.what() << std::endl; }

    // Use try-catch for decorations as well
    const std::vector<std::pair<std::string, std::unique_ptr<Model>*>> decorationAssets = {
        {"assets/models/table_long_decorated_A.obj", &tableLongDecoratedModel},
        {"assets/models/chair.obj", &chairModel},
        {"assets/models/stool.obj", &stoolModel},
        {"assets/models/barrel_large.obj", &barrelModel},
        {"assets/models/shelf_small_candles.obj", &shelfSmallCandlesModel},
        {"assets/models/bed_frame.obj", &bedModel},
        {"assets/models/chest_gold.obj", &chestGoldModel},
        {"assets/models/banner_red.obj", &bannerModel},
        {"assets/models/candle_triple.obj", &candleTripleModel},
        {"assets/models/crates_stacked.obj", &crateStackModel},
        {"assets/models/sword_shield.obj", &swordShieldModel},
        {"assets/models/Pallet_Wood.obj", &woodPalletModel},
        {"assets/models/Wood_Planks_Stack_Large.obj", &woodPlanksModel},
        {"assets/models/Stone_Bricks_Stack_Large.obj", &stoneStackModel},
        {"assets/models/Gold_Bars.obj", &goldBarsModel},
        {"assets/models/Parts_Pile_Large.obj", &metalPartsModel},
        {"assets/models/Textiles_Stack_Large_Colored.obj", &textilesModel}
    };

    for (const auto& asset : decorationAssets) {
        try {
            *asset.second = std::make_unique<Model>(asset.first.c_str());
            std::cout << "Successfully loaded " << asset.first << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Failed to load " << asset.first << ": " << e.what() << std::endl;
        }
    }
}
 

// Spawns all the decorations.
void Renderer::renderDecorations(Shader* shader)
{
    float floorY = -1.0f;

    // Side Tables (Left/Right)
    // Table and chairs setup
    auto drawSideTable = [&](float tx, float tz, float spacing = 1.5f) {
        // Table Rotated to X-axis (-180)
        setModelMaterial(tableMaterial);
        glm::mat4 m = glm::mat4(1.0f);
        m = glm::translate(m, glm::vec3(tx, floorY, tz));
        m = glm::rotate(m, glm::radians(-180.0f), glm::vec3(0,1,0));
        renderModel(tableLongDecoratedModel, m, shader);

        // Chairs with random offsets in rotation and position so they seem natural
        auto drawChair = [&](float cx, float cz, float baseRot) {
            float seed = (cx * 13.0f + cz * 37.0f + tx * 7.0f);
            float jitterRot = std::fmod(std::abs(std::sin(seed) * 100.0f), 20.0f) - 10.0f;
            float jitterX = (std::fmod(std::abs(std::cos(seed * 0.5f) * 100.0f), 0.2f) - 0.1f);
            float jitterZ = (std::fmod(std::abs(std::sin(seed * 0.8f) * 100.0f), 0.2f) - 0.1f);

            glm::mat4 cm = glm::mat4(1.0f);
            cm = glm::translate(cm, glm::vec3(tx + cx + jitterX, floorY, tz + cz + jitterZ));
            cm = glm::rotate(cm, glm::radians(baseRot + jitterRot), glm::vec3(0,1,0));
            setModelMaterial(chairMaterial);
            renderModel(chairModel, cm, shader);
        };

        drawChair(-1.0f, -spacing, 180.0f);
        drawChair(-1.0f,  spacing, 180.0f);
        
        drawChair( 1.0f, -spacing, 0.0f);
        drawChair( 1.0f,  spacing, 0.0f);

        drawChair( 0.0f, -2.5f, 90.0f);
        drawChair( 0.0f,  2.5f, 270.0f);
    };

    // Central table with chairs
    // Chairs positions rotated 90 degrees around table.
    auto drawCentralTable = [&](float tx, float tz) {
        setModelMaterial(tableMaterial);
        glm::mat4 m = glm::mat4(1.0f);
        m = glm::translate(m, glm::vec3(tx, floorY, tz));
        m = glm::rotate(m, glm::radians(-90.0f), glm::vec3(0,1,0));
        renderModel(tableLongDecoratedModel, m, shader);

        auto drawChair = [&](float cx, float cz, float baseRot) {
            // Rotate chair position and orientation
            float rcx = cz;
            float rcz = -cx;
            // Rotate chair 180 degrees
            float rRot = baseRot + 90.0f;

            float seed = (rcx * 13.0f + rcz * 37.0f + tx * 7.0f);
            float jitterRot = std::fmod(std::abs(std::sin(seed) * 100.0f), 20.0f) - 10.0f;
            float jitterX = (std::fmod(std::abs(std::cos(seed * 0.5f) * 100.0f), 0.2f) - 0.1f);
            float jitterZ = (std::fmod(std::abs(std::sin(seed * 0.8f) * 100.0f), 0.2f) - 0.1f);

            glm::mat4 cm = glm::mat4(1.0f);
            cm = glm::translate(cm, glm::vec3(tx + rcx + jitterX, floorY, tz + rcz + jitterZ));
            cm = glm::rotate(cm, glm::radians(rRot + jitterRot), glm::vec3(0,1,0));
            setModelMaterial(chairMaterial);
            renderModel(chairModel, cm, shader);
        };

        drawChair(-1.0f, -1.0f, 180.0f);
        drawChair(-1.0f,  1.0f, 180.0f);
        drawChair( 1.0f, -1.0f, 0.0f);
        drawChair( 1.0f,  1.0f, 0.0f);
        drawChair( 0.0f, -2.5f, 90.0f);
        drawChair( 0.0f,  2.5f, 270.0f);
    };

    // Draw Tables
    drawSideTable(-6.0f, 4.0f, 1.0f);
    drawSideTable( 6.0f, -3.0f, 0.7f);
    drawCentralTable(0.0f, 5.0f);


    // Barrel
    setModelMaterial(barrelMaterial);
    glm::mat4 m = glm::mat4(1.0f);
    m = glm::translate(m, glm::vec3(-8.5f, floorY, 8.5f));
    renderModel(barrelModel, m, shader);

    // Candles on top of barrel
    setModelMaterial(candleMaterial);
    m = glm::mat4(1.0f);
    m = glm::translate(m, glm::vec3(-8.5f, floorY + 1.5f, 8.5f)); // Raised by 0.5 units (1.0 -> 1.5)
    renderModel(candleTripleModel, m, shader);

    // Crates in corner
    setModelMaterial(crateMaterial);
    m = glm::mat4(1.0f);
    m = glm::translate(m, glm::vec3(8.5f, floorY, -8.5f));
    m = glm::rotate(m, glm::radians(30.0f), glm::vec3(0,1,0));
    renderModel(crateStackModel, m, shader);

    // Candles on window
    setModelMaterial(shelfMaterial);
    m = glm::mat4(1.0f);
    m = glm::translate(m, glm::vec3(-10.0f, 0.4f, -2.0f));
    m = glm::rotate(m, glm::radians(90.0f), glm::vec3(0,1,0));
    renderModel(shelfSmallCandlesModel, m, shader);

    // Sword & Shield on South Wall
    setModelMaterial(swordShieldMaterial);
    m = glm::mat4(1.0f);
    m = glm::translate(m, glm::vec3(-0.5f, floorY + 2.25f, 9.6f));
    m = glm::rotate(m, glm::radians(180.0f), glm::vec3(0,1,0));
    renderModel(swordShieldModel, m, shader);


    // Wood pallets
    setModelMaterial(woodPalletMaterial);
    
    m = glm::mat4(1.0f);
    m = glm::translate(m, glm::vec3(-8.5f, floorY, -3.5f));
    m = glm::rotate(m, glm::radians(90.0f), glm::vec3(0,1,0));
    m = glm::rotate(m, glm::radians(5.0f), glm::vec3(0,1,0)); 
    renderModel(woodPalletModel, m, shader);

    // wood Planks
    {
        setModelMaterial(woodPlanksMaterial);
        glm::mat4 rm = m;
        rm = glm::translate(rm, glm::vec3(0.0f, 0.3f, 0.0f));
        renderModel(woodPlanksModel, rm, shader);
        setModelMaterial(woodPalletMaterial);
    }

    // Pallet 2
    m = glm::mat4(1.0f);
    m = glm::translate(m, glm::vec3(-8.5f, floorY, -5.5f));
    m = glm::rotate(m, glm::radians(90.0f), glm::vec3(0,1,0));
    m = glm::rotate(m, glm::radians(-3.0f), glm::vec3(0,1,0));
    renderModel(woodPalletModel, m, shader);

    // Stone bricks
    {
        setModelMaterial(stoneStackMaterial);
        glm::mat4 rm = m;
        rm = glm::translate(rm, glm::vec3(0.0f, 0.3f, 0.0f));
        renderModel(stoneStackModel, rm, shader);
        setModelMaterial(woodPalletMaterial);
    }

    // Pallet 3
    m = glm::mat4(1.0f);
    m = glm::translate(m, glm::vec3(-3.5f, floorY, -8.5f)); 
    m = glm::rotate(m, glm::radians(0.0f), glm::vec3(0,1,0)); 
    m = glm::rotate(m, glm::radians(2.0f), glm::vec3(0,1,0));
    renderModel(woodPalletModel, m, shader);

    // Gold bars
    {
        setModelMaterial(goldBarsMaterial);
        glm::mat4 rm = m;
        rm = glm::translate(rm, glm::vec3(0.0f, 0.3f, 0.0f));
        renderModel(goldBarsModel, rm, shader);
        setModelMaterial(woodPalletMaterial);
    }

    // Pallet 4
    m = glm::mat4(1.0f);
    m = glm::translate(m, glm::vec3(-5.5f, floorY, -8.5f));
    m = glm::rotate(m, glm::radians(0.0f), glm::vec3(0,1,0));
    m = glm::rotate(m, glm::radians(-4.0f), glm::vec3(0,1,0));
    renderModel(woodPalletModel, m, shader);

    // Metal parts
    {
        setModelMaterial(metalPartsMaterial);
        glm::mat4 rm = m;
        rm = glm::translate(rm, glm::vec3(0.0f, 0.3f, 0.0f));
        renderModel(metalPartsModel, rm, shader);
        setModelMaterial(woodPalletMaterial);
    }

    // Pallet 5
    m = glm::mat4(1.0f);
    m = glm::translate(m, glm::vec3(-8.0f, floorY, -8.0f)); 
    m = glm::rotate(m, glm::radians(45.0f), glm::vec3(0,1,0)); 
    renderModel(woodPalletModel, m, shader);

    // Textiles
    {
        setModelMaterial(textilesMaterial);
        glm::mat4 rm = m;
        rm = glm::translate(rm, glm::vec3(0.0f, 0.3f, 0.0f));
        renderModel(textilesModel, rm, shader);
        setModelMaterial(woodPalletMaterial);
    }



    // Second Floor
    float floor2Y = 3.1f; 
   
    // Bed
    setModelMaterial(bedMaterial);
    m = glm::mat4(1.0f);
    m = glm::translate(m, glm::vec3(-0.1f, floor2Y, 4.0f));
    renderModel(bedModel, m, shader);

    // Chest with money
    setModelMaterial(chestMaterial);
    m = glm::mat4(1.0f);
    m = glm::translate(m, glm::vec3(-0.1f, floor2Y, 6.5f)); 
    m = glm::rotate(m, glm::radians(180.0f), glm::vec3(0,1,0)); 
    renderModel(chestGoldModel, m, shader);

    // Banner
    setModelMaterial(bannerMaterial);
    m = glm::mat4(1.0f);
    m = glm::translate(m, glm::vec3(4.0f, floor2Y, 2.1f)); 
    renderModel(bannerModel, m, shader);
    
    // Stool
    setModelMaterial(stoolMaterial);
    m = glm::mat4(1.0f);
    m = glm::translate(m, glm::vec3(-0.5f, floor2Y, 9.0f));
    renderModel(stoolModel, m, shader);
}

// This function sets the model matrix and texture uniforms before the draw command and also collects basic rendering statistics (draw-call count, vertices).
void Renderer::renderModel(const std::unique_ptr<Model>& model, const glm::mat4& modelMatrix, Shader* shader)
{
    if (!model || !shader) return;
    
    // Set model matrix on the target shader
    shader->setMat4("model", modelMatrix);

    // Handle texture binding.
    // If the model has a diffuse texture, we bind it to TU0.
    bool hasTexture = model->hasTexture();
    shader->setBool("hasTexture", hasTexture);
    
    if (hasTexture) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, model->getDiffuseTexture());
        shader->setInt("texture_diffuse1", 0);
    }
    
    model->draw();
    
    // Stats for the performance overlay
    stats.drawCalls++;
    stats.vertexCount += model->getVertexCount();
}

// This associates each material to a name for the GUI.
void Renderer::initializeModelMaterials()
{
    // Structural Materials
    floorMaterial.name = "Floor";
    wallMaterial.name = "Walls";
    ceilingMaterial.name = "Ceiling";
    woodFloorMaterial.name = "Wood Floor";
    stairMaterial.name = "Stairs";
    dirtMaterial.name = "Grass/Dirt";
    
    // Decoration Materials
    tableMaterial.name = "Table";
    chairMaterial.name = "Chair";
    stoolMaterial.name = "Stool";
    barrelMaterial.name = "Barrel";
    shelfMaterial.name = "Shelf";
    bedMaterial.name = "Bed";
    chestMaterial.name = "Chest";
    bannerMaterial.name = "Banner";
    candleMaterial.name = "Candles";
    crateMaterial.name = "Crates";
    swordShieldMaterial.name = "Sword & Shield";
    woodPalletMaterial.name = "Wood Pallet";

    // Resource Pile Materials
    woodPlanksMaterial.name = "Wood Planks";
    stoneStackMaterial.name = "Stone Stack";
    goldBarsMaterial.name = "Gold Bars";
    metalPartsMaterial.name = "Metal Parts";
    textilesMaterial.name = "Textiles";
}

void Renderer::applyPreset(int index) {
    resetParamsToDefaults();
    initializeLights(); // Reset lights to initial state
}

void Renderer::resetParamsToDefaults() {
    // 1. Reset Global Material Parameters
    materialParams = MaterialParams();
    
    // 2. Reset Lighting Model
    globalIlluminationModel = IlluminationModel::LAMBERTIAN;
    
    // 3. Reset Edge Detection
    edgeDetectionFlags = static_cast<int>(EdgeDetectionType::DEPTH_BASED);
    edgeParams = EdgeParams(); 
    
    // 4. Reset Shadow Params
    shadowParams = ShadowParams(); 
    
    // Reset materials while keeping names
    auto resetMat = [](ModelMaterial& mat) {
        std::string savedName = mat.name;
        mat.model = IlluminationModel::LAMBERTIAN;
        mat.params = MaterialParams(); 
        mat.name = savedName;
    };
    
    resetMat(floorMaterial);
    resetMat(wallMaterial);
    // resetMat(windowOpenMaterial);
    // resetMat(windowClosedMaterial);
    resetMat(ceilingMaterial);
    resetMat(woodFloorMaterial);
    resetMat(stairMaterial);
    resetMat(dirtMaterial);
    
    resetMat(tableMaterial);
    resetMat(chairMaterial);
    resetMat(stoolMaterial);
    resetMat(barrelMaterial);
    // resetMat(kegMaterial);
    resetMat(shelfMaterial);
    resetMat(bedMaterial);
    resetMat(chestMaterial);
    resetMat(bannerMaterial);
    resetMat(candleMaterial);
    // resetMat(plateMaterial);
    // resetMat(bottleMaterial);
    resetMat(crateMaterial);
    resetMat(swordShieldMaterial);
    resetMat(woodPalletMaterial);

    resetMat(woodPlanksMaterial);
    resetMat(stoneStackMaterial);
    resetMat(goldBarsMaterial);
    resetMat(metalPartsMaterial);
    resetMat(textilesMaterial);
    
    initializeModelMaterials();
}

// Initialize scene lights (sun, moon, torches)
void Renderer::initializeLights()
{
    // Clear existing lights
    lightManager->clearLights();
    
    // 1. Sun (Almost White)
    // Color: 255, 255, 251 -> (1.0, 1.0, 0.984)
    glm::vec3 sunColor(1.0f, 1.0f, 0.984f); 
    Light sun = Light::createDirectionalLight(glm::vec3(0.0f, -1.0f, 0.0f), sunColor, 1.0f);
    sun.castShadows = true;
    sun.isStatic = false;
    sun.flicker = false;
    lightManager->addLight(sun);

    // 2. Moon (Cool/Blue)
    // Color: 214, 220, 227 -> (0.839, 0.863, 0.890)
    glm::vec3 moonColor(0.839f, 0.863f, 0.890f);
    Light moon = Light::createDirectionalLight(glm::vec3(0.0f, 1.0f, 0.0f), moonColor, 1.0f); // Initially opposite
    moon.castShadows = true;
    moon.isStatic = false;
    moon.flicker = false;
    lightManager->addLight(moon);
    
    // Torch point lights (static, flickering)
    Light t1 = Light::createPointLight(glm::vec3(-6.0f, 1.9f, -9.6f), glm::vec3(1.0f, 0.6f, 0.2f), 1.0f);
    t1.castShadows = true;
    t1.isStatic = true;
    t1.flicker = true;
    lightManager->addLight(t1);

    Light t2 = Light::createPointLight(glm::vec3(-9.6f, 1.9f, -6.0f), glm::vec3(1.0f, 0.6f, 0.2f), 1.0f);
    t2.castShadows = true;
    t2.isStatic = true;
    t2.flicker = true;
    lightManager->addLight(t2);

    Light t3 = Light::createPointLight(glm::vec3(9.6f, 1.9f, 6.0f), glm::vec3(1.0f, 0.6f, 0.2f), 1.0f);
    t3.castShadows = true;
    t3.isStatic = true;
    t3.flicker = true;
    lightManager->addLight(t3);

    Light t4 = Light::createPointLight(glm::vec3(-9.6f, 1.9f, 6.0f), glm::vec3(1.0f, 0.6f, 0.2f), 1.0f);
    t4.castShadows = true; 
    t4.isStatic = true;
    t4.flicker = true;
    lightManager->addLight(t4);
    
    Light t5 = Light::createPointLight(glm::vec3(-1.6f, 5.9f, 6.0f), glm::vec3(1.0f, 0.6f, 0.2f), 1.0f);
    t5.castShadows = true;
    t5.isStatic = true;
    t5.flicker = true;
    lightManager->addLight(t5);
    
    Light t6 = Light::createPointLight(glm::vec3(6.0f, 5.9f, 9.6f), glm::vec3(1.0f, 0.6f, 0.2f), 1.0f);
    t6.castShadows = true;
    t6.isStatic = true;
    t6.flicker = true;
    lightManager->addLight(t6);
}

// Day/night cycle and torch flicker
void Renderer::updateLights(float deltaTime) 
{
    static float totalTime = 0.0f;
    totalTime += deltaTime;
    
    auto& lights = lightManager->getLightsReference();
    
    // Update Sun and Moon (0 and 1)
    if (lights.size() >= 2) {
        float cycleSpeed = 0.01f;
        float rawTime = totalTime * cycleSpeed;
        float progress = rawTime - floor(rawTime);
        
        // Tilt Angle (45 degrees)
        float tilt = glm::radians(45.0f);
        float cosTilt = cos(tilt);
        float sinTilt = sin(tilt);

        // Update light state based on its active window
        auto updateCelestia = [&](Light& light, float t, float startT, float endT, float maxI, float minI) {
            float windowSize = endT - startT;
            
            // Handle wrapping logic if light spans across 1.0 -> 0.0 boundary
            float relativeT = t - startT;
            if (relativeT < 0.0f) relativeT += 1.0f;
            
            // Check if within window
            if (relativeT <= windowSize) {
                // Normalize to 0..1
                float windowProgress = relativeT / windowSize;
                // Map to 0..180 degrees (PI radians) for arc
                float angle = windowProgress * glm::pi<float>();
                
                float orbitalY = sin(angle);
                float orbitalX = cos(angle); // 1 -> -1 (East to West)
                
                float worldY = orbitalY * cosTilt;
                float worldZ = orbitalY * sinTilt;
                float worldX = orbitalX;

                // Direction is opposite to position
                glm::vec3 dir(-worldX, -worldY, -worldZ);
                light.direction = glm::normalize(dir);
                
                // Intensity changes using Y
                float ramp = glm::smoothstep(0.0f, 0.2f, worldY);
                light.intensity = glm::mix(minI, maxI, ramp);
            } else {
                // Inactive
                light.intensity = 0.0f;
                light.direction = glm::normalize(glm::vec3(-1.0f, 0.0f, 0.0f)); 
            }
        };

        // Sun: Active from 0.0 to 0.55 (Day)
        updateCelestia(lights[0], progress, 0.0f, 0.55f, 1.0f, 0.0f);
        
        // Moon: Active from 0.50 to 1.05 (Night) - Overlaps slightly with sun for dusk/dawn
        updateCelestia(lights[1], progress, 0.50f, 1.05f, 0.5f, 0.0f);

        // Disable shadow casting for lights that are off/too dim.
        // Sun (Index 0)
        lights[0].castShadows = (lights[0].intensity > 0.001f);

        // Moon (Index 1)
        lights[1].castShadows = (lights[1].intensity > 0.001f);
    }

    // Flicker Logic for Torches
    for (auto& light : lights) {
        if (light.flicker) {
             // Unique offset based on position to de-sync lights
             float seed = light.position.x * 12.9898f + light.position.y * 78.233f + light.position.z * 43.123f;
             float timeOffset = totalTime + (float)((int)seed % 100) / 10.0f; // Simple pseudo-random offset
             
             // Noise with sin
             float noise = (sin(timeOffset * 3.0f) + sin(timeOffset * 5.3f + 1.2f) + sin(timeOffset * 7.7f + 3.5f)) * 0.33f; // -1 to 1 approx
             
             // Intensity changes +/- 8%
             light.intensity = light.baseIntensity * (1.0f + noise * 0.08f);
             
             // Color changes
             glm::vec3 colorOffset = glm::vec3(noise * 0.03f, noise * 0.01f, 0.0f);
             light.color = glm::clamp(light.baseColor + colorOffset, 0.0f, 1.0f);
        }
    }
}

// Uploads the parameters to the GPU.
// Used by geometryPass to set material uniforms before drawing.
void Renderer::setModelMaterial(const ModelMaterial& material)
{
    if (geometryShader) {
        geometryShader->setVec3("albedo", material.params.albedo);
        geometryShader->setFloat("roughness", material.params.roughness);
        geometryShader->setFloat("specularShininess", material.params.specularShininess);
        geometryShader->setInt("materialType", static_cast<int>(material.model));
        geometryShader->setFloat("minnaertK", material.params.minnaertK);
        geometryShader->setFloat("orenNayarRoughness", material.params.orenNayarRoughness);
        geometryShader->setFloat("ashikhminShirleyNu", material.params.ashikhminShirleyNu);
        geometryShader->setFloat("ashikhminShirleyNv", material.params.ashikhminShirleyNv);
        geometryShader->setFloat("cookTorranceRoughness", material.params.cookTorranceRoughness);
        geometryShader->setFloat("cookTorranceF0", material.params.cookTorranceF0);
        geometryShader->setFloat("intensityCorrection", material.params.intensityCorrection);
        geometryShader->setFloat("ambientOcclusion", 1.0f); // Placeholder for AO texture that I will implement later
    }
}

// JSON Serialization for presets

#include <fstream>
#include <iomanip>

// JSON Helpers for GLM
namespace glm {
    inline void to_json(nlohmann::json& j, const glm::vec3& v) {
        j = nlohmann::json{v.x, v.y, v.z};
    }

    inline void from_json(const nlohmann::json& j, glm::vec3& v) {
        if(j.is_array() && j.size() >= 3) {
            v.x = j[0];
            v.y = j[1];
            v.z = j[2];
        }
    }
}

// Converts a MaterialParams struct into a JSON object.
json Renderer::serializeMaterialParams(const MaterialParams& params) {
    return {
        {"roughness", params.roughness},
        {"metallic", params.metallic},
        {"minnaertK", params.minnaertK},
        {"orenNayarRoughness", params.orenNayarRoughness},
        {"ashikhminShirleyNu", params.ashikhminShirleyNu},
        {"ashikhminShirleyNv", params.ashikhminShirleyNv},
        {"cookTorranceRoughness", params.cookTorranceRoughness},
        {"cookTorranceF0", params.cookTorranceF0},
        {"specularShininess", params.specularShininess},
        {"albedo", params.albedo}, // Uses glm helper
        {"enableQuantization", params.enableQuantization},
        {"diffuseQuantizationBands", params.diffuseQuantizationBands},
        {"specularThreshold1", params.specularThreshold1},
        {"specularThreshold2", params.specularThreshold2},
        {"intensityCorrection", params.intensityCorrection}
    };
}

// Converts a JSON object into a MaterialParams struct.
void Renderer::deserializeMaterialParams(const json& j, MaterialParams& params) {
    if (j.contains("roughness")) params.roughness = j["roughness"];
    if (j.contains("metallic")) params.metallic = j["metallic"];
    if (j.contains("minnaertK")) params.minnaertK = j["minnaertK"];
    if (j.contains("orenNayarRoughness")) params.orenNayarRoughness = j["orenNayarRoughness"];
    if (j.contains("ashikhminShirleyNu")) params.ashikhminShirleyNu = j["ashikhminShirleyNu"];
    if (j.contains("ashikhminShirleyNv")) params.ashikhminShirleyNv = j["ashikhminShirleyNv"];
    if (j.contains("cookTorranceRoughness")) params.cookTorranceRoughness = j["cookTorranceRoughness"];
    if (j.contains("cookTorranceF0")) params.cookTorranceF0 = j["cookTorranceF0"];
    if (j.contains("specularShininess")) params.specularShininess = j["specularShininess"];
    if (j.contains("albedo")) params.albedo = j["albedo"];
    if (j.contains("enableQuantization")) params.enableQuantization = j["enableQuantization"];
    if (j.contains("diffuseQuantizationBands")) params.diffuseQuantizationBands = j["diffuseQuantizationBands"];
    if (j.contains("specularThreshold1")) params.specularThreshold1 = j["specularThreshold1"];
    if (j.contains("specularThreshold2")) params.specularThreshold2 = j["specularThreshold2"];
    if (j.contains("intensityCorrection")) params.intensityCorrection = j["intensityCorrection"];
}

// Saves to a JSON file.
void Renderer::savePreset(int index) {
    std::string filename = "preset_" + std::to_string(index) + ".json";
    json root;

    // 1. Global Parameters (MaterialParams)
    root["globalParams"] = serializeMaterialParams(materialParams);
    
    // 2. Global Illumination Model
    root["globalIlluminationModel"] = static_cast<int>(globalIlluminationModel);

    // 3. Edge Parameters
    json edgeJson;
    edgeJson["enableOutlining"] = edgeParams.enableOutlining;
    edgeJson["depthThreshold"] = edgeParams.depthThreshold;
    edgeJson["normalThreshold"] = edgeParams.normalThreshold;
    edgeJson["sobelThreshold"] = edgeParams.sobelThreshold;
    edgeJson["colorThreshold"] = edgeParams.colorThreshold;
    edgeJson["edgeWidth"] = edgeParams.edgeWidth;
    edgeJson["edgeColor"] = edgeParams.edgeColor;
    // Advanced Params
    edgeJson["depthExponent"] = edgeParams.depthExponent;
    edgeJson["normalSplit"] = edgeParams.normalSplit;
    edgeJson["sobelScale"] = edgeParams.sobelScale;
    edgeJson["smoothWidth"] = edgeParams.smoothWidth;
    edgeJson["laplacianThreshold"] = edgeParams.laplacianThreshold;
    edgeJson["laplacianScale"] = edgeParams.laplacianScale;
    root["edgeParams"] = edgeJson;
    root["edgeDetectionFlags"] = edgeDetectionFlags;

    // 4. Shadow Parameters
    json shadowJson;
    shadowJson["shadowMapSize"] = shadowParams.shadowMapSize;
    shadowJson["cubeShadowMapSize"] = shadowParams.cubeShadowMapSize;
    shadowJson["shadowBias"] = shadowParams.shadowBias;
    shadowJson["shadowNormalBias"] = shadowParams.shadowNormalBias;
    shadowJson["shadowPCFSamples"] = shadowParams.shadowPCFSamples;
    shadowJson["shadowIntensity"] = shadowParams.shadowIntensity;
    shadowJson["enablePCF"] = shadowParams.enablePCF;
    // Save shadow params
    root["shadowParams"] = shadowJson;

    // 5. Per-Model Materials
    json modelsJson;
    auto saveModelMat = [&](const std::string& key, const ModelMaterial& mat) {
        json matJson;
        matJson["model"] = static_cast<int>(mat.model);
        matJson["params"] = serializeMaterialParams(mat.params);
        matJson["name"] = mat.name; // Save name just in case
        modelsJson[key] = matJson;
    };

    saveModelMat("floor", floorMaterial);
    saveModelMat("wall", wallMaterial);
    saveModelMat("ceiling", ceilingMaterial);
    saveModelMat("woodFloor", woodFloorMaterial);
    saveModelMat("stair", stairMaterial);
    saveModelMat("torch", torchMaterial);
    saveModelMat("dirt", dirtMaterial);
    saveModelMat("table", tableMaterial);
    saveModelMat("chair", chairMaterial);
    saveModelMat("stool", stoolMaterial);
    saveModelMat("barrel", barrelMaterial);
    saveModelMat("shelf", shelfMaterial);
    saveModelMat("bed", bedMaterial);
    saveModelMat("chest", chestMaterial);
    saveModelMat("banner", bannerMaterial);
    saveModelMat("candle", candleMaterial);
    saveModelMat("crate", crateMaterial);
    saveModelMat("swordShield", swordShieldMaterial);

    saveModelMat("woodPallet", woodPalletMaterial);
    saveModelMat("woodPlanks", woodPlanksMaterial);
    saveModelMat("stoneStack", stoneStackMaterial);
    saveModelMat("goldBars", goldBarsMaterial);
    saveModelMat("metalParts", metalPartsMaterial);
    saveModelMat("textiles", textilesMaterial);

    root["models"] = modelsJson;

    // Write to file
    try {
        std::ofstream file(filename);
        file << std::setw(4) << root << std::endl;
        std::cout << "Saved preset to " << filename << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Failed to save preset: " << e.what() << std::endl;
    }
}

// Reads a JSON file and restores the parameters.
void Renderer::loadPreset(int index) {
    std::string filename = "preset_" + std::to_string(index) + ".json";
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Preset file " << filename << " not found." << std::endl;
        return;
    }

    try {
        json root;
        file >> root;

        // 1. Global Parameters
        if(root.contains("globalParams")) {
             deserializeMaterialParams(root["globalParams"], materialParams);
        }

        // 2. Global Illumination Model
        if(root.contains("globalIlluminationModel")) {
            globalIlluminationModel = static_cast<IlluminationModel>(root["globalIlluminationModel"]);
        }

        // 3. Edge Parameters
        if(root.contains("edgeParams")) {
            json& ej = root["edgeParams"];
            if(ej.contains("enableOutlining")) edgeParams.enableOutlining = ej["enableOutlining"];
            if(ej.contains("depthThreshold")) edgeParams.depthThreshold = ej["depthThreshold"];
            if(ej.contains("normalThreshold")) edgeParams.normalThreshold = ej["normalThreshold"];
            if(ej.contains("sobelThreshold")) edgeParams.sobelThreshold = ej["sobelThreshold"];
            if(ej.contains("colorThreshold")) edgeParams.colorThreshold = ej["colorThreshold"];
            if(ej.contains("edgeWidth")) edgeParams.edgeWidth = ej["edgeWidth"];
            if(ej.contains("edgeColor")) edgeParams.edgeColor = ej["edgeColor"];
            // Advanced Params
            if(ej.contains("depthExponent")) edgeParams.depthExponent = ej["depthExponent"];
            if(ej.contains("normalSplit")) edgeParams.normalSplit = ej["normalSplit"];
            if(ej.contains("sobelScale")) edgeParams.sobelScale = ej["sobelScale"];
            if(ej.contains("smoothWidth")) edgeParams.smoothWidth = ej["smoothWidth"];
            if(ej.contains("laplacianThreshold")) edgeParams.laplacianThreshold = ej["laplacianThreshold"];
            if(ej.contains("laplacianScale")) edgeParams.laplacianScale = ej["laplacianScale"];
        }
        if(root.contains("edgeDetectionFlags")) edgeDetectionFlags = root["edgeDetectionFlags"];

        // 4. Shadow Parameters
        if(root.contains("shadowParams")) {
            json& sj = root["shadowParams"];
            if(sj.contains("shadowMapSize")) shadowParams.shadowMapSize = sj["shadowMapSize"];
             if(sj.contains("cubeShadowMapSize")) shadowParams.cubeShadowMapSize = sj["cubeShadowMapSize"];
            if(sj.contains("shadowBias")) shadowParams.shadowBias = sj["shadowBias"];
            if(sj.contains("shadowNormalBias")) shadowParams.shadowNormalBias = sj["shadowNormalBias"];
            if(sj.contains("shadowPCFSamples")) shadowParams.shadowPCFSamples = sj["shadowPCFSamples"];
            if(sj.contains("shadowIntensity")) shadowParams.shadowIntensity = sj["shadowIntensity"];
            if(sj.contains("enablePCF")) shadowParams.enablePCF = sj["enablePCF"];
        }

        // 5. Per-Model Materials
        if(root.contains("models")) {
            json& mj = root["models"];
            auto loadModelMat = [&](const std::string& key, ModelMaterial& mat) {
                if (mj.contains(key)) {
                    json& item = mj[key];
                    if (item.contains("model")) mat.model = static_cast<IlluminationModel>(item["model"]);
                    if (item.contains("params")) deserializeMaterialParams(item["params"], mat.params);
                }
            };

            loadModelMat("floor", floorMaterial);
            loadModelMat("wall", wallMaterial);
            loadModelMat("ceiling", ceilingMaterial);
            loadModelMat("woodFloor", woodFloorMaterial);
            loadModelMat("stair", stairMaterial);
            loadModelMat("torch", torchMaterial);
            loadModelMat("dirt", dirtMaterial);
            loadModelMat("table", tableMaterial);
            loadModelMat("chair", chairMaterial);
            loadModelMat("stool", stoolMaterial);
            loadModelMat("barrel", barrelMaterial);
            loadModelMat("shelf", shelfMaterial);
            loadModelMat("bed", bedMaterial);
            loadModelMat("chest", chestMaterial);
            loadModelMat("banner", bannerMaterial);
            loadModelMat("candle", candleMaterial);
            loadModelMat("crate", crateMaterial);
            loadModelMat("swordShield", swordShieldMaterial);
            loadModelMat("woodPallet", woodPalletMaterial);
            loadModelMat("woodPlanks", woodPlanksMaterial);
            loadModelMat("stoneStack", stoneStackMaterial);
            loadModelMat("goldBars", goldBarsMaterial);
            loadModelMat("metalParts", metalPartsMaterial);
            loadModelMat("textiles", textilesMaterial);
        }

        std::cout << "Loaded preset from " << filename << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Failed to parse preset file: " << e.what() << std::endl;
    }
}

// Called by destructor.
void Renderer::cleanup()
{
    // Clean up shadow maps
    for (auto& shadowData : shadowMaps) {
        cleanupShadowMap(shadowData);
    }
    
    if (lightingFBO) glDeleteFramebuffers(1, &lightingFBO);
    if (lightingTexture) glDeleteTextures(1, &lightingTexture);
    if (edgeFBO) glDeleteFramebuffers(1, &edgeFBO);
    if (edgeTexture) glDeleteTextures(1, &edgeTexture);
    if (quadVAO) glDeleteVertexArrays(1, &quadVAO);
    if (quadVBO) glDeleteBuffers(1, &quadVBO);
}

// Frees the FBO and Textures associated with a shadow map.
// Resets the struct state.
void Renderer::cleanupShadowMap(ShadowMapData& shadowData)
{
    if (shadowData.FBO) {
        glDeleteFramebuffers(1, &shadowData.FBO);
        shadowData.FBO = 0;
    }
    if (shadowData.depthMap) {
        glDeleteTextures(1, &shadowData.depthMap);
        shadowData.depthMap = 0;
    }
    if (shadowData.depthCubemap) {
        glDeleteTextures(1, &shadowData.depthCubemap);
        shadowData.depthCubemap = 0;
    }
    shadowData.isActive = false;
    shadowData.hasRendered = false;
}

// Helper to compute the logical view-projection matrix for a light source.
glm::mat4 Renderer::calculateLightSpaceMatrix(const Light& light) const
{
    glm::mat4 lightProjection = glm::mat4(1.0f);
    glm::mat4 lightView = glm::mat4(1.0f);
    float nearPlane = shadowParams.nearPlane;
    float farPlane = shadowParams.farPlane;

    if(light.type == LightType::DIRECTIONAL) {
        float size = shadowParams.orthoSize;
        lightProjection = glm::ortho(-size, size, -size, size, nearPlane, farPlane);
        
        glm::vec3 lightDir = glm::normalize(light.direction);
        glm::vec3 lightPos = -lightDir * (farPlane * 0.5f);
        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
        
        if(fabs(glm::dot(lightDir, up)) > 0.99f)
            up = glm::vec3(1.0f, 0.0f, 0.0f);
            
        lightView = glm::lookAt(lightPos, glm::vec3(0.0f), up);
    } else if(light.type == LightType::SPOT) {
        float aspect = 1.0f;
        float fov = glm::degrees(acos(light.outerCutOff)) * 2.0f; 
        lightProjection = glm::perspective(glm::radians(std::max(1.0f, fov)), aspect, nearPlane, farPlane);
        
        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
        if(fabs(glm::dot(glm::normalize(light.direction), up)) > 0.99f)
             up = glm::vec3(1.0f, 0.0f, 0.0f);
             
        lightView = glm::lookAt(light.position, light.position + light.direction, up);
    }
    
    return lightProjection * lightView;
}
