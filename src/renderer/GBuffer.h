#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

class GBuffer
{
public:
    GBuffer();
    ~GBuffer();

    // Sets up the framebuffer and all textures.
    bool init(unsigned int windowWidth, unsigned int windowHeight);
    
    // Bind this to start geometry data.
    void bind();
    
    // Go back to the default framebuffer (screen).
    void unbind();
    
    void bindForReading();
    void bindForWriting();
    
    // Handle window resizing, recreating textures as needed.
    void resize(unsigned int width, unsigned int height);

    // Get textures for the Lighting Pass
    
    unsigned int getBaseColorTexture() const { return gBaseColor; }  // Target 0: Diffuse Color + Material ID
    unsigned int getNormalTexture() const { return gNormal; }        // Target 1: Surface Normal vector + Roughness
    unsigned int getPositionTexture() const { return gPosition; }    // Target 2: World Space Position + Metallic
    unsigned int getQuantizationTexture() const { return gQuantization; } // Target 3: Custom data for Cel Shading + AO
    unsigned int getDepthTexture() const { return gDepth; }          // Depth Buffer

private:
    unsigned int gBuffer;
    unsigned int gBaseColor, gNormal, gPosition, gQuantization, gDepth;
    unsigned int rboDepth;
    unsigned int width, height;

    void cleanup();
};