/*
 * JSON Serialization Helpers
 * --------------------------
 * Bridges the gap between the GLM math library and our JSON persistence layer.
 * Without this, saving vector data would be a manual nightmare.
 * 
 * We inject these functions directly into the `glm` namespace so the JSON library
 * can find them via Argument Dependent Lookup (ADL) automatically.
 */

#pragma once
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

namespace glm {
    // Encodes a vec3 into a simple JSON array [x, y, z].
    // Usage: json j = myVec3;
    inline void to_json(nlohmann::json& j, const glm::vec3& v) {
        j = nlohmann::json{v.x, v.y, v.z};
    }

    // Decoding logic: careful unpacking to ensure we don't crash on malformed data.
    // Usage: glm::vec3 v = j.get<glm::vec3>();
    inline void from_json(const nlohmann::json& j, glm::vec3& v) {
        // Make sure it's actually an array with enough components.
        // If the JSON is malformed (e.g., null or too short), we leave the vector untouched.
        if(j.is_array() && j.size() >= 3) {
            v.x = j[0];
            v.y = j[1];
            v.z = j[2];
        }
    }
}
