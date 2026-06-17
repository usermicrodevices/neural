#include "store.hpp"

DocumentStore::DocumentStore(const std::string& db_path)
: db(nullptr), persistent_path(db_path) {
    std::string conn_str = "file:" + persistent_path + "?mode=memory&cache=shared";
    if (sqlite3_open(conn_str.c_str(), &db) != SQLITE_OK) {
        throw std::runtime_error("Failed to open in-memory database");
    }
    vocab = std::make_unique<Vocabulary>();
    initialize_db();
    load_from_persistent();
    initialize_db();
    load_state();
}

DocumentStore::~DocumentStore() { if (db) sqlite3_close(db); }

UploadResult DocumentStore::get_last_upload_result() const { return last_upload_result_; }

void DocumentStore::initialize_db() {
    const char* sql =
    "CREATE TABLE IF NOT EXISTS chunks (id INTEGER PRIMARY KEY, text TEXT UNIQUE) WITHOUT ROWID;"
    "CREATE TABLE IF NOT EXISTS vocab (word TEXT PRIMARY KEY, idx INTEGER, df INTEGER) WITHOUT ROWID;"
    "CREATE TABLE IF NOT EXISTS model (layer INTEGER, weights BLOB, biases BLOB);"
    "CREATE TABLE IF NOT EXISTS chunk_tags (chunk_id INTEGER, tag TEXT, PRIMARY KEY (chunk_id, tag)) WITHOUT ROWID;"
    "CREATE TABLE IF NOT EXISTS src_type (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT UNIQUE);"
    "CREATE TABLE IF NOT EXISTS uml (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, schema BLOB, UNIQUE(name, schema));"
    "CREATE TABLE IF NOT EXISTS uml_src (id INTEGER PRIMARY KEY AUTOINCREMENT, uml_id INTEGER, src_type_id INTEGER, source BLOB, UNIQUE(uml_id, src_type_id, source));"
    "CREATE TABLE IF NOT EXISTS uml_embeddings (uml_name TEXT PRIMARY KEY, embedding BLOB);";
    char* errmsg = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &errmsg) != SQLITE_OK) {
        std::string error = errmsg;
        sqlite3_free(errmsg);
        throw std::runtime_error("Failed to create tables: " + error);
    }
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "INSERT OR IGNORE INTO src_type(name) VALUES('C++'),('Python')", -1, &stmt, nullptr);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void DocumentStore::load_from_persistent() {
    sqlite3* file_db = nullptr;
    if (sqlite3_open(persistent_path.c_str(), &file_db) != SQLITE_OK) {
        Logger::Warn("No persistent DB found, starting fresh");
        return;
    }
    sqlite3_backup* backup = sqlite3_backup_init(db, "main", file_db, "main");
    if (backup) {
        sqlite3_backup_step(backup, -1);
        sqlite3_backup_finish(backup);
    } else {
        Logger::Warn("Could not backup persistent DB into memory");
    }
    sqlite3_close(file_db);
}

std::string DocumentStore::escape_json(const std::string& s) {
    std::string o;
    o.reserve(s.size() + s.size() / 4);
    for (char c : s) {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else o += c;
    }
    return o;
}

void DocumentStore::load_state() {
    sqlite3_stmt* stmt;
    vocab->clear();
    sqlite3_prepare_v2(db, "SELECT word, idx FROM vocab ORDER BY idx", -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string w(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        int idx = sqlite3_column_int(stmt, 1);
        if (static_cast<int>(vocab->words_size()) <= idx) {
            vocab->words_resize(idx + 1);
            vocab->df_resize(idx + 1, 0);
        }
        vocab->set_word(idx, w);
        vocab->set_word2idx(w, idx);
    }
    sqlite3_finalize(stmt);
    sqlite3_prepare_v2(db, "SELECT idx, df FROM vocab", -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int idx = sqlite3_column_int(stmt, 0);
        int freq = sqlite3_column_int(stmt, 1);
        if (idx < static_cast<int>(vocab->df_size()))
            vocab->set_df(idx, freq);
    }
    sqlite3_finalize(stmt);
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM chunks", -1, &stmt, nullptr);
    if (sqlite3_step(stmt) == SQLITE_ROW)
        vocab->set_doc_count(sqlite3_column_int(stmt, 0));
    sqlite3_finalize(stmt);
    sqlite3_prepare_v2(db, "SELECT layer, weights, biases FROM model ORDER BY layer", -1, &stmt, nullptr);
    int in = 0, hn = 0, out = 0;
    std::vector<double> w1, b1, w2, b2;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int layer = sqlite3_column_int(stmt, 0);
        const void* wblob = sqlite3_column_blob(stmt, 1);
        int wsize = sqlite3_column_bytes(stmt, 1);
        const void* bblob = sqlite3_column_blob(stmt, 2);
        int bsize = sqlite3_column_bytes(stmt, 2);
        std::vector<double> weights(wsize / sizeof(double));
        std::vector<double> biases(bsize / sizeof(double));
        if (wblob) std::memcpy(weights.data(), wblob, wsize);
        if (bblob) std::memcpy(biases.data(), bblob, bsize);
        if (layer == 1) {
            w1 = std::move(weights);
            b1 = std::move(biases);
            hn = static_cast<int>(b1.size());
            in = hn > 0 ? (w1.size() / hn) : 0;
        } else if (layer == 2) {
            w2 = std::move(weights);
            b2 = std::move(biases);
            out = static_cast<int>(b2.size());
            if (hn == 0) hn = out > 0 ? (w2.size() / out) : 0;
        }
    }
    sqlite3_finalize(stmt);
    if (!w1.empty() && !b1.empty() && !w2.empty() && !b2.empty() && in > 0 && hn > 0 && out > 0) {
        net = std::make_unique<NeuralNetwork>(in, hn, out);
        net->GetW1() = std::move(w1);
        net->GetB1() = std::move(b1);
        net->GetW2() = std::move(w2);
        net->GetB2() = std::move(b2);
    } else {
        int out_size = 1;
        sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM chunks", -1, &stmt, nullptr);
        if (sqlite3_step(stmt) == SQLITE_ROW) out_size = std::max(1, sqlite3_column_int(stmt, 0));
        sqlite3_finalize(stmt);
        net = std::make_unique<NeuralNetwork>(std::max(1, vocab->size()), HIDDEN_SIZE, out_size);
        net->init_random();
    }
    if (net && vocab->size() != net->input_size()) {
        if (vocab->size() > net->input_size()) {
            net->expand_inputs(vocab->size());
        } else {
            Logger::Warn("Vocabulary shrunk ({} < {}), keeping network.", vocab->size(), net->input_size());
        }
    }
    vectors_dirty_ = true;
}

