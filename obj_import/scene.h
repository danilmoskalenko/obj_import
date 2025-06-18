#include <string>
#include "model.h"

// Структуры и перечисления для работы со сценой
struct SceneObject {
   std::string name;
   Model model;
   glm::vec3 position = glm::vec3(0.0f);
   glm::vec3 rotation = glm::vec3(0.0f);
   glm::vec3 scale = glm::vec3(1.0f);
   glm::vec3 mirrorNormal = glm::vec3(0.0f, 1.0f, 0.0f);

   SceneObject() : name(""), model(Model("")), position(0.0f), rotation(0.0f), scale(1.0f), mirrorNormal(0.0f, 1.0f, 0.0f) {}
   SceneObject(const std::string& name, const Model& model) : name(name), model(model) {}

};

// Режимы отображения
enum DisplayMode { NORMAL_MODE, FACE_NORMALS, VERTEX_NORMALS };
enum LightingMode { NONE, NO_LIGHTING, AMBIENT, SPOTLIGHT, DIRECTIONAL, POINT };
enum ShadowMode { SHADOW_NONE, SHADOW_MAPPING, SHADOW_RAYTRACING };
enum ShadowNormalType { SHADOW_VERTEX_NORMALS, SHADOW_FACE_NORMALS };
