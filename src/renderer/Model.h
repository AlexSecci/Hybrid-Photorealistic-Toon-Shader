#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <vector>
#include <string>
#include <memory>

struct Vertex {
    glm::vec3 Position;  
    glm::vec3 Normal;    
    glm::vec2 TexCoords; 
};

struct Texture {
    unsigned int id;
    std::string type; // e.g., "texture_diffuse", "texture_specular"
    std::string path; // Used to prevent reloading the same texture twice
};

// I need this beacause some of my models are made of multiple Meshes
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<Texture> textures;
    unsigned int VAO, VBO, EBO;
    
    // Uploads the geometry to the GPU.
    void setupMesh();
    void draw();
};

class Model {
public:
    Model(const std::string& path);
    ~Model();
    
    // Draws every mesh in this model.
    void draw();
    
    // Helper to check if we actually loaded any textures.
    bool hasTexture() const { return !textures_loaded.empty(); }
    
    // Returns the ID of the first loaded texture (usually diffuse) or 0 if none.
    unsigned int getDiffuseTexture() const { return !textures_loaded.empty() ? textures_loaded[0].id : 0; }
    
    size_t getVertexCount() const;
    
private:
    std::vector<Mesh> meshes;
    std::vector<Texture> textures_loaded;
    std::string directory;
    
    // recursive brain of the operation
    void loadModel(const std::string& path);
    void processNode(aiNode* node, const aiScene* scene);
    Mesh processMesh(aiMesh* mesh, const aiScene* scene);
    
    // Material parsing magic
    std::vector<Texture> loadMaterialTextures(aiMaterial* mat, aiTextureType type, std::string typeName);
    unsigned int TextureFromFile(const char* path, const std::string& directory);
};