void DocumentStore::save_state() {
    sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "DELETE FROM vocab; DELETE FROM model;", nullptr, nullptr, nullptr);
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "INSERT INTO vocab(word, idx, df) VALUES(?,?,?)", -1, &stmt, nullptr);
    int words_count = static_cast<int>(vocab->words_size());
    int df_count = static_cast<int>(vocab->df_size());
    for (int i = 0; i < words_count; ++i) {
        sqlite3_bind_text(stmt, 1, vocab->word(i).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, i);
        sqlite3_bind_int(stmt, 3, i < df_count ? vocab->get_df(i) : 0);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);
    auto save_layer = [&](int layer, const std::vector<double>& w, const std::vector<double>& b) {
        sqlite3_prepare_v2(db, "INSERT INTO model(layer, weights, biases) VALUES(?,?,?)", -1, &stmt, nullptr);
        sqlite3_bind_int(stmt, 1, layer);
        sqlite3_bind_blob(stmt, 2, w.data(), w.size() * sizeof(double), SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 3, b.data(), b.size() * sizeof(double), SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    };
    save_layer(1, net->GetW1(), net->GetB1());
    save_layer(2, net->GetW2(), net->GetB2());
    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
}

void DocumentStore::rebuild_vector_cache() {
    cached_vectors_.clear();
    cached_ids_.clear();
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT id, text FROM chunks ORDER BY id", -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        std::string raw_txt(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        std::string clean_txt = clean_text(raw_txt);
        auto vec = vocab->vectorize(clean_txt);
        sqlite3_stmt* tag_stmt = nullptr;
        sqlite3_prepare_v2(db, "SELECT tag FROM chunk_tags WHERE chunk_id=?", -1, &tag_stmt, nullptr);
        sqlite3_bind_int(tag_stmt, 1, id);
        while (sqlite3_step(tag_stmt) == SQLITE_ROW) {
            std::string tag_str(reinterpret_cast<const char*>(sqlite3_column_text(tag_stmt, 0)));
            auto tag_vec = vocab->vectorize(tag_str);
            for (size_t i = 0; i < vec.size(); ++i) vec[i] += tag_vec[i];
        }
        sqlite3_finalize(tag_stmt);
        cached_vectors_.push_back(std::move(vec));
        cached_ids_.push_back(id);
    }
    sqlite3_finalize(stmt);
    vectors_dirty_ = false;
}

void DocumentStore::ensure_vector_cache() {
    if (vectors_dirty_) rebuild_vector_cache();
}

