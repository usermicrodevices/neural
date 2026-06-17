#include <string>

class ButtonEdit {
    DataTable& table_;
    Form& form_;
public:
    ButtonEdit(DataTable& table, Form& form) : table_(table), form_(form) {}
    void onClick(int rowId) {
        Record* r = table_.findById(rowId);
        if (r) {
            form_.fields = r->fields;
            table_.selectedRowId = rowId;
        }
    }
};
