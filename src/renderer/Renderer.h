#pragma once

#include <memory>
#include <vector>
#include <array>
#include <string>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include "GBuffer.h"
#include "Shader.h"
#include "Model.h"
#include "../camera/Camera.h"
#include "../lighting/LightManager.h"

using json = nlohmann::json;

enum class IlluminationModel {
    LAMBERTIAN,        // Standard diffuse
    MINNAERT,          // Velvet-like cloth
    OREN_NAYAR,        // Dusty/Rough surfaces
    ASHIKHMIN_SHIRLEY, // Anisotropic metal
    COOK_TORRANCE      // Microfacet PBR
};

// Flags for which edge detection algorithms are active.
enum class EdgeDetectionType {
    DEPTH_BASED = 1,  
    NORMAL_BASED = 2, 
    SOBEL = 4,        
    COLOR_BASED = 8,  
    LAPLACIAN = 16    
};

class Renderer
{
public:
    Renderer(unsigned int width, unsigned int height);
    ~Renderer();

    void render(const Camera& camera, float deltaTime);
    
    void updateLights(float deltaTime); 
    
    void resize(unsigned int width, unsigned int height);
    
    // Presets
    void applyPreset(int index);
    void savePreset(int index);
    void loadPreset(int index);
    void resetParamsToDefaults();

    // Edge Detection
    void setEdgeDetectionFlags(int flags) { edgeDetectionFlags = flags; }
    int getEdgeDetectionFlags() const { return edgeDetectionFlags; }

    struct Stats {
        unsigned int drawCalls = 0;
        unsigned int vertexCount = 0;
    };
    
    const Stats& getStats() const { return stats; }
    void resetStats() { stats = {0, 0}; }

    // Every parameter for the materials
    struct MaterialParams {
        float roughness = 0.1f;              
        float metallic = 0.0f;               
        float minnaertK = 1.2f;              
        float orenNayarRoughness = 0.3f;
        float ashikhminShirleyNu = 100.0f;
        float ashikhminShirleyNv = 100.0f;
        float cookTorranceRoughness = 0.3f;
        float cookTorranceF0 = 0.5f;     
        float specularShininess = 32.0f;
        glm::vec3 albedo = glm::vec3(0.2f, 0.7f, 0.9f);
        
        // Cel shading quantization
        bool enableQuantization = true;      
        int diffuseQuantizationBands = 5;    
        float specularThreshold1 = 0.3f;     
        float specularThreshold2 = 0.7f;     
        float intensityCorrection = 1.0f;    
    };
    
    // Binds a material configuration to a specific object type.
    struct ModelMaterial {
        IlluminationModel model = IlluminationModel::LAMBERTIAN;
        MaterialParams params;
        std::string name;
    };

    // Helper for JSON serialization
    json serializeMaterialParams(const MaterialParams& params);
    void deserializeMaterialParams(const json& j, MaterialParams& params);
    
    // Materials
    MaterialParams materialParams;  
    IlluminationModel globalIlluminationModel = IlluminationModel::LAMBERTIAN;
    
