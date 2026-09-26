/** @file tools/precision_touchpad_probe.cpp
 * @brief Explicit local smoke test for the Windows precision touchpad API.
 */
#include "src/precision_touchpad.h"

#include <iostream>

/**

 * @brief Probe device creation; --inject additionally submits one to five contact movements.

 */
int main(int argc, char **argv) {
  precision_touchpad::device device;
  if (!device.create(12000, 7500)) {
    std::cerr << "Create failed: " << GetLastError() << '\n';
    return 1;
  }
  std::cout << "Precision touchpad created\n";
  if (argc < 2 || std::strcmp(argv[1], "--inject") != 0) {
    return 0;
  }
  unsigned sequence = 0;
  unsigned time = 1;
  for (unsigned count = 1; count <= 5; ++count) {
    precision_touchpad::frame frame {};
    frame.command = 1;
    frame.count = count;
    frame.width = 12000;
    frame.height = 7500;
    for (unsigned step = 0; step <= 12; ++step) {
      frame.sequence = ++sequence;
      frame.time = time;
      time += 10;
      for (unsigned i = 0; i < count; ++i) {
        frame.contacts[i] = {27 + i, 2000 + i * 1600 + step * 100, 3000};
      }
      if (!device.submit(frame)) {
        std::cerr << "Injection failed for " << count << " contacts: " << GetLastError() << '\n';
        return 2;
      }
      Sleep(10);
    }
    frame.count = 0;
    frame.sequence = ++sequence;
    frame.time = time;
    time += 20;
    if (!device.submit(frame)) {
      return 3;
    }
    std::cout << count << " contact frames and release accepted\n";
    Sleep(20);
  }
  return 0;
}
