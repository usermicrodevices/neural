// SearchField + ButtonSearch - searches phone entries by name, phone, or email
class SearchField {
public:
    std::string query;
    void onInput(const std::string& text) { query = text; }
};

class ButtonSearch {
    PhoneBook& book_;
    SearchField& field_;
    std::vector<PhoneEntry> results_;
public:
    ButtonSearch(PhoneBook& book, SearchField& field) : book_(book), field_(field) {}
    void onClick() {
        results_.clear();
        for (const auto& e : book_.findAll()) {
            if (e.name.find(field_.query) != std::string::npos ||
                e.phone.find(field_.query) != std::string::npos ||
                e.email.find(field_.query) != std::string::npos) {
                results_.push_back(e);
            }
        }
    }
    std::vector<PhoneEntry> getResults() const { return results_; }
};