    ModelMaterial floorMaterial = {{}, {}, "Floor"};
    ModelMaterial wallMaterial = {{}, {}, "Walls"};
    ModelMaterial ceilingMaterial = {{}, {}, "Ceiling"};
    ModelMaterial woodFloorMaterial = {{}, {}, "WoodFloor"};
    ModelMaterial stairMaterial = {{}, {}, "Stair"};
    ModelMaterial torchMaterial = {{}, {}, "Torch"};
    ModelMaterial dirtMaterial = {{}, {}, "Grass/Dirt"};
    ModelMaterial tableMaterial = {{}, {}, "Table"};
    ModelMaterial chairMaterial = {{}, {}, "Chair"};
    ModelMaterial stoolMaterial = {{}, {}, "Stool"};
    ModelMaterial barrelMaterial = {{}, {}, "Barrel"};
    ModelMaterial woodPalletMaterial = {{}, {}, "Wood Pallet"};
    ModelMaterial woodPlanksMaterial = {{}, {}, "Wood Planks"};
    ModelMaterial stoneStackMaterial = {{}, {}, "Stone Stack"};
    ModelMaterial goldBarsMaterial = {{}, {}, "Gold Bars"};
    ModelMaterial metalPartsMaterial = {{}, {}, "Metal Parts"};
    ModelMaterial textilesMaterial = {{}, {}, "Textiles"};
    ModelMaterial shelfMaterial = {{}, {}, "Shelf"};
    ModelMaterial bedMaterial = {{}, {}, "Bed"};
    ModelMaterial chestMaterial = {{}, {}, "Chest"};
    ModelMaterial bannerMaterial = {{}, {}, "Banner"};
    ModelMaterial candleMaterial = {{}, {}, "Candles"};
    ModelMaterial crateMaterial = {{}, {}, "Crates"};
    ModelMaterial swordShieldMaterial = {{}, {}, "Sword & Shield"};

    // Edge Detection
    struct EdgeParams {
        bool enableOutlining = true;
        float depthThreshold = 0.1f;
        float normalThreshold = 0.5f;
        float sobelThreshold = 0.3f;
        float colorThreshold = 0.2f;
        float edgeWidth = 1.0f;
        glm::vec3 edgeColor = glm::vec3(0.0f, 0.0f, 0.0f);
        
        // Fine-tuning controls
        float depthExponent = 1.0f; 
        float normalSplit = 0.5f;   
        float sobelScale = 1.0f;    
        float smoothWidth = 1.0f;   
        float laplacianThreshold = 0.5f; 
        float laplacianScale = 1.0f;     
    } edgeParams;

    // Shadows
    struct ShadowParams {
        int shadowMapSize = 2048;           
        int cubeShadowMapSize = 1024;       
        float shadowBias = 0.005f;          
        float shadowNormalBias = 0.02f;     
        int shadowPCFSamples = 2;           
        float shadowIntensity = 0.7f;       
        bool enablePCF = true;              
        float orthoSize = 20.0f;            
        float nearPlane = 0.5f;             
        float farPlane = 50.0f;             
    } shadowParams;

    // Crazy Mode
    bool isCrazyMode = false;
    
    struct CrazyTorchParams {
        float speed;
        float radius;
        float angle;
        glm::vec3 centerOffset;
        glm::vec3 color;
        size_t lightIndex;
    };
    std::vector<CrazyTorchParams> crazyTorchParams;
    
    void setCrazyMode(bool enable);
    void updateCrazyTorches(float deltaTime);

    float crazyModeTime = 0.0f;

    LightManager& getLightManager() { return *lightManager; }

private:
    unsigned int width, height;
    std::unique_ptr<GBuffer> gBuffer;
    std::unique_ptr<LightManager> lightManager;

    // Shadow map data structure for each light
    struct ShadowMapData {
        unsigned int FBO = 0;
        unsigned int depthMap = 0;        // For directional/spot lights
        unsigned int depthCubemap = 0;     // For point lights
        glm::mat4 lightSpaceMatrix;       // For directional/spot
        std::array<glm::mat4, 6> shadowTransforms; // For point lights (6 cube faces)
        bool isActive = false;
        LightType type;
        int size = 0;
        bool hasRendered = false; // For static lights
    };
    
    static const size_t MAX_SHADOW_CASTING_LIGHTS = 8;
    std::vector<ShadowMapData> shadowMaps;

    // Pipeline Shaders
    std::unique_ptr<Shader> geometryShader;       // Pass 1: Fill G-Buffer
    std::unique_ptr<Shader> shadowMapShader;      // Pass 0a: Depth map (Dir/Spot)
    std::unique_ptr<Shader> pointShadowShader;    // Pass 0b: Cube depth map (Point)
    std::unique_ptr<Shader> hybridCelShader;      // Pass 2: Lighting & Cel Shading
    std::unique_ptr<Shader> edgeDetectionShader;  // Pass 3: Edge Filters
    std::unique_ptr<Shader> compositeShader;      // Pass 4: Final Mix

