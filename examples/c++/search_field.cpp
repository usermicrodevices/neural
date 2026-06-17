#include <string>
#include <vector>

class SearchField {
public:
    std::string query;
    void onInput(const std::string& text) { query = text; }
};

class ButtonSearch {
    DataTable& table_;
    SearchField& field_;
    std::vector<Record> results_;
public:
    ButtonSearch(DataTable& table, SearchField& field) : table_(table), field_(field) {}
    void onClick() {
        results_.clear();
        for (const auto& r : table_.findAll()) {
            for (const auto& [k, v] : r.fields) {
                if (v.find(field_.query) != std::string::npos) {
                    results_.push_back(r);
                    break;
                }
            }
        }
    }
    std::vector<Record> getResults() const { return results_; }
};
