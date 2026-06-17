#include <string>
#include <vector>

struct Cell {
    std::string value;
    std::string dataType = "text";
    std::string getDisplay() const { return value; }
    void setValue(const std::string& v) { value = v; }
};

class TableRow {
public:
    int index = 0;
    std::vector<Cell> cells;
    bool selected = false;
    void setIndex(int i) { index = i; }
    Cell getCell(int col) {
        if (col >= 0 && col < (int)cells.size()) return cells[col];
        return Cell{};
    }
    void addCell(const Cell& c) { cells.push_back(c); }
    void removeCell(int col) {
        if (col >= 0 && col < (int)cells.size()) cells.erase(cells.begin() + col);
    }
    void select() { selected = true; }
    void deselect() { selected = false; }
};
