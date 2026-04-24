// hello
#include <iostream>
#include <fstream>
#include <cmath>

int main() {
    const int sampleRate = 44100;
    const float frequency = 440.0f; // A4
    const int duration = 2; // seconds
    const int numSamples = sampleRate * duration;

    std::ofstream file("sine.wav", std::ios::binary);

    // WAV header (minimal)
    file.write("RIFF", 4);
    int chunkSize = 36 + numSamples * 2;
    file.write((char*)&chunkSize, 4);
    file.write("WAVEfmt ", 8);

    int subchunk1Size = 16;
    short audioFormat = 1;
    short numChannels = 1;
    int byteRate = sampleRate * 2;
    short blockAlign = 2;
    short bitsPerSample = 16;

    file.write((char*)&subchunk1Size, 4);
    file.write((char*)&audioFormat, 2);
    file.write((char*)&numChannels, 2);
    file.write((char*)&sampleRate, 4);
    file.write((char*)&byteRate, 4);
    file.write((char*)&blockAlign, 2);
    file.write((char*)&bitsPerSample, 2);

    file.write("data", 4);
    int dataSize = numSamples * 2;
    file.write((char*)&dataSize, 4);

    for (int i = 0; i < numSamples; i++) {
        float t = (float)i / sampleRate;
        float sample = sin(2 * M_PI * frequency * t);
        short intSample = (short)(sample * 32767);
        file.write((char*)&intSample, 2);
    }

    file.close();
}
