#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <random>
#include <vector>
#include <thread>

#include "logger.hpp"

class NeuralNetwork {
public:
    NeuralNetwork(int input_size, int hidden_size, int output_size);
    void init_random();
    void expand_outputs(int new_output_size);
    void expand_inputs(int new_input_size);
    std::vector<double> forward(const std::vector<double>& input) const;
    void train_batch(const std::vector<std::vector<double>>& inputs,
                     const std::vector<int>& labels, double lr);
    std::pair<int,double> predict(const std::vector<double>& input) const;
    std::vector<double> get_embedding(const std::vector<double>& input) const;
    int input_size() const;
    int output_size() const;
    std::vector<double>& GetW1();
    std::vector<double>& GetB1();
    std::vector<double>& GetW2();
    std::vector<double>& GetB2();
    const std::vector<double>& GetW1() const;
    const std::vector<double>& GetB1() const;
    const std::vector<double>& GetW2() const;
    const std::vector<double>& GetB2() const;

private:
    int in, hn, out;
    std::vector<double> W1, b1, W2, b2;
    void softmax(std::vector<double>& x) const;
};
