#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

enum class CameraMovement {
    FORWARD,
    BACKWARD,
    LEFT,
    RIGHT,
    UP,
    DOWN
};

class Camera {
public:
    // Camera attributes
    glm::vec3 position;
    glm::vec3 front;
    glm::vec3 up;
    glm::vec3 right;
    glm::vec3 worldUp;
    
    // Euler angles
    float yaw;
    float pitch;
    
    // Camera options
    float movementSpeed;
    float mouseSensitivity;
    float fov;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;

    float getMovementSpeed() const { return movementSpeed; }
    float getMouseSensitivity() const { return mouseSensitivity; }
    
    // Constructor with vectors
    Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 3.0f),
           glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f),
           float yaw = -90.0f,
           float pitch = 0.0f);
    
    // Constructor with scalar values
    Camera(float posX, float posY, float posZ,
           float upX, float upY, float upZ,
           float yaw, float pitch);
    
    // Returns the view matrix calculated using Euler angles and the LookAt matrix
    glm::mat4 getViewMatrix() const;
    
    // Returns the projection matrix
    glm::mat4 getProjectionMatrix(float aspectRatio, float nearPlane = 0.1f, float farPlane = 100.0f) const;
    
    // Processes input received from keyboard
    void processKeyboard(CameraMovement direction, float deltaTime, float speedMultiplier = 1.0f);
    
    // Processes input received from mouse movement
    void processMouseMovement(float xoffset, float yoffset, bool constrainPitch = true);
    
    // Processes input received from mouse scroll
    void processMouseScroll(float yoffset);
    
    // Get camera position
    glm::vec3 getPosition() const { return position; }
    
    // Get camera front vector
    glm::vec3 getFront() const { return front; }
    
    // Get camera up vector
    glm::vec3 getUp() const { return up; }
    
    // Get camera right vector
    glm::vec3 getRight() const { return right; }
    
    // Get field of view
    float getFOV() const { return fov; }
    
    // Set camera position
    void setPosition(const glm::vec3& pos);
    
    // Set camera target (look at point)
    void lookAt(const glm::vec3& target);
    void setFOV(float fieldOfView);
    float getYaw() const;
    float getPitch() const;

    void setNearPlane(float near) { nearPlane = near; }
    void setFarPlane(float far) { farPlane = far; }
    float getNearPlane() const { return nearPlane; }
    float getFarPlane() const { return farPlane; }

    // Set movement speed
    void setMovementSpeed(float speed) { movementSpeed = speed; }
    
    // Set mouse sensitivity
    void setMouseSensitivity(float sensitivity) { mouseSensitivity = sensitivity; }

private:
    // Calculates the front vector from the camera's (updated) Euler angles
    void updateCameraVectors();
    
};