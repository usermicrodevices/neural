#include "vocabulary.hpp"

Vocabulary::Vocabulary() : doc_count(0) {}

void Vocabulary::clear() { words.clear(); word2idx.clear(); df.clear(); doc_count = 0; chunk_tfs.clear(); }

int Vocabulary::add_words(const std::string& text) {
    std::istringstream iss(text);
    std::string word;
    int added = 0;
    while (iss >> word) {
        if (word.empty()) continue;
        auto it = word2idx.find(word);
        if (it == word2idx.end()) {
            if (static_cast<int>(words.size()) >= MAX_VOCAB) continue;
            word2idx[word] = words.size();
            words.push_back(word);
            df.push_back(0);
            ++added;
        }
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
    std::unordered_map<int, int> tf;
    std::istringstream iss(text);
    std::string word;
    while (iss >> word) {
        auto it = word2idx.find(word);
        if (it != word2idx.end()) tf[it->second]++;
    }
    for (auto& p : tf) {
        int idx = p.first;
        double idf_val = idf(idx);
        vec[idx] = static_cast<double>(p.second) * idf_val;
    }
    return vec;
}

void Vocabulary::add_document(const std::string& text, int chunk_id) {
    std::vector<std::pair<int,int>> term_freq;
    std::istringstream iss(text);
    std::string word;
    while (iss >> word) {
        auto it = word2idx.find(word);
        if (it != word2idx.end()) {
            int idx = it->second;
            auto pos = std::find_if(term_freq.begin(), term_freq.end(),
                [idx](const auto& p) { return p.first == idx; });
            if (pos == term_freq.end()) term_freq.emplace_back(idx, 1);
            else pos->second++;
        }
    }
    chunk_tfs.push_back({chunk_id, std::move(term_freq)});
    for (auto& [idx, cnt] : chunk_tfs.back().tf) {
        if (idx < static_cast<int>(df.size())) df[idx]++;
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

void Vocabulary::rebuild_chunk_tfs() {}