void DocumentStore::add_tag(const std::string& tag, int chunk_id) {
    std::unique_lock lock(mtx_);
    std::string cleaned_tag = clean_text(tag);
    sqlite3_stmt* insert_tag = nullptr;
    sqlite3_prepare_v2(db, "INSERT OR IGNORE INTO chunk_tags(chunk_id, tag) VALUES(?,?)", -1, &insert_tag, nullptr);
    sqlite3_bind_int(insert_tag, 1, chunk_id);
    sqlite3_bind_text(insert_tag, 2, cleaned_tag.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(insert_tag);
    sqlite3_finalize(insert_tag);
    vectors_dirty_ = true;
    ensure_vector_cache();
    int total_chunks = static_cast<int>(cached_vectors_.size());
    if (total_chunks > net->output_size()) {
        net->expand_outputs(total_chunks);
    }
    std::vector<int> Y;
    Y.reserve(total_chunks);
    for (int id : cached_ids_) Y.push_back(id - 1);
    const int epochs = 20;
    for (int e = 0; e < epochs; ++e) {
        net->train_batch(cached_vectors_, Y, 0.01);
    }
    save_state();
}

void DocumentStore::add_document(const std::string& text, const std::string& tags,
                                 std::function<void(int)> progress_cb, bool serialization) {
    std::unique_lock lock(mtx_);
    Logger::Trace("DocumentStore::add_document adding document ({} bytes)", text.size());
    auto raw_chunks = split_into_chunks(text);
    Logger::Trace("DocumentStore::add_document split into {} chunks", raw_chunks.size());
    std::vector<std::string> tag_phrases;
    {
        const char* p = tags.c_str();
        const char* end = p + tags.size();
        while (p < end) {
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
            if (p >= end) break;
            const char* start = p;
            while (p < end && *p != ';') ++p;
            std::string tp(start, p - start);
            while (!tp.empty() && (tp.back() == ' ' || tp.back() == '\t' || tp.back() == '\n' || tp.back() == '\r'))
                tp.pop_back();
            if (!tp.empty()) {
                std::replace(tp.begin(), tp.end(), ' ', '_');
                tag_phrases.push_back(std::move(tp));
            }
            if (p < end) ++p;
        }
    }

    std::unordered_set<std::string> existing_chunks;
    {
        sqlite3_stmt* check_stmt = nullptr;
        sqlite3_prepare_v2(db, "SELECT text FROM chunks", -1, &check_stmt, nullptr);
        while (sqlite3_step(check_stmt) == SQLITE_ROW) {
            existing_chunks.insert(reinterpret_cast<const char*>(sqlite3_column_text(check_stmt, 0)));
        }
        sqlite3_finalize(check_stmt);
    }

    std::vector<std::string> unique_chunks;
    uint duplicate_count = 0;
    for (const auto& raw_ch : raw_chunks) {
        if (existing_chunks.count(raw_ch)) {
            Logger::Warn("DocumentStore::add_document duplicate chunk skipped: {}", raw_ch.substr(0, 10));
            duplicate_count++;
        } else {
            unique_chunks.push_back(raw_ch);
        }
    }

    if (unique_chunks.empty()) {
        last_upload_result_ = UploadResult::DUPLICATE;
        Logger::Warn("DocumentStore::add_document document is full duplicate.");
        return;
    } else if (duplicate_count > 0) {
        last_upload_result_ = UploadResult::PARTIAL;
    } else {
        last_upload_result_ = UploadResult::OK;
    }

    for (const auto& tp : tag_phrases) {
        vocab->add_words(tp);
    }
    int old_vocab_size = vocab->size();
    for (const auto& raw_ch : unique_chunks) {
        std::string cleaned = clean_text(raw_ch);
        vocab->add_words(cleaned);
    }
    int new_vocab_size = vocab->size();
    Logger::Trace("DocumentStore::add_document vocabulary size: {} -> {}", old_vocab_size, new_vocab_size);

    int next_id = 0;
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT COALESCE(MAX(id),0)+1 FROM chunks", -1, &stmt, nullptr);
    if (sqlite3_step(stmt) == SQLITE_ROW) next_id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
    sqlite3_stmt* insert_chunk = nullptr;
    sqlite3_prepare_v2(db, "INSERT INTO chunks(id, text) VALUES(?,?)", -1, &insert_chunk, nullptr);
    sqlite3_stmt* insert_tag = nullptr;
    sqlite3_prepare_v2(db, "INSERT OR IGNORE INTO chunk_tags(chunk_id, tag) VALUES(?,?)", -1, &insert_tag, nullptr);
    for (const auto& raw_ch : unique_chunks) {
        sqlite3_bind_int(insert_chunk, 1, next_id);
        sqlite3_bind_text(insert_chunk, 2, raw_ch.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(insert_chunk) != SQLITE_DONE) {
            Logger::Error("DocumentStore::add_document insert failed: {}", sqlite3_errmsg(db));
        }
        sqlite3_reset(insert_chunk);
        for (const auto& tp : tag_phrases) {
            sqlite3_bind_int(insert_tag, 1, next_id);
            sqlite3_bind_text(insert_tag, 2, tp.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(insert_tag);
            sqlite3_reset(insert_tag);
        }
        std::string cleaned = clean_text(raw_ch);
        vocab->add_document(cleaned, next_id);
        next_id++;
    }
    sqlite3_finalize(insert_chunk);
    sqlite3_finalize(insert_tag);
    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);

    int total_chunks = 0;
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM chunks", -1, &stmt, nullptr);
    if (sqlite3_step(stmt) == SQLITE_ROW) total_chunks = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    if (new_vocab_size > old_vocab_size) {
        if (new_vocab_size > net->input_size()) {
            net->expand_inputs(new_vocab_size);
        }
    }
    if (total_chunks > net->output_size()) {
        net->expand_outputs(total_chunks);
    }

    vectors_dirty_ = true;
    ensure_vector_cache();

    int new_count = static_cast<int>(cached_vectors_.size());
    std::vector<int> Y;
    Y.reserve(new_count);
    for (int id : cached_ids_) Y.push_back(id - 1);

    const int epochs = 100;
    for (int e = 0; e < epochs; ++e) {
        net->train_batch(cached_vectors_, Y, 0.01);
        if (progress_cb) progress_cb(static_cast<int>((e+1)*100.0/epochs));
    }
    save_state();
    if (serialization) serialize();
    else Logger::Warn("DocumentStore::add_document stay in‑memory only.");
}

std::string DocumentStore::get_answer(const std::string& prompt, double threshold) {
    std::shared_lock lock(mtx_);
    std::string cleaned = clean_text(prompt);
    auto tokens = Vocabulary::tokenize(cleaned);
    std::set<std::string> candidate_tags;
    for (const auto& w : tokens) candidate_tags.insert(w);
    for (size_t i = 0; i + 1 < tokens.size(); ++i) candidate_tags.insert(tokens[i] + "_" + tokens[i+1]);
    if (!candidate_tags.empty()) {
        std::string tag_placeholders;
        for (size_t i = 0; i < candidate_tags.size(); ++i) {
            if (i > 0) tag_placeholders += ",";
            tag_placeholders += "?";
        }
        std::string tag_query = "SELECT DISTINCT chunk_id FROM chunk_tags WHERE tag IN (" + tag_placeholders + ")";
        sqlite3_stmt* tag_stmt = nullptr;
        if (sqlite3_prepare_v2(db, tag_query.c_str(), -1, &tag_stmt, nullptr) != SQLITE_OK) {
            Logger::Error("Tag query prepare failed: {}", sqlite3_errmsg(db));
        } else {
            int idx = 1;
            for (const auto& t : candidate_tags) {
                sqlite3_bind_text(tag_stmt, idx++, t.c_str(), -1, SQLITE_TRANSIENT);
            }
            std::vector<int> candidate_chunks;
            while (sqlite3_step(tag_stmt) == SQLITE_ROW) {
                candidate_chunks.push_back(sqlite3_column_int(tag_stmt, 0));
            }
            sqlite3_finalize(tag_stmt);
            if (!candidate_chunks.empty()) {
                int best_id = candidate_chunks[0];
                sqlite3_stmt* stmt;
                sqlite3_prepare_v2(db, "SELECT text FROM chunks WHERE id=?", -1, &stmt, nullptr);
                sqlite3_bind_int(stmt, 1, best_id);
                std::string result;
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    std::string txt(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
                    result = "{\"chunk_id\":" + std::to_string(best_id) + ",\"answer\":\"" + escape_json(txt) + "\"}";
                } else {
                    result = R"({"chunk_id":-1,"answer":"Tag matched but chunk not found"})";
                }
                sqlite3_finalize(stmt);
                return result;
            }
        }
    }
    auto vec = vocab->vectorize(prompt);
    if ((int)vec.size() != net->input_size()) {
        Logger::Error("DocumentStore::get_answer vocabulary size ({}) != network input size ({}).", vec.size(), net->input_size());
        return R"({"answer":"Model mismatch. Please re-upload documents.","chunk_id":-1})";
    }
    auto [idx, conf] = net->predict(vec);
    if (conf < threshold) {
        std::ostringstream msg;
        msg << "I don't have enough confidence information yet. (confidence: " << conf << "). Please ask again later or lower the threshold.";
        return "{\"answer\":\"" + escape_json(msg.str()) + "\",\"chunk_id\":-1}";
    }
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT text FROM chunks WHERE id=?", -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, idx + 1);
    std::string result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string txt(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        result = "{\"chunk_id\":" + std::to_string(idx+1) + ",\"answer\":\"" + escape_json(txt) + "\"}";
    } else {
        result = R"({"chunk_id":-1,"answer":"No chunk found"})";
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<std::string> DocumentStore::split_into_chunks(const std::string& text) {
    std::vector<std::string> chunks;
    const char* p = text.c_str();
    const char* end = p + text.size();
    std::string current;
    current.reserve(MAX_CHUNK_SIZE);
    while (p < end) {
        const char* line_start = p;
        while (p < end && *p != '\n') ++p;
        size_t line_len = p - line_start;
        if (!current.empty() && current.size() + line_len + 1 > MAX_CHUNK_SIZE) {
            chunks.push_back(std::move(current));
            current.clear();
            current.reserve(MAX_CHUNK_SIZE);
        }
        if (!current.empty()) current += '\n';
        current.append(line_start, line_len);
        if (p < end) ++p;
    }
    if (!current.empty()) chunks.push_back(std::move(current));
    if (chunks.empty() && !text.empty())
        chunks.push_back(text);
    return chunks;
}

std::string DocumentStore::clean_text(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '.' || c == ' ' || c == '_')
            out += static_cast<char>(c);
        else
            out += ' ';
    }
    return out;
}

void DocumentStore::serialize() {
    sqlite3* file_db = nullptr;
    int err_code = sqlite3_open(persistent_path.c_str(), &file_db);
    if (err_code != SQLITE_OK){
        Logger::Error("DocumentStore::serialize: failed to open persistent DB for writing; error code {}", err_code);
        return;
    }
    sqlite3_backup* backup = sqlite3_backup_init(file_db, "main", db, "main");
    if (backup) {
        sqlite3_backup_step(backup, -1);
        err_code = sqlite3_backup_finish(backup);
        if (err_code != SQLITE_OK)
            Logger::Error("DocumentStore::serialize: failed sqlite3_backup_finish file {}; error code {}", persistent_path, err_code);
    }
    else
        Logger::Error("DocumentStore::serialize sqlite3_backup_init");
    err_code = sqlite3_close(file_db);
    if (err_code != SQLITE_OK)
        Logger::Error("DocumentStore::serialize: failed sqlite3_close file {}; error code {}", persistent_path, err_code);
}

size_t DocumentStore::get_memory_usage() const {
    sqlite3_int64 total = sqlite3_memory_used();
    total += vocab->words_size() * (sizeof(std::string) + sizeof(int));
    for (size_t i = 0; i < vocab->words_size(); ++i)
        total += vocab->word(i).capacity();
    total += net->GetW1().capacity() * sizeof(double);
    total += net->GetB1().capacity() * sizeof(double);
    total += net->GetW2().capacity() * sizeof(double);
    total += net->GetB2().capacity() * sizeof(double);
    return total;
}

nlohmann::json DocumentStore::get_source_types() {
    std::shared_lock lock(mtx_);
    nlohmann::json result = nlohmann::json::array();
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT id, name FROM src_type ORDER BY id", -1, &stmt, nullptr) != SQLITE_OK) {
        Logger::Error("DocumentStore::get_source_types: prepare failed: {}", sqlite3_errmsg(db));
        return result;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        nlohmann::json entry;
        entry["id"] = sqlite3_column_int(stmt, 0);
        entry["name"] = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        result.push_back(entry);
    }
    sqlite3_finalize(stmt);
    return result;
}

