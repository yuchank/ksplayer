extern "C" {
  #include <libavdevice/avdevice.h>
}

#include <iostream>

int main(int argc, char *argv[]) {

  if (argc < 2) {
    std::cout << "please provide a movie file" << std::endl;
  }

  avdevice_register_all();

  return 0;
}