//Main Entry Point
//Window creation, OpenGL context init, and main render loop.
//Bridges raw input to Camera/GUI.

#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "renderer/Renderer.h"
#include "camera/Camera.h"
#include "ui/GUI.h"

// Force usage of discrete GPU on laptops with hybrid graphics

extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

const unsigned int WINDOW_WIDTH = 1920;
const unsigned int WINDOW_HEIGHT = 1080;

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void processInput(GLFWwindow* window);

Camera camera(glm::vec3(0.0f, 0.0f, 3.0f));
float lastX = WINDOW_WIDTH / 2.0f;
float lastY = WINDOW_HEIGHT / 2.0f;
bool firstMouse = true;
bool guiMode = false;  // Toggle for GUI vs camera control mode

float deltaTime = 0.0f;
float lastFrame = 0.0f;

int main()
{
    // Initialize GLFW. This is required before any other GLFW functions can be called.
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    // Configure the OpenGL context.
    // i set it to OpenGL 4.6 beacuse its the newest, but should work with other profiles aswell
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

#ifdef __APPLE__
    // Required for macOS, idk why but dont ever remove it.
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    // Create the main window object.
    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Cel Shading Renderer", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }

    // specific the context of our window as the main context on the current thread.
    glfwMakeContextCurrent(window);
    
    // Set callbacks for input handling. GLFW will invoke these when events occur.
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);

    // Disable the cursor (lock it to the window center) for standard FPS camera controls.
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // Initialize GLAD.
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    // Enable depth testing
    glEnable(GL_DEPTH_TEST);

    // Initialize renderer and imGUI.
    auto renderer = std::make_unique<Renderer>(WINDOW_WIDTH, WINDOW_HEIGHT);
    auto gui = std::make_unique<GUI>(window);
    gui->setRenderer(renderer.get());
    
    // Store renderer pointer for callbacks
    glfwSetWindowUserPointer(window, renderer.get());
    gui->setCamera(&camera);
    
    // Load default preset (Preset 1)
    renderer->loadPreset(0);

    // Main Loop
    while (!glfwWindowShouldClose(window)) {
        // Delta time for frame-rate independent movement
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // Process inputs
        processInput(window);

        // Clear the screen and prepare buffers
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Execute the rendering pipeline.
        renderer->render(camera, deltaTime);
        
        // Render the UI overlay on top of the 3D scene.
        gui->render();

        // Swap the front and back buffers.
        // We render to the back buffer and swap to prevent screen tearing/flickering.
        glfwSwapBuffers(window);
        
        // Check for new events update state.
        glfwPollEvents();
    }

    // Clean up resources
    gui.reset();
    renderer.reset();
    glfwTerminate();
    return 0;
}

void processInput(GLFWwindow* window)
{
    // Close the window if ESC is pressed.
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    // Toggle logic for GUI mode. We use a static boolean to track the previous key state so we only toggle once per key press
    static bool tabPressed = false;
    if (glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS && !tabPressed) {
        guiMode = !guiMode;
        if (guiMode) {
            // Unbind the mouse to allow cursor interaction with the ImGui window.
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        } else {
            // Re-capture the mouse for camera control.
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            // Reset firstMouse to avoid a large view jump caused by the cursor position 
            // having moved while in GUI mode.
            firstMouse = true; 
        }
        tabPressed = true;
    }
    // Reset the toggle lock when the key is released.
    if (glfwGetKey(window, GLFW_KEY_TAB) == GLFW_RELEASE) {
        tabPressed = false;
    }

    // Camera movement logic is only active when not interacting with the GUI (TODO: in theory but this is somehow bugged).
    // pass deltaTime to ensure movement speed is frame-rate independent.
    if (!guiMode) {
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            camera.processKeyboard(FORWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            camera.processKeyboard(BACKWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            camera.processKeyboard(LEFT, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            camera.processKeyboard(RIGHT, deltaTime);
    }
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    // Update the OpenGL viewport
    glViewport(0, 0, width, height);

    // Retrieve the renderer validation and update its internal size
    Renderer* renderer = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
    if (renderer) {
        renderer->resize(width, height);
    }
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    // If we're in GUI mode, the mouse is used for UI interaction, so we ignore it for camera look.
    if (guiMode) return;

    float xposf = static_cast<float>(xpos);
    float yposf = static_cast<float>(ypos);

    // Handle the initial entry into the window. Without this check, the jump from (0,0) 
    // to the actual mouse position would cause a sudden, large camera movement.
    if (firstMouse) {
        lastX = xposf;
        lastY = yposf;
        firstMouse = false;
    }

    // Calculate the mouse offset since the last frame.
    // Standard FPS camera setup: +x is right, +y is up (but y is often inverted in screen coords,
    // though here we subtract ypos from lastY which handles the inversion for pitch).
    float xoffset = xposf - lastX;
    float yoffset = lastY - yposf; // Reversed since y-coordinates range from bottom to top

    lastX = xposf;
    lastY = yposf;

    camera.processMouseMovement(xoffset, yoffset);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    // Adjust camera zoom (FOV) based on scroll wheel input.
    if (!guiMode) {
        camera.processMouseScroll(static_cast<float>(yoffset));
    }
}