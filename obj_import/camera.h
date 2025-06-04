#ifndef CAMERA_H
#define CAMERA_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>

#include <vector>

enum Camera_Movement {
   FORWARD,
   BACKWARD,
   LEFT,
   RIGHT,
   UP,
   DOWN
};

const float YAW = -90.0f;
const float PITCH = 0.0f;
const float SPEED = 2.5f;
const float SENSITIVITY = 0.1f;
const float ZOOM = 45.0f;

class Camera
{
public:
   glm::vec3 Position;
   glm::vec3 Front;
   glm::vec3 Up;
   glm::vec3 Right;
   glm::vec3 WorldUp;
   float Yaw;
   float Pitch;
   float MovementSpeed;
   float MouseSensitivity;
   float Zoom;
   glm::vec3 Target = glm::vec3(0.0f); // ����� ��������

   Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f), float yaw = YAW, float pitch = PITCH) : Front(glm::vec3(0.0f, 0.0f, -1.0f)), MovementSpeed(SPEED), MouseSensitivity(SENSITIVITY), Zoom(ZOOM)
   {
      Position = position;
      WorldUp = up;
      Yaw = yaw;
      Pitch = pitch;
      updateCameraVectors();
   }

   glm::mat4 GetViewMatrix()
   {
      return glm::lookAt(Position, Position + Front, Up);
   }

   void ProcessKeyboard(Camera_Movement direction, float deltaTime)
   {
      float velocity = MovementSpeed * deltaTime;
      if (direction == FORWARD)
         Position += Front * velocity;
      if (direction == BACKWARD)
         Position -= Front * velocity;
      if (direction == LEFT)
         Position -= Right * velocity;
      if (direction == RIGHT)
         Position += Right * velocity;
      if (direction == UP)
         Position += WorldUp * velocity;
      if (direction == DOWN)
         Position -= WorldUp * velocity;
   }

   void ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch = true)
   {
      xoffset *= MouseSensitivity;
      yoffset *= MouseSensitivity;

      Yaw += xoffset;
      Pitch -= yoffset;

      if (constrainPitch)
      {
         if (Pitch > 89.0f)
            Pitch = 89.0f;
         if (Pitch < -89.0f)
            Pitch = -89.0f;
      }

      updateCameraVectors();
   }

   void ProcessMouseOrbit(float xoffset, float yoffset)
   {
      xoffset *= MouseSensitivity * 0.5f;
      yoffset *= MouseSensitivity * 0.5f;

      glm::vec3 prevPosition = Position;

      // ��������� ������ �� ������ � ����
      glm::vec3 direction = Position - Target;

      // ������ ����������� ��� ��������
      glm::quat rotX = glm::angleAxis(glm::radians(yoffset), Right);
      glm::quat rotY = glm::angleAxis(glm::radians(-xoffset), WorldUp);
      glm::quat rotation = glm::normalize(rotY * rotX);

      // ��������� �������� � ������� �����������
      direction = rotation * direction;

      // ��������� ������� ������
      Position = Target + direction;

      // �������� �� ���������� �������
      if (!std::isfinite(Position.x) || !std::isfinite(Position.y) || !std::isfinite(Position.z)) {
         Position = prevPosition;
         return;
      }

      // ��������� ������� ������
      Front = glm::normalize(Target - Position);
      Right = glm::normalize(glm::cross(Front, WorldUp));
      Up = glm::normalize(glm::cross(Right, Front));
   }

   void ProcessMousePan(float xoffset, float yoffset)
   {
      xoffset *= MouseSensitivity * 0.01f;
      yoffset *= MouseSensitivity * 0.01f;

      glm::vec3 prevPosition = Position;
      Position -= Right * xoffset;
      Position += Up * yoffset;

      // �������� �� ���������� �������
      if (!std::isfinite(Position.x) || !std::isfinite(Position.y) || !std::isfinite(Position.z)) {
         Position = prevPosition;
         return;
      }

      Target = Position + Front;
      updateCameraVectors();
   }

   void ProcessMouseScroll(float yoffset)
   {
      Zoom -= (float)yoffset;
      if (Zoom < 1.0f)
         Zoom = 1.0f;
      if (Zoom > 45.0f)
         Zoom = 45.0f;

      glm::vec3 direction = glm::normalize(Position - Target);
      float distance = glm::length(Position - Target) - yoffset * 0.1f;
      if (distance < 0.1f) distance = 0.1f;
      Position = Target + direction * distance;
   }

   void Reset()
   {
      Position = glm::vec3(0.0f, 0.0f, 10.0f);
      Yaw = YAW;
      Pitch = PITCH;
      Zoom = ZOOM;
      updateCameraVectors();
   }

private:
   void updateCameraVectors()
   {
      glm::vec3 front;
      front.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
      front.y = sin(glm::radians(Pitch));
      front.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
      Front = glm::normalize(front);
      Right = glm::normalize(glm::cross(Front, WorldUp));
      Up = glm::normalize(glm::cross(Right, Front));
   }
};

#endif