bool DocumentStore::create_uml_container(const std::string& name,
                                          const std::string& uml_schema,
                                          const std::vector<std::pair<uint8_t, std::string>>& sources) {
    std::unique_lock lock(mtx_);
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "INSERT INTO uml(name, schema) VALUES(?,?)", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 2, uml_schema.c_str(), uml_schema.size(), SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        Logger::Error("DocumentStore::create_uml_container: insert into uml failed: {}", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_finalize(stmt);
    int64_t uml_id = sqlite3_last_insert_rowid(db);
    sqlite3_prepare_v2(db, "INSERT INTO uml_src(uml_id, src_type_id, source) VALUES(?,?,?)", -1, &stmt, nullptr);
    for (const auto& p : sources) {
        sqlite3_bind_int64(stmt, 1, uml_id);
        sqlite3_bind_int(stmt, 2, p.first);
        sqlite3_bind_blob(stmt, 3, p.second.c_str(), p.second.size(), SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            Logger::Error("DocumentStore::create_uml_container: insert into uml_src failed: {}", sqlite3_errmsg(db));
        }
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);
    serialize();
    return true;
}

void DocumentStore::store_uml_embedding(const std::string& uml_name, const std::vector<double>& embedding) {
    std::vector<float> floats(embedding.begin(), embedding.end());
    std::string blob(reinterpret_cast<const char*>(floats.data()), floats.size() * sizeof(float));
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO uml_embeddings(uml_name, embedding) VALUES (?, ?)", -1, &stmt, nullptr) != SQLITE_OK) {
        Logger::Error("Failed to prepare store embedding: {}", sqlite3_errmsg(db));
        return;
    }
    sqlite3_bind_text(stmt, 1, uml_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 2, blob.data(), static_cast<int>(blob.size()), SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

nlohmann::json DocumentStore::search_uml_by_embedding(const std::vector<double>& query_embedding, double threshold) {
    std::shared_lock lock(mtx_);
    nlohmann::json result = nlohmann::json::array();
    double query_norm = 0.0;
    for (double v : query_embedding) query_norm += v * v;
    query_norm = std::sqrt(query_norm);
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "SELECT uml_name, embedding FROM uml_embeddings", -1, &stmt, nullptr) != SQLITE_OK) {
        Logger::Error("Failed to prepare search embeddings: {}", sqlite3_errmsg(db));
        return result;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const void* blob = sqlite3_column_blob(stmt, 1);
        int blob_size = sqlite3_column_bytes(stmt, 1);
        if (!blob || blob_size <= 0) continue;
        std::vector<float> floats(blob_size / sizeof(float));
        std::memcpy(floats.data(), blob, blob_size);
        std::vector<double> stored_embedding(floats.begin(), floats.end());
        double dot = 0.0;
        double norm_b = 0.0;
        size_t len = std::min(query_embedding.size(), stored_embedding.size());
        for (size_t i = 0; i < len; ++i) {
            dot += query_embedding[i] * stored_embedding[i];
            norm_b += stored_embedding[i] * stored_embedding[i];
        }
        norm_b = std::sqrt(norm_b);
        double similarity = (query_norm < 1e-8 || norm_b < 1e-8) ? 0.0 : dot / (query_norm * norm_b);
        if (similarity >= threshold) {
            nlohmann::json entry;
            entry["name"] = name;
            entry["similarity"] = similarity;
            nlohmann::json container = get_uml_container(name);
            if (container.contains("sources")) {
                entry["sources"] = container["sources"];
            }
            result.push_back(std::move(entry));
        }
    }
    sqlite3_finalize(stmt);
    std::sort(result.begin(), result.end(),
              [](const nlohmann::json& a, const nlohmann::json& b) {
                  return a["similarity"].get<double>() > b["similarity"].get<double>();
              });
    return result;
}

