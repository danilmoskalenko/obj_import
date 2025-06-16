#pragma once
#include <vector>

struct Ray {
   glm::vec3 origin;
   glm::vec3 direction;

   Ray(const glm::vec3& o, const glm::vec3& d) : origin(o), direction(glm::normalize(d)) {}
};

struct Triangle {
   glm::vec3 v0, v1, v2;

   Triangle(const glm::vec3& _v0, const glm::vec3& _v1, const glm::vec3& _v2)
      : v0(_v0), v1(_v1), v2(_v2) {
   }
};

// ‘ункци€ проверки пересечени€ луча с треугольником (алгоритм M?llerЦTrumbore)
bool RayTriangleIntersect(const Ray& ray, const Triangle& triangle, float& t) {
   const float EPSILON = 0.0000001f;
   glm::vec3 edge1 = triangle.v1 - triangle.v0;
   glm::vec3 edge2 = triangle.v2 - triangle.v0;
   glm::vec3 h = glm::cross(ray.direction, edge2);
   float a = glm::dot(edge1, h);

   if (a > -EPSILON && a < EPSILON)
      return false;

   float f = 1.0f / a;
   glm::vec3 s = ray.origin - triangle.v0;
   float u = f * glm::dot(s, h);

   if (u < 0.0f || u > 1.0f)
      return false;

   glm::vec3 q = glm::cross(s, edge1);
   float v = f * glm::dot(ray.direction, q);

   if (v < 0.0f || u + v > 1.0f)
      return false;

   t = f * glm::dot(edge2, q);

   return (t > EPSILON);
}

bool RaySphereIntersect(const Ray& ray, const glm::vec3& center, float radius, float& t) {
   glm::vec3 oc = ray.origin - center;
   float a = glm::dot(ray.direction, ray.direction);
   float b = 2.0f * glm::dot(oc, ray.direction);
   float c = glm::dot(oc, oc) - radius * radius;
   float discriminant = b * b - 4 * a * c;

   if (discriminant < 0) return false;

   t = (-b - sqrt(discriminant)) / (2.0f * a);
   return t >= 0;
}

// √енераци€ вершин и индексов сферы (только позиции, без UV/нормалей)
inline void GenerateSphere(float radius, unsigned int sectorCount, unsigned int stackCount,
   std::vector<float>& vertices, std::vector<unsigned int>& indices)
{
   vertices.clear();
   indices.clear();

   const float PI = 3.14159265359f;
   for (unsigned int i = 0; i <= stackCount; ++i) {
      float stackAngle = PI / 2 - i * PI / stackCount; // от +pi/2 до -pi/2
      float xy = radius * cosf(stackAngle);
      float z = radius * sinf(stackAngle);

      for (unsigned int j = 0; j <= sectorCount; ++j) {
         float sectorAngle = j * 2 * PI / sectorCount; // от 0 до 2pi
         float x = xy * cosf(sectorAngle);
         float y = xy * sinf(sectorAngle);
         vertices.push_back(x);
         vertices.push_back(y);
         vertices.push_back(z);
      }
   }

   for (unsigned int i = 0; i < stackCount; ++i) {
      unsigned int k1 = i * (sectorCount + 1);
      unsigned int k2 = k1 + sectorCount + 1;
      for (unsigned int j = 0; j < sectorCount; ++j, ++k1, ++k2) {
         if (i != 0) {
            indices.push_back(k1);
            indices.push_back(k2);
            indices.push_back(k1 + 1);
         }
         if (i != (stackCount - 1)) {
            indices.push_back(k1 + 1);
            indices.push_back(k2);
            indices.push_back(k2 + 1);
         }
      }
   }
}

