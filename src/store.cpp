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
    "CREATE TABLE IF NOT EXISTS chunks (id INTEGER PRIMARY KEY, text TEXT);"
    "CREATE TABLE IF NOT EXISTS vocab (word TEXT PRIMARY KEY, idx INTEGER, df INTEGER);"
    "CREATE TABLE IF NOT EXISTS model (layer INTEGER, weights BLOB, biases BLOB);"
    "CREATE TABLE IF NOT EXISTS chunk_tags (chunk_id INTEGER, tag TEXT, PRIMARY KEY (chunk_id, tag));";
    char* errmsg = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &errmsg) != SQLITE_OK) {
        std::string error = errmsg;
        sqlite3_free(errmsg);
        throw std::runtime_error("Failed to create tables: " + error);
    }
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
    std::ostringstream o;
    for (char c : s) {
        if (c == '"') o << "\\\"";
        else if (c == '\\') o << "\\\\";
        else if (c == '\n') o << "\\n";
        else if (c == '\r') o << "\\r";
        else if (c == '\t') o << "\\t";
        else o << c;
    }
    return o.str();
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
        if (vocab->size() > 0) {
            Logger::Warn("Vocabulary size ({}) != network input size ({}). Recreating network.", vocab->size(), net->input_size());
        }
        net = std::make_unique<NeuralNetwork>(std::max(1, vocab->size()), HIDDEN_SIZE, net->output_size());
        net->init_random();
    }
}

void DocumentStore::save_state() {
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
}

void DocumentStore::add_tag(const std::string& tag, int chunk_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::string cleaned_tag = clean_text(tag);
    sqlite3_stmt* insert_tag = nullptr;
    sqlite3_prepare_v2(db, "INSERT OR IGNORE INTO chunk_tags(chunk_id, tag) VALUES(?,?)", -1, &insert_tag, nullptr);
    sqlite3_bind_int(insert_tag, 1, chunk_id);
    sqlite3_bind_text(insert_tag, 2, cleaned_tag.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(insert_tag);
    sqlite3_finalize(insert_tag);
    std::vector<std::vector<double>> X;
    std::vector<int> Y;
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT id, text FROM chunks ORDER BY id", -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        std::string raw_txt(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        std::string clean_txt = clean_text(raw_txt);
        auto vec = vocab->vectorize(clean_txt);
        X.push_back(vec);
        Y.push_back(id - 1);
    }
    sqlite3_finalize(stmt);
    int total_chunks = (int)X.size();
    if (total_chunks > net->output_size()) {
        net->expand_outputs(total_chunks);
    }
    const int epochs = 20;
    for (int e = 0; e < epochs; ++e) {
        net->train_batch(X, Y, 0.01);
    }
    save_state();
}

void DocumentStore::add_document(const std::string& text, const std::string& tags,
                                 std::function<void(int)> progress_cb, bool serialization) {
    std::lock_guard<std::mutex> lock(mtx_);
    Logger::Trace("DocumentStore::add_document adding document ({} bytes)", text.size());
    auto raw_chunks = split_into_chunks(text);
    Logger::Trace("DocumentStore::add_document split into {} chunks", raw_chunks.size());
    std::vector<std::string> tag_phrases;
    std::stringstream ss(tags);
    std::string tag;
    while (std::getline(ss, tag, ';')) {
        size_t first = tag.find_first_not_of(" \t\n\r");
        if (first == std::string::npos) continue;
        size_t last = tag.find_last_not_of(" \t\n\r");
        tag = tag.substr(first, last - first + 1);
        if (tag.empty()) continue;
        std::replace(tag.begin(), tag.end(), ' ', '_');
        tag_phrases.push_back(tag);
    }
    std::vector<std::string> unique_chunks;
    sqlite3_stmt* duplicate_stmt = nullptr;
    sqlite3_prepare_v2(db, "SELECT 1 FROM chunks WHERE text = ? LIMIT 1", -1, &duplicate_stmt, nullptr);
    uint duplicate_count = 0;
    for (const auto& raw_ch : raw_chunks) {
        sqlite3_reset(duplicate_stmt);
        sqlite3_bind_text(duplicate_stmt, 1, raw_ch.c_str(), -1, SQLITE_TRANSIENT);
        bool is_duplicate = (sqlite3_step(duplicate_stmt) == SQLITE_ROW);
        if (!is_duplicate) {
            unique_chunks.push_back(raw_ch);
        } else {
            Logger::Warn("DocumentStore::add_document duplicate chunk skipped: {}", raw_ch.substr(0, 10));
            duplicate_count++;
        }
    }
    sqlite3_finalize(duplicate_stmt);
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
        std::string cleaned_tag = clean_text(tp);
        vocab->add_words(cleaned_tag);
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
    int total_chunks = 0;
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM chunks", -1, &stmt, nullptr);
    if (sqlite3_step(stmt) == SQLITE_ROW) total_chunks = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    if (new_vocab_size > old_vocab_size) {
        auto new_net = std::make_unique<NeuralNetwork>(new_vocab_size, HIDDEN_SIZE, net->output_size());
        new_net->init_random();
        net = std::move(new_net);
    }
    if (total_chunks > net->output_size()) {
        net->expand_outputs(total_chunks);
    }
    std::vector<std::vector<double>> X;
    std::vector<int> Y;
    sqlite3_prepare_v2(db, "SELECT id, text FROM chunks ORDER BY id", -1, &stmt, nullptr);
    sqlite3_stmt* tag_stmt = nullptr;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        std::string raw_txt(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        std::string clean_txt = clean_text(raw_txt);
        auto vec = vocab->vectorize(clean_txt);
        sqlite3_prepare_v2(db, "SELECT tag FROM chunk_tags WHERE chunk_id=?", -1, &tag_stmt, nullptr);
        sqlite3_bind_int(tag_stmt, 1, id);
        while (sqlite3_step(tag_stmt) == SQLITE_ROW) {
            std::string tag_str(reinterpret_cast<const char*>(sqlite3_column_text(tag_stmt, 0)));
            auto tag_vec = vocab->vectorize(tag_str);
            for (size_t i = 0; i < vec.size(); ++i) vec[i] += tag_vec[i];
        }
        sqlite3_finalize(tag_stmt);
        X.push_back(vec);
        Y.push_back(id - 1);
    }
    sqlite3_finalize(stmt);
    const int epochs = 100;
    for (int e = 0; e < epochs; ++e) {
        net->train_batch(X, Y, 0.01);
        if (progress_cb) progress_cb(static_cast<int>((e+1)*100.0/epochs));
    }
    save_state();
    if (serialization) serialize();
    else Logger::Warn("DocumentStore::add_document stay in‑memory only.");
}

