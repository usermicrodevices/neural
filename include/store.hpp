#pragma once

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include <sqlite3.h>
#include <nlohmann/json.hpp>

#include "logger.hpp"
#include "neural.hpp"
#include "vocabulary.hpp"

enum UploadResult{
    OK,
    PARTIAL,
    DUPLICATE
};

class DocumentStore {
public:
    DocumentStore(const std::string& db_path);
    ~DocumentStore();
    void initialize_db();
    nlohmann::json get_source_types();
    void add_document(const std::string& text, const std::string& tags, std::function<void(int)> progress_cb, bool serialization=false);
    std::string get_answer(const std::string& prompt, double threshold = CONFIDENCE_THRESHOLD);
    static std::string escape_json(const std::string& s);
    size_t get_memory_usage() const;
    void serialize();
    UploadResult get_last_upload_result() const;
    void add_tag(const std::string& tag, int chunk_id);
    void create_uml_container(const std::string& name, const std::string& uml_schema,
                              const std::vector<std::pair<uint8_t, std::string>>& sources);
    nlohmann::json list_tables();
    nlohmann::json get_table_data(const std::string& table, const std::string& filter, int offset = 0, int limit = 100);

private:
    sqlite3* db;
    std::unique_ptr<Vocabulary> vocab;
    std::unique_ptr<NeuralNetwork> net;
    mutable std::shared_mutex mtx_;
    std::string persistent_path;
    static constexpr int MAX_CHUNK_SIZE = 200000;
    static constexpr int HIDDEN_SIZE = 128;
    static constexpr double CONFIDENCE_THRESHOLD = 0.01;
    UploadResult last_upload_result_ = UploadResult::OK;

    std::vector<std::vector<double>> cached_vectors_;
    std::vector<int> cached_ids_;
    bool vectors_dirty_ = true;

    void load_state();
    void save_state();
    void load_from_persistent();
    std::vector<std::string> split_into_chunks(const std::string& text);
    static std::string clean_text(const std::string& text);
    void rebuild_vector_cache();
    void ensure_vector_cache();
};
