#include "linear.h"

#include <cmath>
#include <stdexcept>
#include <utility>

std::vector<double> solve_linear_system(std::vector<std::vector<double>> matrix,
                                        std::vector<double> rhs) {
    const std::size_t n = rhs.size();
    if (matrix.size() != n) {
        throw std::runtime_error("Linear system matrix size does not match rhs");
    }

    for (std::size_t i = 0; i < n; ++i) {
        if (matrix[i].size() != n) {
            throw std::runtime_error("Linear system matrix must be square");
        }
    }

    for (std::size_t col = 0; col < n; ++col) {
        std::size_t pivot = col;
        double max_value = std::fabs(matrix[pivot][col]);
        for (std::size_t row = col + 1; row < n; ++row) {
            const double candidate = std::fabs(matrix[row][col]);
            if (candidate > max_value) {
                max_value = candidate;
                pivot = row;
            }
        }

        if (max_value < 1e-12) {
            throw std::runtime_error("Circuit matrix is singular or ill-conditioned");
        }

        if (pivot != col) {
            std::swap(matrix[pivot], matrix[col]);
            std::swap(rhs[pivot], rhs[col]);
        }

        const double pivot_value = matrix[col][col];
        for (std::size_t row = col + 1; row < n; ++row) {
            const double factor = matrix[row][col] / pivot_value;
            if (factor == 0.0) {
                continue;
            }
            for (std::size_t k = col; k < n; ++k) {
                matrix[row][k] -= factor * matrix[col][k];
            }
            rhs[row] -= factor * rhs[col];
        }
    }

    std::vector<double> solution(n, 0.0);
    for (std::size_t i = n; i-- > 0;) {
        double sum = rhs[i];
        for (std::size_t j = i + 1; j < n; ++j) {
            sum -= matrix[i][j] * solution[j];
        }
        solution[i] = sum / matrix[i][i];
    }

    return solution;
}
