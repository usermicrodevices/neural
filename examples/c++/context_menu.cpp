#include <string>
#include <vector>
#include <functional>

struct MenuItem {
    std::string label;
    std::string icon;
    bool enabled = true;
    bool separator = false;
    std::function<void()> onClick;
    void setEnabled(bool e) { enabled = e; }
    std::string render() { return (icon.empty() ? "" : icon + " ") + label; }
};

class ContextMenu {
public:
    int x = 0, y = 0;
    bool visible = false;
    std::vector<MenuItem> items;
    void show(int px, int py) { x = px; y = py; visible = true; }
    void hide() { visible = false; }
    void addItem(const MenuItem& item) { items.push_back(item); }
    void removeItem(const std::string& label) {
        items.erase(std::remove_if(items.begin(), items.end(),
            [&](const MenuItem& i) { return i.label == label; }), items.end());
    }
    MenuItem* hitTest(int mx, int my) {
        if (!visible) return nullptr;
        int cy = y;
        for (auto& item : items) {
            if (item.separator) { cy += 4; continue; }
            if (mx >= x && mx <= x + 160 && my >= cy && my <= cy + 24) {
                return item.enabled ? &item : nullptr;
            }
            cy += 24;
        }
        return nullptr;
    }
};
