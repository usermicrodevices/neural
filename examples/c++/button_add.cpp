#include <string>
#include <map>

class Form {
public:
    std::map<std::string, std::string> fields;
    void clear() { fields.clear(); }
    std::string getField(const std::string& name) const {
        auto it = fields.find(name);
        return it != fields.end() ? it->second : "";
    }
    void setField(const std::string& name, const std::string& value) {
        fields[name] = value;
    }
};

class ButtonAdd {
    DataTable& table_;
    Form& form_;
public:
    ButtonAdd(DataTable& table, Form& form) : table_(table), form_(form) {}
    void onClick() {
        if (form_.getField("name").empty()) return;
        table_.addRecord(form_.fields);
        form_.clear();
    }
};
