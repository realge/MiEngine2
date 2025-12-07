#pragma once
#include <string>


class VulkanRenderer;
class DebugPanel {

protected:
    std::string panelName;
    VulkanRenderer* renderer;
    bool isOpen = true;

public:
    DebugPanel(const std::string& name, VulkanRenderer* renderer) 
        : panelName(name), renderer(renderer) {}
    
    virtual ~DebugPanel() = default;
    
    virtual void draw() = 0;
    
    // Make sure these methods are defined inline or in cpp file
    
    const std::string& getName() const { return panelName; }
    bool getOpen() const { return isOpen; }
    void setOpen(bool open) { isOpen = open; }
    void toggle() { isOpen = !isOpen; }
};

