#include <string>

class TableRowTitle {
public:
    std::string text;
    std::string alignment = "left";
    int rowIndex = 0;
    bool isHeader = false;
    void setText(const std::string& t) { text = t; }
    void setAlignment(const std::string& a) { alignment = a; }
    std::string render() { return text; }
    void markAsHeader() { isHeader = true; }
};
