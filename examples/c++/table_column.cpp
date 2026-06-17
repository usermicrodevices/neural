#include <string>
#include <vector>
#include <algorithm>

class TableColumn {
public:
    int index = 0;
    std::string title;
    std::string dataType = "text";
    int width = 100;
    bool sortable = false;
    bool visible = true;
    void setTitle(const std::string& t) { title = t; }
    void setWidth(int w) { width = w; }
    void sort(std::vector<TableRow>& rows) {
        std::sort(rows.begin(), rows.end(), [this](const TableRow& a, const TableRow& b) {
            return a.getCell(index).value < b.getCell(index).value;
        });
    }
    void hide() { visible = false; }
    void show() { visible = true; }
};