nlohmann::json DocumentStore::search_uml_nearest(const std::string& query, double threshold) {
    std::shared_lock lock(mtx_);
    std::string cleaned = clean_text(query);
    auto tokens = Vocabulary::tokenize(cleaned);
    std::string joined;
    for (const auto& t : tokens) {
        if (!joined.empty()) joined += " ";
        joined += t;
    }
    if (!net || net->input_size() <= 0) {
        return nlohmann::json{{"error", "Model not initialized. Please upload documents first."}};
    }
    auto query_vec = vocab->vectorize(joined);
    if ((int)query_vec.size() != net->input_size()) {
        return nlohmann::json{{"error", "Model mismatch. Please re-upload documents."}};
    }
    auto query_embedding = net->get_embedding(query_vec);
    double query_norm = 0.0;
    for (double v : query_embedding) query_norm += v * v;
    query_norm = std::sqrt(query_norm);

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "SELECT name, schema FROM uml", -1, &stmt, nullptr) != SQLITE_OK) {
        Logger::Error("search_uml_nearest: failed to prepare: {}", sqlite3_errmsg(db));
        return nlohmann::json::array();
    }
    nlohmann::json result = nlohmann::json::array();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const void* schema_blob = sqlite3_column_blob(stmt, 1);
        int schema_size = sqlite3_column_bytes(stmt, 1);
        std::string schema;
        if (schema_blob && schema_size > 0) {
            schema = std::string(static_cast<const char*>(schema_blob), schema_size);
        }
        std::string text = name + " " + schema;
        auto uml_vec = vocab->vectorize(text);
        if ((int)uml_vec.size() != net->input_size()) continue;
        auto uml_embedding = net->get_embedding(uml_vec);
        double dot = 0.0;
        double norm_b = 0.0;
        size_t len = std::min(query_embedding.size(), uml_embedding.size());
        for (size_t i = 0; i < len; ++i) {
            dot += query_embedding[i] * uml_embedding[i];
            norm_b += uml_embedding[i] * uml_embedding[i];
        }
        norm_b = std::sqrt(norm_b);
        double similarity = (query_norm < 1e-8 || norm_b < 1e-8) ? 0.0 : dot / (query_norm * norm_b);
        if (similarity >= threshold) {
            nlohmann::json entry;
            entry["name"] = name;
            entry["similarity"] = similarity;
            nlohmann::json container = get_uml_container(name);
            if (container.contains("sources")) {
                entry["sources"] = container["sources"];
            }
            result.push_back(std::move(entry));
        }
    }
    sqlite3_finalize(stmt);
    std::sort(result.begin(), result.end(),
              [](const nlohmann::json& a, const nlohmann::json& b) {
                  return a["similarity"].get<double>() > b["similarity"].get<double>();
              });
    return result;
}

