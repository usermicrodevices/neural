#include "neural.hpp"

NeuralNetwork::NeuralNetwork(int input_size, int hidden_size, int output_size)
    : in(input_size), hn(hidden_size), out(output_size)
{}

std::vector<double>& NeuralNetwork::GetW1() { return W1; }
std::vector<double>& NeuralNetwork::GetB1() { return b1; }
std::vector<double>& NeuralNetwork::GetW2() { return W2; }
std::vector<double>& NeuralNetwork::GetB2() { return b2; }

const std::vector<double>& NeuralNetwork::GetW1() const { return W1; }
const std::vector<double>& NeuralNetwork::GetB1() const { return b1; }
const std::vector<double>& NeuralNetwork::GetW2() const { return W2; }
const std::vector<double>& NeuralNetwork::GetB2() const { return b2; }

void NeuralNetwork::init_random() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<double> dist(0.0, 0.1);
    W1.resize(in * hn);
    b1.resize(hn);
    W2.resize(hn * out);
    b2.resize(out);
    for (auto& v : W1) v = dist(gen);
    for (auto& v : b1) v = 0.0;
    for (auto& v : W2) v = dist(gen);
    for (auto& v : b2) v = 0.0;
}

void NeuralNetwork::expand_inputs(int new_input_size) {
    if (new_input_size <= in) return;
    std::vector<double> newW1(new_input_size * hn);
    for (int i = 0; i < in; ++i)
        for (int j = 0; j < hn; ++j)
            newW1[i * hn + j] = W1[i * hn + j];
    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<double> dist(0.0, 0.1);
    for (int i = in; i < new_input_size; ++i)
        for (int j = 0; j < hn; ++j)
            newW1[i * hn + j] = dist(gen);
    W1.swap(newW1);
    in = new_input_size;
}

void NeuralNetwork::expand_outputs(int new_output_size) {
    if (new_output_size <= out) return;
    std::vector<double> newW2(hn * new_output_size);
    std::vector<double> newb2(new_output_size);
    for (int j = 0; j < hn; ++j)
        for (int k = 0; k < out; ++k)
            newW2[j * new_output_size + k] = W2[j * out + k];
    for (int k = 0; k < out; ++k) newb2[k] = b2[k];
    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<double> dist(0.0, 0.1);
    for (int j = 0; j < hn; ++j)
        for (int k = out; k < new_output_size; ++k)
            newW2[j * new_output_size + k] = dist(gen);
    for (int k = out; k < new_output_size; ++k) newb2[k] = 0.0;
    W2.swap(newW2);
    b2.swap(newb2);
    out = new_output_size;
}

std::vector<double> NeuralNetwork::forward(const std::vector<double>& input) const {
    if ((int)input.size() != in) {
        Logger::Error("NeuralNetwork::forward: input size mismatch (expected {}, got {})", in, input.size());
        return std::vector<double>(out, 0.0);
    }
    std::vector<double> h(hn, 0.0);
    for (int j = 0; j < hn; ++j) {
        double sum = b1[j];
        for (int i = 0; i < in; ++i) sum += W1[i * hn + j] * input[i];
        h[j] = std::max(0.0, sum);
    }
    std::vector<double> scores(out, 0.0);
    for (int k = 0; k < out; ++k) {
        double sum = b2[k];
        for (int j = 0; j < hn; ++j) sum += W2[j * out + k] * h[j];
        scores[k] = sum;
    }
    softmax(scores);
    return scores;
}

