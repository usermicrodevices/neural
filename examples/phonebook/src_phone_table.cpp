// PhoneEntry data model - represents a single phone book record
struct PhoneEntry {
    int id;
    std::string name;
    std::string phone;
    std::string email;
};

class PhoneBook {
private:
    std::vector<PhoneEntry> entries_;
    int next_id_ = 1;
public:
    void addEntry(const std::string& name, const std::string& phone, const std::string& email) {
        PhoneEntry e{next_id_++, name, phone, email};
        entries_.push_back(e);
    }
    void removeEntry(int id) {
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
            [id](const PhoneEntry& e) { return e.id == id; }), entries_.end());
    }
    PhoneEntry* findById(int id) {
        for (auto& e : entries_) if (e.id == id) return &e;
        return nullptr;
    }
    std::vector<PhoneEntry> findAll() { return entries_; }
};