std::string DocumentStore::get_answer(const std::string& prompt, double threshold) {
    Logger::Trace("DocumentStore::get_answer confidence threshold: {}; question: {}", threshold, prompt);
    std::lock_guard<std::mutex> lock(mtx_);
    std::string cleaned = clean_text(prompt);
    std::vector<std::string> tokens;
    std::istringstream iss(cleaned);
    std::string word;
    while (iss >> word) tokens.push_back(word);
    std::set<std::string> candidate_tags;
    for (const auto& w : tokens) candidate_tags.insert(w);
    for (size_t i = 0; i + 1 < tokens.size(); ++i) candidate_tags.insert(tokens[i] + "_" + tokens[i+1]);
    if (!candidate_tags.empty()) {
        std::string tag_placeholders;
        for (size_t i = 0; i < candidate_tags.size(); ++i) {
            if (i > 0) tag_placeholders += ";";
            tag_placeholders += "?";
        }
        std::string tag_query = "SELECT DISTINCT chunk_id FROM chunk_tags WHERE tag IN (" + tag_placeholders + ")";
        sqlite3_stmt* tag_stmt;
        sqlite3_prepare_v2(db, tag_query.c_str(), -1, &tag_stmt, nullptr);
        int idx = 1;
        for (const auto& t : candidate_tags) sqlite3_bind_text(tag_stmt, idx++, t.c_str(), -1, SQLITE_TRANSIENT);
        std::vector<int> candidate_chunks;
        while (sqlite3_step(tag_stmt) == SQLITE_ROW) candidate_chunks.push_back(sqlite3_column_int(tag_stmt, 0));
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
    auto vec = vocab->vectorize(prompt);
    if ((int)vec.size() != net->input_size()) {
        Logger::Error("DocumentStore::get_answer vocabulary size ({}) != network input size ({}). Reinitializing network.", vec.size(), net->input_size());
        net = std::make_unique<NeuralNetwork>(vec.size(), HIDDEN_SIZE, net->output_size());
        net->init_random();
        return R"({"answer":"Model mismatch. Please re‑upload documents.","chunk_id":-1})";
    }
    auto [idx, conf] = net->predict(vec);
    Logger::Trace("DocumentStore::get_answer prediction: chunk {}, confidence {:.4f}", idx, conf);
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
    Logger::Trace("DocumentStore::get_answer {}", result.substr(0, 50));
    return result;
}

std::vector<std::string> DocumentStore::split_into_chunks(const std::string& text) {
    std::vector<std::string> chunks;
    std::istringstream stream(text);
    std::string line, current;
    while (std::getline(stream, line, '\n')) {
        if (!current.empty() && current.size() + line.size() + 1 > MAX_CHUNK_SIZE) {
            chunks.push_back(current);
            current.clear();
        }
        if (!current.empty()) current += '\n';
        current += line;
    }
    if (!current.empty()) chunks.push_back(current);
    if (chunks.empty() && !text.empty())
        chunks.push_back(text);
    return chunks;
}

std::string DocumentStore::clean_text(const std::string& text) {
    std::string out;
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
    if (err_code == SQLITE_OK)
        Logger::Info("👌DocumentStore::serialize success sqlite3_open file {}", persistent_path);
    else {
        Logger::Error("🚫DocumentStore::serialize: failed to open persistent DB for writing; error code {}", err_code);
        return;
    }
    sqlite3_backup* backup = sqlite3_backup_init(file_db, "main", db, "main");
    if (backup) {
        sqlite3_backup_step(backup, -1);
        err_code = sqlite3_backup_finish(backup);
        if (err_code == SQLITE_OK)
            Logger::Info("👌DocumentStore::serialize in‑memory DB to {}", persistent_path);
        else
            Logger::Error("🚫DocumentStore::serialize: failed sqlite3_backup_finish file {}; error code {}", persistent_path, err_code);
    }
    else
        Logger::Error("🚫DocumentStore::serialize sqlite3_backup_init");
    err_code = sqlite3_close(file_db);
    if (err_code == SQLITE_OK)
        Logger::Info("👌DocumentStore::serialize success sqlite3_close file {}", persistent_path);
    else
        Logger::Error("🚫DocumentStore::serialize: failed sqlite3_close file {}; error code {}", persistent_path, err_code);
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
