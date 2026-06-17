// ButtonAdd - adds a new phone entry from form fields
class ButtonAdd {
    PhoneBook& book_;
    Form& form_;
public:
    ButtonAdd(PhoneBook& book, Form& form) : book_(book), form_(form) {}
    void onClick() {
        if (form_.nameValue.empty() || form_.phoneValue.empty()) return;
        book_.addEntry(form_.nameValue, form_.phoneValue, form_.emailValue);
        form_.clear();
    }
};

class Form {
public:
    std::string nameValue;
    std::string phoneValue;
    std::string emailValue;
    void clear() { nameValue.clear(); phoneValue.clear(); emailValue.clear(); }
};
