#pragma once
#ifndef FFT_H
#define FFT_H

#include <complex>

// declarations
const double FFT_PI = std::atan(1.0) * 4;
void fft(double *x_in, std::complex<double> *x_out, int nBufSize);
void fft_rec(std::complex<double> *x, int nBufSize);
void fft_magnitude(double *in, double *out, const int nBufSize);
void fft_magnitude_db(double *in, double *out, int nBufSize);

// implementation
void fft(double *x_in, std::complex<double> *x_out, int nBufSize)
{
    for (int i = 0; i < nBufSize; i++)
    {
        x_out[i] = std::complex<double>(x_in[i], 0);
        x_out[i] *= 1;  // window
    }
    fft_rec(x_out, nBufSize);
}

void fft_rec(std::complex<double> *x, int nBufSize)
{
    if (nBufSize <= 1) return;

    // fft
    std::complex<double> *odd = new std::complex<double>[nBufSize / 2];
    std::complex<double> *even = new std::complex<double>[nBufSize / 2];
    for (int i = 0; i < nBufSize / 2; i++)
    {
        even[i] = x[i * 2];
        odd[i] = x[i * 2 + 1];
    }
    fft_rec(even, nBufSize / 2);
    fft_rec(odd, nBufSize / 2);

    // dft
    for (int k = 0; k < nBufSize / 2; k++)
    {
        std::complex<double> t = odd[k] * exp(std::complex<double>(0, -2 * FFT_PI * k / nBufSize));
        x[k] = even[k] + t;
        x[nBufSize / 2 + k] = even[k] - t;
    }

    // cleanup
    delete[] odd;
    delete[] even;
}

void fft_magnitude(double *in, double *out, const int nBufSize)
{
    std::complex<double> *c = new std::complex<double>[nBufSize];
    if (c == nullptr) return;
    fft(in, c, nBufSize);
    for (int i = 0; i < nBufSize / 2; i++)
        out[i] = sqrt(c[i].real() * c[i].real() + c[i].imag() * c[i].imag());
    delete[] c;
}

void fft_magnitude_db(double *in, double *out, int nBufSize)
{
    std::complex<double> *c = new std::complex<double>[nBufSize];
    if (c == nullptr) return;
    fft(in, c, nBufSize);
    for (int i = 0; i < nBufSize / 2; i++)
        out[i] = 10 * log10(c[i].real() * c[i].real() + c[i].imag() * c[i].imag());
    delete[] c;
}

#endif /* ifndef FFT_H */