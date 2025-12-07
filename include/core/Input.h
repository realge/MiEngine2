#pragma once
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <unordered_map>

class Input {
public:
    static void Initialize(GLFWwindow* window) {
        s_Window = window;
        glfwSetKeyCallback(window, KeyCallback);
        glfwSetCursorPosCallback(window, MouseCallback);
        glfwSetMouseButtonCallback(window, MouseButtonCallback);
        glfwSetScrollCallback(window, ScrollCallback);
    }

    static bool IsKeyPressed(int key) {
        return s_Keys[key];
    }

    static bool IsMouseButtonPressed(int button) {
        return glfwGetMouseButton(s_Window, button) == GLFW_PRESS;
    }

    static glm::vec2 GetMousePosition() {
        double x, y;
        glfwGetCursorPos(s_Window, &x, &y);
        return { (float)x, (float)y };
    }

    static float GetMouseScroll() {
        float scroll = s_ScrollY;
        s_ScrollY = 0.0f; // Reset after reading
        return scroll;
    }

    static glm::vec2 GetMouseDelta() {
        glm::vec2 currentPos = GetMousePosition();
        glm::vec2 delta = currentPos - s_LastMousePos;
        s_LastMousePos = currentPos;
        return delta;
    }
    
    static void ResetMouseDelta() {
        s_LastMousePos = GetMousePosition();
    }

private:
    static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
        if (key >= 0 && key < 1024) {
            if (action == GLFW_PRESS) s_Keys[key] = true;
            else if (action == GLFW_RELEASE) s_Keys[key] = false;
        }
    }

    static void MouseCallback(GLFWwindow* window, double xpos, double ypos) {
        // Handled by GetMouseDelta
    }

    static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
        // Can add specific handling if needed
    }

    static void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
        s_ScrollY = (float)yoffset;
    }

    static GLFWwindow* s_Window;
    static bool s_Keys[1024];
    static float s_ScrollY;
    static glm::vec2 s_LastMousePos;
};
