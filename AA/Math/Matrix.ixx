module; // Global fragment başlangıcı
#include <cassert>

export module Radian.Math:Matrix;
import std;

export namespace Radian::Math {
    template<typename T, size_t R, size_t C>
    struct alignas(sizeof(T)* ((R* C % 4 == 0) ? 4 : 1)) Matrix {

        using value_type = T;
        static constexpr size_t rows = R;
        static constexpr size_t cols = C;

        std::array<T, R* C> data{};

        template <typename Self>
        constexpr auto view(this Self&& self) noexcept {
            return std::mdspan(self.data.data(), std::extents<size_t, R, C>{});
        }

        template <typename Self>
        constexpr auto&& operator[](this Self&& self, size_t r, size_t c) noexcept {
            return self.data[r * C + c];
        }

        template <typename Self>
        constexpr auto* ptr(this Self&& self) noexcept {
            return self.data.data();
        }

        static constexpr size_t size() noexcept {
            return R * C;
        }

        template <typename Self>
        constexpr auto transposed_view(this Self&& self) noexcept {
            using TransposedLayout = std::layout_left;

            return std::mdspan<T, std::extents<size_t, C, R>, TransposedLayout>(
                self.data.data(), std::extents<size_t, C, R>{}
            );
        }


        template <typename Self, typename OtherT, size_t OtherR, size_t OtherC>
        constexpr auto operator+(this Self&& self, const Matrix<OtherT, OtherR, OtherC>& other) noexcept {
            // Derleme zamanı boyut kontrolü
            static_assert(R == OtherR && C == OtherC,
                "ERROR: Matrix dimensions must match for addition.");

            Matrix<T, R, C> result;
            std::transform(self.data.begin(), self.data.end(),
                other.data.begin(),
                result.data.begin(),
                std::plus<>{});
            return result;
        }

        template <typename Self, typename OtherT, size_t OtherR, size_t OtherC>
        constexpr auto operator-(this Self&& self, const Matrix<OtherT, OtherR, OtherC>& other) noexcept {
            static_assert(R == OtherR && C == OtherC,
                "ERROR: Matrix dimensions must match for subtraction.");

            Matrix<T, R, C> result;
            std::transform(self.data.begin(), self.data.end(),
                other.data.begin(),
                result.data.begin(),
                std::minus<>{});
            return result;
        }

        // 2. SKALER ÇARPMA (Matrix * Scalar)
        template <typename Self, typename ScalarT>
        constexpr auto operator*(this Self&& self, ScalarT scalar) noexcept {
            static_assert(std::is_arithmetic_v<ScalarT>,
                "ERROR: Scalar must be an arithmetic type.");

            Matrix<T, R, C> result;
            // Tek girişli transform: Her elemanı skaler ile çarp
            std::transform(self.data.begin(), self.data.end(),
                result.data.begin(),
                [scalar](T val) { return static_cast<T>(val * scalar); });
            return result;
        }

        // 3. SKALER BÖLME (Matrix / Scalar)
        template <typename Self, typename ScalarT>
        constexpr auto operator/(this Self&& self, ScalarT scalar) noexcept {
            static_assert(std::is_arithmetic_v<ScalarT>,
                "ERROR: Scalar must be an arithmetic type.");

            Matrix<T, R, C> result;
            // Not: Performans için 1/scalar hesaplayıp çarpmak bazen daha hızlıdır 
            // ama hassasiyet (precision) için doğrudan bölme daha güvenlidir.
            std::transform(self.data.begin(), self.data.end(),
                result.data.begin(),
                [scalar](T val) { return static_cast<T>(val / scalar); });
            return result;
        }

        //3. from MATH to METH 
        // Sınıfın (Matrix) içindeyken:
        template <typename OtherT, size_t OtherR, size_t OtherC>
        constexpr auto operator*(this const Matrix& self, const Matrix<OtherT, OtherR, OtherC>& other) noexcept
        {
            static_assert(C == OtherR, "HATA: Matris çarpımı için sütun(A) == satır(B) olmalı.");

            Matrix<T, R, OtherC> result{};
            for (size_t i = 0; i < R; ++i) {
                for (size_t k = 0; k < C; ++k) {
                    const auto& temp = self[i, k];
                    for (size_t j = 0; j < OtherC; ++j) {
                        result[i, j] += temp * other[k, j];
                    }
                }
            }
            return result;
        }

