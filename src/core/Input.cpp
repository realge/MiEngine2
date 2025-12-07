#include "core/Input.h"

GLFWwindow* Input::s_Window = nullptr;
bool Input::s_Keys[1024] = { false };
float Input::s_ScrollY = 0.0f;
glm::vec2 Input::s_LastMousePos = { 0.0f, 0.0f };