nlohmann::json DocumentStore::get_uml_container(const std::string& name) {
    std::shared_lock lock(mtx_);
    nlohmann::json result;
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT id, name, schema FROM uml WHERE name=?", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result["id"] = sqlite3_column_int64(stmt, 0);
        result["name"] = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        const void* schema_blob = sqlite3_column_blob(stmt, 2);
        int schema_size = sqlite3_column_bytes(stmt, 2);
        if (schema_blob && schema_size > 0) {
            result["schema"] = std::string(static_cast<const char*>(schema_blob), schema_size);
        } else {
            result["schema"] = "";
        }
        int64_t uml_id = result["id"].get<int64_t>();
        sqlite3_stmt* src_stmt;
        sqlite3_prepare_v2(db,
            "SELECT u.id, t.name, u.source FROM uml_src u "
            "JOIN src_type t ON u.src_type_id = t.id "
            "WHERE u.uml_id=?", -1, &src_stmt, nullptr);
        sqlite3_bind_int64(src_stmt, 1, uml_id);
        nlohmann::json sources = nlohmann::json::array();
        while (sqlite3_step(src_stmt) == SQLITE_ROW) {
            nlohmann::json src;
            src["id"] = sqlite3_column_int64(src_stmt, 0);
            src["type"] = std::string(reinterpret_cast<const char*>(sqlite3_column_text(src_stmt, 1)));
            const void* src_blob = sqlite3_column_blob(src_stmt, 2);
            int src_size = sqlite3_column_bytes(src_stmt, 2);
            if (src_blob && src_size > 0) {
                src["content"] = std::string(static_cast<const char*>(src_blob), src_size);
            } else {
                src["content"] = "";
            }
            sources.push_back(std::move(src));
        }
        sqlite3_finalize(src_stmt);
        result["sources"] = std::move(sources);
    } else {
        result["error"] = "UML container not found";
    }
    sqlite3_finalize(stmt);
    return result;
}

