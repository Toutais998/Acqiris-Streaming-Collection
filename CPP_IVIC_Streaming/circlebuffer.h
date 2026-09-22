#pragma once

#include <mutex>
#include <vector>


class circlebuffer {
private:
  size_t read;
  size_t write;
  size_t size;
  std::vector<int16_t> buffer;
  std::mutex mtx;

public:
  circlebuffer(size_t s);
  ~circlebuffer();

  size_t getSize() const { return size; }
  size_t availableElements() { return (write - read) / 1000000; }
  const std::vector<int16_t> &getBuffer() { return buffer; }

  bool isEmpty() const;
  bool isFull() const;
  void push(int16_t *num, size_t arraySize);
  void pushn(int16_t *inputArray, size_t arraySize);
  size_t popn(int16_t *output, size_t arraySize);
  void pushn_0(int16_t *inputArray, size_t arraySize);
  size_t popn_0(int16_t *output, size_t arraySize);
  int16_t pop();
  void unpop(size_t n) {
      if (n > size) return;
      for (size_t i = 0; i < n; ++i) {
          if (read == 0)
              read = size - 1;
          else
              --read;
      }
  }
};
