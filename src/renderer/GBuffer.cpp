#include "GBuffer.h"
#include <iostream>

GBuffer::GBuffer() : gBuffer(0), gBaseColor(0), gNormal(0), gPosition(0), gQuantization(0), gDepth(0), rboDepth(0), width(0), height(0)
{
}

GBuffer::~GBuffer()
{
    cleanup();
}

bool GBuffer::init(unsigned int windowWidth, unsigned int windowHeight)
{
    width = windowWidth;
    height = windowHeight;

    glGenFramebuffers(1, &gBuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, gBuffer);

    // Target 0: Albedo & Info
    // RGB: Base Color (Diffuse Albedo)
    // A:   Material
    glGenTextures(1, &gBaseColor);
    glBindTexture(GL_TEXTURE_2D, gBaseColor);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gBaseColor, 0);

    //Target 1: Normals & Roughness
    // RGB: World-space normal, A: Roughness
    glGenTextures(1, &gNormal);
    glBindTexture(GL_TEXTURE_2D, gNormal);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gNormal, 0);

    // Target 2: Position & Metallic
    // RGB: World Space Position. Essential for calculating light direction/distance per pixel.
    // A:   Metallic factor (0.0 = Dielectric, 1.0 = Metal).
    glGenTextures(1, &gPosition);
    glBindTexture(GL_TEXTURE_2D, gPosition);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, gPosition, 0);

    // Target 3: Stylization Data
    // RGB: Quantization / Cel-Shading Control flags (e.g. number of bands)
    // A:   I was planning to use it for Ambient Occlusion but not used for now.
    glGenTextures(1, &gQuantization);
    glBindTexture(GL_TEXTURE_2D, gQuantization);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, gQuantization, 0);

    // Depth Buffer
    // I need to use a depth texture to read depth later for edge detection.
    // GL_DEPTH_COMPONENT32F is the most precise one, i'm not sure it's needed but seems to have no impact.
    glGenTextures(1, &gDepth);
    glBindTexture(GL_TEXTURE_2D, gDepth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gDepth, 0);

    // The Fragment Shader in Geometry Pass will output to locations 0, 1, 2, 3 that are the same as this array.
    unsigned int attachments[4] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3 };
    glDrawBuffers(4, attachments);

    // Verify the framebuffer.
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cout << "ERROR::GBUFFER::Framebuffer not complete!" << std::endl;
        cleanup();
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void GBuffer::bind()
{
    // Bind as the target for RENDER operations (Writing)
    glBindFramebuffer(GL_FRAMEBUFFER, gBuffer);
}

void GBuffer::unbind()
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GBuffer::bindForReading()
{
    glBindFramebuffer(GL_READ_FRAMEBUFFER, gBuffer);
}

void GBuffer::bindForWriting()
{
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gBuffer);
}

void GBuffer::resize(unsigned int newWidth, unsigned int newHeight)
{
    if (width == newWidth && height == newHeight) return;
    
    // Full cleanup and re-init is safer than resizing.
    cleanup();
    init(newWidth, newHeight);
}

void GBuffer::cleanup()
{
    if (gBuffer) {
        glDeleteFramebuffers(1, &gBuffer);
        gBuffer = 0;
    }
    if (gBaseColor) {
        glDeleteTextures(1, &gBaseColor);
        gBaseColor = 0;
    }
    if (gNormal) {
        glDeleteTextures(1, &gNormal);
        gNormal = 0;
    }
    if (gPosition) {
        glDeleteTextures(1, &gPosition);
        gPosition = 0;
    }
    if (gQuantization) {
        glDeleteTextures(1, &gQuantization);
        gQuantization = 0;
    }
    if (gDepth) {
        glDeleteTextures(1, &gDepth);
        gDepth = 0;
    }
    if (rboDepth) {
        glDeleteRenderbuffers(1, &rboDepth);
        rboDepth = 0;
    }
}