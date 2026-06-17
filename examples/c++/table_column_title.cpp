#include <string>

class TableColumnTitle {
public:
    std::string text;
    std::string alignment = "left";
    int columnIndex = 0;
    bool sortable = false;
    bool isFilterable = false;
    void setText(const std::string& t) { text = t; }
    void setAlignment(const std::string& a) { alignment = a; }
    std::string render() { return text; }
    void enableSort() { sortable = true; }
    void enableFilter() { isFilterable = true; }
};
