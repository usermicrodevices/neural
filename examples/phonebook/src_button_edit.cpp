// ButtonEdit - loads selected phone entry into form for editing
class ButtonEdit {
    PhoneBook& book_;
    PhoneTable& table_;
    Form& form_;
public:
    ButtonEdit(PhoneBook& book, PhoneTable& table, Form& form)
        : book_(book), table_(table), form_(form) {}
    void onClick(int rowId) {
        PhoneEntry* e = book_.findById(rowId);
        if (e) {
            form_.nameValue = e->name;
            form_.phoneValue = e->phone;
            form_.emailValue = e->email;
            table_.selectedRowId = rowId;
        }
    }
};
