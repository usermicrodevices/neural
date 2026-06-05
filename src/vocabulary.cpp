#include "vocabulary.hpp"

Vocabulary::Vocabulary() : doc_count(0) {}

void Vocabulary::clear() { words.clear(); word2idx.clear(); df.clear(); doc_count = 0; }

bool Vocabulary::is_valid_word(const std::string& w) {
    if (w.length() < 2) return false;
    bool has_letter = false;
    for (unsigned char c : w) {
        if (!std::isalnum(c) && c != '_') return false;
        if (std::isalpha(c)) has_letter = true;
    }
    return has_letter;
}

std::vector<std::string> Vocabulary::tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    const char* p = text.c_str();
    const char* end = p + text.size();
    while (p < end) {
        while (p < end && *p == ' ') ++p;
        if (p >= end) break;
        const char* start = p;
        while (p < end && *p != ' ') ++p;
        tokens.emplace_back(start, p - start);
    }
    return tokens;
}

int Vocabulary::add_words(const std::string& text) {
    auto tokens = tokenize(text);
    int added = 0;
    for (const auto& w : tokens) {
        if (!is_valid_word(w)) continue;
        if (word2idx.find(w) != word2idx.end()) continue;
        if (static_cast<int>(words.size()) >= MAX_VOCAB) continue;
        word2idx[w] = static_cast<int>(words.size());
        words.push_back(w);
        df.push_back(0);
        ++added;
    }
    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        std::string bigram = tokens[i] + "_" + tokens[i+1];
        if (!is_valid_word(bigram)) continue;
        if (word2idx.find(bigram) != word2idx.end()) continue;
        if (static_cast<int>(words.size()) >= MAX_VOCAB) continue;
        word2idx[bigram] = static_cast<int>(words.size());
        words.push_back(bigram);
        df.push_back(0);
        ++added;
    }
    return added;
}

int Vocabulary::size() const { return words.size(); }

double Vocabulary::idf(int word_idx) const {
    if (word_idx < 0 || word_idx >= static_cast<int>(df.size())) return 0.0;
    int freq = df[word_idx];
    if (freq == 0) return 0.0;
    return std::log((doc_count + 1.0) / (freq + 1.0)) + 1.0;
}

std::vector<double> Vocabulary::vectorize(const std::string& text) const {
    std::vector<double> vec(words.size(), 0.0);
    auto tokens = tokenize(text);
    std::unordered_map<int, int> tf;
    for (const auto& w : tokens) {
        auto it = word2idx.find(w);
        if (it != word2idx.end()) tf[it->second]++;
    }
    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        std::string bigram = tokens[i] + "_" + tokens[i+1];
        auto it = word2idx.find(bigram);
        if (it != word2idx.end()) tf[it->second]++;
    }
    for (const auto& p : tf) {
        vec[p.first] = static_cast<double>(p.second) * idf(p.first);
    }
    return vec;
}

void Vocabulary::add_document(const std::string& text, int chunk_id) {
    (void)chunk_id;
    auto tokens = tokenize(text);
    std::unordered_map<int, int> term_freq;
    for (const auto& w : tokens) {
        auto it = word2idx.find(w);
        if (it != word2idx.end()) term_freq[it->second]++;
    }
    for (auto& [idx, cnt] : term_freq) {
        if (idx < static_cast<int>(df.size())) df[idx] += cnt;
    }
    doc_count++;
}

void Vocabulary::update_total_docs(int new_total_docs) { doc_count = new_total_docs; }

int Vocabulary::total_docs() const { return doc_count; }

const std::string& Vocabulary::word(int idx) const { return words[idx]; }

size_t Vocabulary::words_size(){return words.size();}

void Vocabulary::words_resize(int size){words.resize(size);}

void Vocabulary::set_word(int id, const std::string& value){words[id] = value;}

void Vocabulary::set_word2idx(const std::string& value, int id){word2idx[value] = id;}

size_t Vocabulary::df_size(){return df.size();}

int Vocabulary::get_df(int id){return df[id];}

void Vocabulary::set_df(int id, int value){df[id] = value;}

void Vocabulary::df_resize(int size){df.resize(size);}

void Vocabulary::df_resize(int size, const int default_value){df.resize(size, default_value);}

int Vocabulary::get_doc_count(){return doc_count;}

void Vocabulary::set_doc_count(int value){doc_count = value;}