nlohmann::json DocumentStore::search_uml(const std::string& query) {
    std::shared_lock lock(mtx_);
    nlohmann::json result = nlohmann::json::array();
    std::string cleaned = clean_text(query);
    auto tokens = Vocabulary::tokenize(cleaned);
    if (tokens.empty()) return result;
    std::string search_term;
    for (const auto& t : tokens) {
        if (!search_term.empty()) search_term += " ";
        search_term += t;
    }
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "SELECT u.id, u.name, CAST(u.schema AS TEXT) as schema_text "
        "FROM uml u WHERE u.name LIKE '%' || ? || '%' "
        "OR CAST(u.schema AS TEXT) LIKE '%' || ? || '%'",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, search_term.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, search_term.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        nlohmann::json entry;
        entry["id"] = sqlite3_column_int64(stmt, 0);
        entry["name"] = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        const char* schema_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        entry["schema_preview"] = schema_text ? std::string(schema_text).substr(0, 200) : "";
        result.push_back(std::move(entry));
    }
    sqlite3_finalize(stmt);
    sqlite3_prepare_v2(db,
        "SELECT DISTINCT u.id, u.name, CAST(s.source AS TEXT) as src_text "
        "FROM uml_src s JOIN uml u ON s.uml_id = u.id "
        "WHERE CAST(s.source AS TEXT) LIKE '%' || ? || '%'",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, search_term.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int64_t id = sqlite3_column_int64(stmt, 0);
        bool already_added = false;
        for (const auto& e : result) {
            if (e["id"].get<int64_t>() == id) { already_added = true; break; }
        }
        if (!already_added) {
            nlohmann::json entry;
            entry["id"] = id;
            entry["name"] = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
            const char* src_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            entry["source_preview"] = src_text ? std::string(src_text).substr(0, 200) : "";
            result.push_back(std::move(entry));
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

nlohmann::json DocumentStore::list_tables() {
    std::shared_lock lock(mtx_);
    nlohmann::json result = nlohmann::json::array();
    sqlite3_stmt* stmt;
    const char* sql = "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        Logger::Error("list_tables prepare failed: {}", sqlite3_errmsg(db));
        return result;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        result.push_back(name);
    }
    sqlite3_finalize(stmt);
    return result;
}

nlohmann::json DocumentStore::get_table_data(const std::string& table, const std::string& filter, int offset, int limit) {
    std::shared_lock lock(mtx_);
    nlohmann::json result;
    nlohmann::json tables = list_tables();
    bool found = false;
    for (const auto& t : tables) if (t == table) { found = true; break; }
    if (!found) {
        result["error"] = "Invalid table name";
        Logger::Warn("DocumentStore::get_table_data invalid table name {}", table);
        return result;
    }
    std::string pragma = "PRAGMA table_info(" + table + ")";
    sqlite3_stmt* stmt;
    std::vector<std::string> columns;
    if (sqlite3_prepare_v2(db, pragma.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* col = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            columns.push_back(col);
        }
        sqlite3_finalize(stmt);
    }
    result["columns"] = columns;
    std::string query = "SELECT * FROM " + table;
    if (!filter.empty()) {
        std::vector<std::string> conditions;
        for (const auto& col : columns) {
            conditions.push_back("CAST(" + col + " AS TEXT) LIKE '%' || ? || '%'");
        }
        query += " WHERE " + conditions[0];
        for (size_t i = 1; i < conditions.size(); ++i) query += " OR " + conditions[i];
    }
    query += " LIMIT ? OFFSET ?";
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        int err = sqlite3_errcode(db);
        if (err == SQLITE_BUSY) {
            result["error"] = "Database is busy – please retry later";
            Logger::Warn("DocumentStore::get_table_data: database busy for table {}", table);
        } else {
            result["error"] = sqlite3_errmsg(db);
            Logger::Error("DocumentStore::get_table_data prepare failed: {}", sqlite3_errmsg(db));
        }
        return result;
    }
    int param_idx = 1;
    if (!filter.empty()) {
        int param_count = sqlite3_bind_parameter_count(stmt);
        for (int i = 1; i <= param_count - 2; ++i) {
            sqlite3_bind_text(stmt, i, filter.c_str(), -1, SQLITE_TRANSIENT);
        }
        param_idx = param_count - 1;
    }
    sqlite3_bind_int(stmt, param_idx, limit);
    sqlite3_bind_int(stmt, param_idx + 1, offset);
    nlohmann::json rows = nlohmann::json::array();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        nlohmann::json row = nlohmann::json::array();
        for (int i = 0; i < sqlite3_column_count(stmt); ++i) {
            int type = sqlite3_column_type(stmt, i);
            if (type == SQLITE_INTEGER) {
                row.push_back(sqlite3_column_int64(stmt, i));
            } else if (type == SQLITE_FLOAT) {
                row.push_back(sqlite3_column_double(stmt, i));
            } else if (type == SQLITE_TEXT) {
                const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
                std::string val = txt ? txt : "";
                if (val.length() > 100) val = val.substr(0, 100) + "...";
                row.push_back(val);
            } else if (type == SQLITE_BLOB) {
                const void* blob = sqlite3_column_blob(stmt, i);
                int blob_size = sqlite3_column_bytes(stmt, i);
                if (blob && blob_size > 0) {
                    std::string val(static_cast<const char*>(blob), blob_size);
                    if (val.length() > 200) val = val.substr(0, 200) + "...";
                    row.push_back(val);
                } else {
                    row.push_back("");
                }
            } else {
                row.push_back(nullptr);
            }
        }
        rows.push_back(row);
    }
    sqlite3_finalize(stmt);
    result["rows"] = rows;
    result["offset"] = offset;
    result["limit"] = limit;
    result["has_more"] = (limit > 0) && (static_cast<int>(rows.size()) == limit);
    return result;
}

