#include "p0s/minimal_zarr_writer.h"

#include <iostream>

int main(int argc, char** argv) {
  return p0s::RunStoreGeneratorCli(argc, argv, std::cout, std::cerr);
}
