#include <string>
#include <vector>
#include <map>
#include <algorithm>

struct Record {
    int id;
    std::map<std::string, std::string> fields;
    std::string get(const std::string& key) const {
        auto it = fields.find(key);
        return it != fields.end() ? it->second : "";
    }
    void set(const std::string& key, const std::string& value) {
        fields[key] = value;
    }
};

class DataTable {
private:
    std::vector<Record> rows_;
    int next_id_ = 1;
public:
    void addRecord(const std::map<std::string, std::string>& fields) {
        Record r;
        r.id = next_id_++;
        r.fields = fields;
        rows_.push_back(r);
    }
    void removeRecord(int id) {
        rows_.erase(std::remove_if(rows_.begin(), rows_.end(),
            [id](const Record& r) { return r.id == id; }), rows_.end());
    }
    Record* findById(int id) {
        for (auto& r : rows_) if (r.id == id) return &r;
        return nullptr;
    }
    std::vector<Record> findAll() { return rows_; }
    std::vector<Record> findByField(const std::string& key, const std::string& value) {
        std::vector<Record> result;
        for (auto& r : rows_) {
            if (r.get(key).find(value) != std::string::npos) result.push_back(r);
        }
        return result;
    }
};
