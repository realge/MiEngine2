#pragma once
#include <string>

class Scene;
class Camera;
class VulkanRenderer;

namespace MiEngine {
    class MiWorld;
}

class Game {
public:
    virtual ~Game() = default;

    virtual void OnInit() = 0;
    virtual void OnUpdate(float deltaTime) = 0;
    virtual void OnRender() = 0;
    virtual void OnShutdown() = 0;

    // Helper to access the scene (will be set by Application)
    void SetScene(Scene* scene) { m_Scene = scene; }
    void SetCamera(Camera* camera) { m_Camera = camera; }
    void SetWorld(MiEngine::MiWorld* world) { m_World = world; }
    void SetRenderer(VulkanRenderer* renderer) { m_Renderer = renderer; }

    virtual bool UsesDefaultCameraInput() const { return true; }
    virtual bool UsesDefaultCameraMovement() const { return UsesDefaultCameraInput(); }

protected:
    Scene* m_Scene = nullptr;
    Camera* m_Camera = nullptr;
    MiEngine::MiWorld* m_World = nullptr;
    VulkanRenderer* m_Renderer = nullptr;
};