        static constexpr Matrix identity() noexcept {
            static_assert(R == C, "Birim matris kare olmalidir.");
            Matrix result{};
            for (size_t i = 0; i < R; ++i) {
                result[i, i] = static_cast<T>(1);
            }
            return result;
        }

        // 2. DETERMINANT
        template <typename Self>
        constexpr T determinant(this Self&& self) noexcept {
            static_assert(R == C, "Determinant yalnizca kare matrisler icin hesaplanabilir.");

            if constexpr (R == 1) return self[0, 0];
            else if constexpr (R == 2) {
                return self[0, 0] * self[1, 1] - self[0, 1] * self[1, 0];
            }
            else if constexpr (R == 3) {
                return self[0, 0] * (self[1, 1] * self[2, 2] - self[1, 2] * self[2, 1]) -
                    self[0, 1] * (self[1, 0] * self[2, 2] - self[1, 2] * self[2, 0]) +
                    self[0, 2] * (self[1, 0] * self[2, 1] - self[1, 1] * self[2, 0]);
            }
            else {
                // 4x4 ve uzeri icin genel Gauss Eliminasyonu
                Matrix temp = self;
                T det = 1;
                for (size_t i = 0; i < R; ++i) {
                    size_t pivot = i;
                    for (size_t j = i + 1; j < R; ++j) {
                        if (std::abs(temp[j, i]) > std::abs(temp[pivot, i])) pivot = j;
                    }
                    if (std::abs(temp[pivot, i]) < static_cast<T>(1e-6)) return 0; // Singular

                    if (pivot != i) {
                        for (size_t k = 0; k < R; ++k) std::swap(temp[i, k], temp[pivot, k]);
                        det = -det;
                    }
                    det *= temp[i, i];
                    for (size_t j = i + 1; j < R; ++j) {
                        T factor = temp[j, i] / temp[i, i];
                        for (size_t k = i; k < R; ++k) temp[j, k] -= factor * temp[i, k];
                    }
                }
                return det;
            }
        }

        template <typename Self>
        constexpr Matrix inverse(this Self&& self) {
            static_assert(R == C, "Ters matris yalnizca kare matrisler icin hesaplanabilir.");

            Matrix result = identity();
            Matrix temp = self; // Veriyi bozmamak icin kopya uzerinden islem yapilir

            // Gauss-Jordan Eliminasyonu
            for (size_t i = 0; i < R; ++i) {
                // Pivot secimi
                size_t pivot = i;
                for (size_t j = i + 1; j < R; ++j) {
                    if (std::abs(temp[j, i]) > std::abs(temp[pivot, i])) pivot = j;
                }

                // Matrisin tersi yoksa (Determinant 0 ise) programi guvenli sekilde kirmak veya identity donmek
                if (std::abs(temp[pivot, i]) < static_cast<T>(1e-6)) {
                    // debugta hata veriyor. normal oyunda hiç birşey yapmııyor.
                    assert(false && "Singular matrix has no inverse."); // Debug modunda assert ile hata yakala
                    return identity(); // Oyun motorlarinda cokmemesi icin genelde identity donulur
                }

                // Satirlari degistir (Pivoting)
                if (pivot != i) {
                    for (size_t k = 0; k < R; ++k) {
                        std::swap(temp[i, k], temp[pivot, k]);
                        std::swap(result[i, k], result[pivot, k]);
                    }
                }

                // Pivot satirini normalize et
                T div = temp[i, i];
                for (size_t k = 0; k < R; ++k) {
                    temp[i, k] /= div;
                    result[i, k] /= div;
                }

                // Diger satirlari sifirla
                for (size_t j = 0; j < R; ++j) {
                    if (i != j) {
                        T factor = temp[j, i];
                        for (size_t k = 0; k < R; ++k) {
                            temp[j, k] -= factor * temp[i, k];
                            result[j, k] -= factor * result[i, k];
                        }
                    }
                }
            }
            return result;
        }

    };
    template <typename ScalarT, typename T, size_t R, size_t C>
    constexpr auto operator*(ScalarT scalar, const Matrix<T, R, C>& matrix) noexcept {
        return matrix * scalar; // Zaten tanımladığımız Matrix * Scalar'ı çağırır
    }

}

export namespace Radian::Math {
    using Mat4f = Matrix<float, 4, 4>;
    using Mat3f = Matrix<float, 3, 3>;
    using Mat2f = Matrix<float, 2, 2>;
}