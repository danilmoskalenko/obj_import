#version 330 core
out vec4 FragColor;

in vec2 TexCoords;
in vec3 Normal;
in vec3 FragPos;
in vec4 FragPosLightSpace;

uniform sampler2D texture_diffuse1;
uniform sampler2D shadowMap;
uniform int lightingMode;
uniform int normalMode;
uniform vec3 viewPos;
uniform vec3 backgroundColor;
uniform vec3 objectColor;
uniform vec3 lightPos;
uniform vec3 lightDir;
uniform vec3 lightColor;
uniform float ambientStrength;
uniform float diffuseStrength;
uniform float specularStrength;
uniform float shininess;
uniform float spotCutOff;
uniform float spotOuterCutOff;
uniform float constant;
uniform float linear;
uniform float quadratic;

float ShadowCalculation(vec4 fragPosLightSpace, vec3 lightDirNorm, vec3 norm)
{
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    if (projCoords.z > 1.0)
        return 0.0;
    float closestDepth = texture(shadowMap, projCoords.xy).r;
    float currentDepth = projCoords.z;
    float bias = max(0.005 * (1.0 - dot(norm, lightDirNorm)), 0.0005);
    float shadow = currentDepth - bias > closestDepth ? 1.0 : 0.0;
    return shadow;
}

void main()
{
    vec3 norm = normalize(Normal);
    if (length(norm) < 0.001) {
        norm = vec3(0.0, 1.0, 0.0);
    }
    vec4 texColor = texture(texture_diffuse1, TexCoords);
    vec3 result = vec3(0.0);

    if (lightingMode == 0) {
        result = texColor.rgb * objectColor;
    }
    else if (lightingMode == 1) {
        result = backgroundColor;
    }
    else {
        vec3 effectiveLightColor = length(lightColor) < 0.001 ? vec3(1.0) : lightColor;
        vec3 ambient = ambientStrength * effectiveLightColor * texColor.rgb * objectColor;

        vec3 lightDirNorm;
        float attenuation = 1.0;
        if (lightingMode == 3 || lightingMode == 5) {
            lightDirNorm = normalize(lightPos - FragPos);
            if (lightingMode == 5) {
                float distance = length(lightPos - FragPos);
                attenuation = 1.0 / (constant + linear * distance + quadratic * (distance * distance));
            }
        }
        else {
            lightDirNorm = normalize(-lightDir);
            if (length(lightDirNorm) < 0.001) {
                lightDirNorm = vec3(0.0, -1.0, 0.0);
            }
        }

        float lambertFactor = max(dot(norm, lightDirNorm), 0.0);
        vec3 diffuse = diffuseStrength * lambertFactor * effectiveLightColor * texColor.rgb * objectColor;

        vec3 specular = vec3(0.0);
        if (lightingMode == 3 || lightingMode == 4 || lightingMode == 5) {
            vec3 viewDir = normalize(viewPos - FragPos);
            vec3 reflectDir = reflect(-lightDirNorm, norm);
            float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
            specular = specularStrength * spec * effectiveLightColor;
        }

        float intensity = 1.0;
        if (lightingMode == 3) {
            float theta = dot(lightDirNorm, normalize(-lightDir));
            float epsilon = spotCutOff - spotOuterCutOff;
            intensity = clamp((theta - spotOuterCutOff) / epsilon, 0.0, 1.0);
            diffuse *= intensity;
            specular *= intensity;
        }

        float shadow = 0.0;
        if (lightingMode == 3 || lightingMode == 4 || lightingMode == 5) {
            shadow = ShadowCalculation(FragPosLightSpace, lightDirNorm, norm);
        }

        result = (ambient + (1.0 - shadow) * attenuation * (diffuse + specular)) * texColor.rgb * objectColor;
    }

    FragColor = vec4(result, texColor.a);
}