nlohmann::json DocumentStore::list_uml_blocks() {
    std::shared_lock lock(mtx_);
    nlohmann::json result = nlohmann::json::array();
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "SELECT id, name, schema FROM uml", -1, &stmt, nullptr) != SQLITE_OK) {
        return result;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        nlohmann::json block;
        int64_t id = sqlite3_column_int64(stmt, 0);
        block["id"] = id;
        block["name"] = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        const void* schema_blob = sqlite3_column_blob(stmt, 2);
        int schema_size = sqlite3_column_bytes(stmt, 2);
        if (schema_blob && schema_size > 0) {
            block["schema"] = std::string(static_cast<const char*>(schema_blob), schema_size);
        } else {
            block["schema"] = "";
        }
        sqlite3_stmt* src_stmt;
        sqlite3_prepare_v2(db,
            "SELECT t.name, u.source FROM uml_src u "
            "JOIN src_type t ON u.src_type_id = t.id "
            "WHERE u.uml_id=?", -1, &src_stmt, nullptr);
        sqlite3_bind_int64(src_stmt, 1, id);
        nlohmann::json sources = nlohmann::json::array();
        while (sqlite3_step(src_stmt) == SQLITE_ROW) {
            nlohmann::json src;
            src["type"] = std::string(reinterpret_cast<const char*>(sqlite3_column_text(src_stmt, 0)));
            const void* src_blob = sqlite3_column_blob(src_stmt, 1);
            int src_size = sqlite3_column_bytes(src_stmt, 1);
            if (src_blob && src_size > 0) {
                src["content"] = std::string(static_cast<const char*>(src_blob), src_size);
            } else {
                src["content"] = "";
            }
            sources.push_back(std::move(src));
        }
        sqlite3_finalize(src_stmt);
        block["sources"] = sources;
        result.push_back(std::move(block));
    }
    sqlite3_finalize(stmt);
    return result;
}

nlohmann::json DocumentStore::compose_uml_project(const std::vector<std::string>& block_names) {
    std::shared_lock lock(mtx_);
    nlohmann::json project;
    project["blocks"] = nlohmann::json::array();
    std::string uml_text = "@startuml PhoneBookApp\n";
    uml_text += "skinparam packageStyle rectangle\n\n";

    for (const auto& name : block_names) {
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "SELECT id, name, schema FROM uml WHERE name=?", -1, &stmt, nullptr) != SQLITE_OK) continue;
        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_ROW) { sqlite3_finalize(stmt); continue; }
        int64_t id = sqlite3_column_int64(stmt, 0);
        std::string uml_name(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        const void* schema_blob = sqlite3_column_blob(stmt, 2);
        int schema_size = sqlite3_column_bytes(stmt, 2);
        std::string schema;
        if (schema_blob && schema_size > 0) {
            schema = std::string(static_cast<const char*>(schema_blob), schema_size);
        }
        sqlite3_finalize(stmt);

        nlohmann::json block;
        block["name"] = uml_name;
        block["schema"] = schema;

        sqlite3_stmt* src_stmt;
        sqlite3_prepare_v2(db,
            "SELECT t.name, u.source FROM uml_src u "
            "JOIN src_type t ON u.src_type_id = t.id "
            "WHERE u.uml_id=?", -1, &src_stmt, nullptr);
        sqlite3_bind_int64(src_stmt, 1, id);
        nlohmann::json sources = nlohmann::json::array();
        while (sqlite3_step(src_stmt) == SQLITE_ROW) {
            nlohmann::json src;
            src["type"] = std::string(reinterpret_cast<const char*>(sqlite3_column_text(src_stmt, 0)));
            const void* src_blob = sqlite3_column_blob(src_stmt, 1);
            int src_size = sqlite3_column_bytes(src_stmt, 1);
            if (src_blob && src_size > 0) {
                src["content"] = std::string(static_cast<const char*>(src_blob), src_size);
            } else {
                src["content"] = "";
            }
            sources.push_back(std::move(src));
        }
        sqlite3_finalize(src_stmt);
        block["sources"] = sources;
        project["blocks"].push_back(std::move(block));

        uml_text += "' Block: " + uml_name + "\n";
        uml_text += schema + "\n\n";
    }
    uml_text += "@enduml";
    project["uml_composite"] = uml_text;

    std::string combined_sources;
    for (auto& b : project["blocks"]) {
        if (b.contains("sources")) {
            for (auto& src : b["sources"]) {
                if (src.contains("content") && !src["content"].get<std::string>().empty()) {
                    combined_sources += "// === " + b["name"].get<std::string>() + " (" + src["type"].get<std::string>() + ") ===\n";
                    combined_sources += src["content"].get<std::string>() + "\n\n";
                }
            }
        }
    }
    project["combined_sources"] = combined_sources;
    return project;
}