void NeuralNetwork::train_batch(const std::vector<std::vector<double>>& inputs,
                                const std::vector<int>& labels, double lr) {
    int N = inputs.size();
    if (N == 0) return;
    for (const auto& x : inputs) {
        if ((int)x.size() != in) {
            Logger::Error("train_batch: input size mismatch (expected {}, got {})", in, x.size());
            return;
        }
    }
    unsigned int num_threads = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::vector<double>> dW1_local(num_threads, std::vector<double>(in * hn, 0.0));
    std::vector<std::vector<double>> db1_local(num_threads, std::vector<double>(hn, 0.0));
    std::vector<std::vector<double>> dW2_local(num_threads, std::vector<double>(hn * out, 0.0));
    std::vector<std::vector<double>> db2_local(num_threads, std::vector<double>(out, 0.0));
    auto worker = [&](int tid, int start, int end) {
        std::vector<double> dW1(in * hn, 0.0);
        std::vector<double> db1(hn, 0.0);
        std::vector<double> dW2(hn * out, 0.0);
        std::vector<double> db2(out, 0.0);
        for (int n = start; n < end; ++n) {
            const auto& x = inputs[n];
            int y = labels[n];
            std::vector<double> h(hn, 0.0);
            for (int j = 0; j < hn; ++j) {
                double sum = b1[j];
                for (int i = 0; i < in; ++i) sum += W1[i * hn + j] * x[i];
                h[j] = std::max(0.0, sum);
            }
            std::vector<double> scores(out, 0.0);
            for (int k = 0; k < out; ++k) {
                double sum = b2[k];
                for (int j = 0; j < hn; ++j) sum += W2[j * out + k] * h[j];
                scores[k] = sum;
            }
            softmax(scores);

            for (int k = 0; k < out; ++k) {
                double grad = scores[k] - (k == y ? 1.0 : 0.0);
                db2[k] += grad;
                for (int j = 0; j < hn; ++j) dW2[j * out + k] += grad * h[j];
            }
            for (int j = 0; j < hn; ++j) {
                double grad = 0.0;
                for (int k = 0; k < out; ++k) grad += W2[j * out + k] * (scores[k] - (k == y ? 1.0 : 0.0));
                if (h[j] <= 0) grad = 0.0;
                db1[j] += grad;
                for (int i = 0; i < in; ++i) dW1[i * hn + j] += grad * x[i];
            }
        }
        dW1_local[tid] = std::move(dW1);
        db1_local[tid] = std::move(db1);
        dW2_local[tid] = std::move(dW2);
        db2_local[tid] = std::move(db2);
    };
    std::vector<std::thread> threads;
    int chunk_size = (N + num_threads - 1) / num_threads;
    for (unsigned int t = 0; t < num_threads; ++t) {
        int start = t * chunk_size;
        int end = std::min(start + chunk_size, N);
        if (start >= end) break;
        threads.emplace_back(worker, t, start, end);
    }
    for (auto& th : threads) th.join();
    std::vector<double> dW1_total(in * hn, 0.0);
    std::vector<double> db1_total(hn, 0.0);
    std::vector<double> dW2_total(hn * out, 0.0);
    std::vector<double> db2_total(out, 0.0);
    for (unsigned int t = 0; t < num_threads; ++t) {
        for (size_t i = 0; i < dW1_total.size(); ++i) dW1_total[i] += dW1_local[t][i];
        for (size_t i = 0; i < db1_total.size(); ++i) db1_total[i] += db1_local[t][i];
        for (size_t i = 0; i < dW2_total.size(); ++i) dW2_total[i] += dW2_local[t][i];
        for (size_t i = 0; i < db2_total.size(); ++i) db2_total[i] += db2_local[t][i];
    }
    double scale = lr / N;
    for (int j = 0; j < hn; ++j) {
        b1[j] -= scale * db1_total[j];
        for (int i = 0; i < in; ++i) W1[i * hn + j] -= scale * dW1_total[i * hn + j];
    }
    for (int k = 0; k < out; ++k) {
        b2[k] -= scale * db2_total[k];
        for (int j = 0; j < hn; ++j) W2[j * out + k] -= scale * dW2_total[j * out + k];
    }
}

std::pair<int,double> NeuralNetwork::predict(const std::vector<double>& input) const {
    auto probs = forward(input);
    auto max_it = std::max_element(probs.begin(), probs.end());
    int idx = std::distance(probs.begin(), max_it);
    return {idx, *max_it};
}

void NeuralNetwork::softmax(std::vector<double>& x) const {
    double max_val = *std::max_element(x.begin(), x.end());
    double sum = 0.0;
    for (auto& v : x) {
        v = std::exp(v - max_val);
        sum += v;
    }
    for (auto& v : x) v /= sum;
}

int NeuralNetwork::input_size() const { return in; }

int NeuralNetwork::output_size() const { return out; }
