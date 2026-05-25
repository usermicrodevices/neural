#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <utility>

struct ChunkTF {
    int id;
    std::vector<std::pair<int,int>> tf;

};

class Vocabulary {
public:
    static constexpr int MAX_VOCAB = 100000;
    Vocabulary();
    void clear();
    int add_words(const std::string& text);
    int size() const;
    double idf(int word_idx) const;
    std::vector<double> vectorize(const std::string& text) const;
    void add_document(const std::string& text, int chunk_id);
    void update_total_docs(int new_total_docs);
    int total_docs() const;
    const std::string& word(int idx) const;
    size_t words_size();
    void words_resize(int size);
    void set_word(int id, const std::string& value);
    void set_word2idx(const std::string& value, int id);
    size_t df_size();
    void df_resize(int size);
    void df_resize(int size, const int default_value);
    int get_df(int id);
    void set_df(int id, int value);
    int get_doc_count();
    void set_doc_count(int value);

private:
    std::vector<std::string> words;
    std::unordered_map<std::string, int> word2idx;
    std::vector<int> df;
    int doc_count;
    std::vector<ChunkTF> chunk_tfs;
    void rebuild_chunk_tfs();
};