    // Render targets result textures
    unsigned int lightingFBO;
    unsigned int lightingTexture;
    unsigned int edgeFBO;
    unsigned int edgeTexture;

    // Quad for screen-space rendering
    unsigned int quadVAO, quadVBO;
    
    // 3D Models pointers
    std::unique_ptr<Model> floorTileModel;
    std::unique_ptr<Model> wallModel;
    std::unique_ptr<Model> cornerModel;
    std::unique_ptr<Model> doorwayModel;
    std::unique_ptr<Model> windowOpenModel;
    std::unique_ptr<Model> windowClosedModel;
    std::unique_ptr<Model> ceilingModel;
    std::unique_ptr<Model> woodFloorModel;
    std::unique_ptr<Model> stairModel;
    std::unique_ptr<Model> torchModel;
    std::unique_ptr<Model> floorDirtLargeModel;
    std::unique_ptr<Model> floorDirtLargeRockyModel;
    std::unique_ptr<Model> floorDirtSmallAModel;
    std::unique_ptr<Model> floorDirtSmallBModel;
    std::unique_ptr<Model> floorDirtSmallCModel;
    std::unique_ptr<Model> floorDirtSmallDModel;
    std::unique_ptr<Model> floorDirtSmallWeedsModel;
    std::unique_ptr<Model> tableLongDecoratedModel;
    std::unique_ptr<Model> chairModel;
    std::unique_ptr<Model> stoolModel;
    std::unique_ptr<Model> barrelModel;
    std::unique_ptr<Model> shelfSmallCandlesModel;
    std::unique_ptr<Model> woodPalletModel;
    std::unique_ptr<Model> woodPlanksModel;
    std::unique_ptr<Model> stoneStackModel;
    std::unique_ptr<Model> goldBarsModel;
    std::unique_ptr<Model> metalPartsModel;
    std::unique_ptr<Model> textilesModel;
    std::unique_ptr<Model> bedModel;
    std::unique_ptr<Model> chestGoldModel;
    std::unique_ptr<Model> bannerModel;
    std::unique_ptr<Model> candleTripleModel;
    std::unique_ptr<Model> crateStackModel;
    std::unique_ptr<Model> swordShieldModel;

    int edgeDetectionFlags;

    // Initialization
    void initializeShaders();
    void initializeRenderTargets();
    void initializeShadowMapping();
    void initializeLights();
    void initializeQuad();
    void loadModels();
    void initializeModelMaterials();
    
    // Shadow pass
    void updateShadowMaps();
    
    // Rendering stages
    void renderScene(Shader* shader, const glm::mat4& viewProjection = glm::mat4(1.0f));
    void renderDecorations(Shader* shader); 
    void renderModel(const std::unique_ptr<Model>& model, const glm::mat4& modelMatrix, Shader* shader);
    void setModelMaterial(const ModelMaterial& material);
    
    void shadowMapPass();
    void renderShadowMapForLight(size_t lightIndex, const Light& light, ShadowMapData& shadowData);
    void renderDirectionalShadow(const Light& light, ShadowMapData& shadowData);
    void renderPointShadow(const Light& light, ShadowMapData& shadowData);
    void renderSpotShadow(const Light& light, ShadowMapData& shadowData);
    
    void geometryPass(const Camera& camera);  // Fill G-Buffer
    void lightingPass(const Camera& camera);  // Calculate lighting
    void edgeDetectionPass();                 // Draw outlines
    void compositePass();                     // Combine layers
    void renderQuad();                        // Helper for screen-space effects
    
    void cleanup();
    void cleanupShadowMap(ShadowMapData& shadowData);
    
    glm::mat4 calculateLightSpaceMatrix(const Light& light) const;
    
    Stats stats;
};
