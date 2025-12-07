// Camera.cpp - Fixed implementation
#include "camera/Camera.h"
#include <algorithm>

// Default camera values
const float YAW         = -90.0f;
const float PITCH       =  0.0f;
const float SPEED       =  5.0f;
const float SENSITIVITY =  0.1f;
const float FOV         =  45.0f;

Camera::Camera(glm::vec3 position, glm::vec3 up, float yaw, float pitch) 
    : position(position), worldUp(up), yaw(yaw), pitch(pitch),
      movementSpeed(SPEED), mouseSensitivity(SENSITIVITY), fov(FOV) {
    updateCameraVectors();
}

Camera::Camera(float posX, float posY, float posZ, float upX, float upY, float upZ, float yaw, float pitch)
    : position(glm::vec3(posX, posY, posZ)), worldUp(glm::vec3(upX, upY, upZ)), 
      yaw(yaw), pitch(pitch), movementSpeed(SPEED), mouseSensitivity(SENSITIVITY), fov(FOV) {
    updateCameraVectors();
}

glm::mat4 Camera::getViewMatrix() const {
    // FPS camera: look from position towards position + front
    return glm::lookAt(position, position + front, up);
}

glm::mat4 Camera::getProjectionMatrix(float aspectRatio, float nearPlane, float farPlane) const {
    return glm::perspective(glm::radians(fov), aspectRatio, nearPlane, farPlane);
}

void Camera::processKeyboard(CameraMovement direction, float deltaTime, float speedMultiplier) {
    float velocity = movementSpeed * speedMultiplier * deltaTime;
    
    if (direction == CameraMovement::FORWARD)
        position += front * velocity;
    if (direction == CameraMovement::BACKWARD)
        position -= front * velocity;
    if (direction == CameraMovement::LEFT)
        position -= right * velocity;
    if (direction == CameraMovement::RIGHT)
        position += right * velocity;
    if (direction == CameraMovement::UP)
        position += worldUp * velocity;
    if (direction == CameraMovement::DOWN)
        position -= worldUp * velocity;
}

void Camera::processMouseMovement(float xoffset, float yoffset, bool constrainPitch) {
    xoffset *= mouseSensitivity;
    yoffset *= mouseSensitivity;
    
    yaw += xoffset;
    pitch += yoffset;
    
    // Constrain pitch to avoid screen flip
    if (constrainPitch) {
        pitch = glm::clamp(pitch, -89.0f, 89.0f);
    }
    
    // Update front, right and up vectors
    updateCameraVectors();
}

void Camera::processMouseScroll(float yoffset) {
    fov -= yoffset;
    fov = glm::clamp(fov, 1.0f, 120.0f);
}

void Camera::setPosition(const glm::vec3& pos) {
    position = pos;
}

void Camera::lookAt(const glm::vec3& target) {
    glm::vec3 direction = glm::normalize(target - position);
    
    pitch = glm::degrees(asin(direction.y));
    yaw = glm::degrees(atan2(direction.z, direction.x));
    
    updateCameraVectors();
}

void Camera::setFOV(float fieldOfView) {
    fov = glm::clamp(fieldOfView, 1.0f, 120.0f);
}

float Camera::getYaw() const {
    return yaw;
}

float Camera::getPitch() const {
    return pitch;
}

void Camera::updateCameraVectors() {
    // Calculate the new front vector from euler angles
    glm::vec3 newFront;
    newFront.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    newFront.y = sin(glm::radians(pitch));
    newFront.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    front = glm::normalize(newFront);
    
    // Recalculate right and up vectors
    right = glm::normalize(glm::cross(front, worldUp));
    up = glm::normalize(glm::cross(right, front));
}