class ButtonDelete {
    DataTable& table_;
public:
    ButtonDelete(DataTable& table) : table_(table) {}
    void onClick(int rowId) {
        table_.removeRecord(rowId);
        if (table_.selectedRowId == rowId) table_.selectedRowId = -1;
    }
    void confirm() {
        if (table_.selectedRowId >= 0) onClick(table_.selectedRowId);
    }
};
