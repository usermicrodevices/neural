// ButtonDelete - removes a phone entry by ID
class ButtonDelete {
    PhoneBook& book_;
    PhoneTable& table_;
public:
    ButtonDelete(PhoneBook& book, PhoneTable& table) : book_(book), table_(table) {}
    void onClick(int rowId) {
        book_.removeEntry(rowId);
        if (table_.selectedRowId == rowId) table_.selectedRowId = -1;
    }
    void confirm() {
        if (table_.selectedRowId >= 0) onClick(table_.selectedRowId);
    }
};
