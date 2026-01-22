#version 460 core

out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D lightingTexture;
uniform sampler2D edgeTexture;

uniform bool enableOutlining;

void main()
{
    vec3 lighting = texture(lightingTexture, TexCoords).rgb;
    vec4 edge = texture(edgeTexture, TexCoords);
    
    vec3 finalColor = lighting;
    
    if (enableOutlining) {
        // Blend lighting with edges
        finalColor = mix(lighting, edge.rgb, edge.a);
    }
    
    FragColor = vec4(finalColor, 1.0);
}