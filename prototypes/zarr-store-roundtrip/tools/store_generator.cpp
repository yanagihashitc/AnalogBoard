#include "p0s/minimal_zarr_writer.h"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char** argv) {
  if (argc != 3 && argc != 4) {
    std::cerr << "usage: p0s_store_generator <accepted-kat> <output-root> "
                 "[--open]\n";
    return 2;
  }
  const bool finalize = argc == 3;
  if (!finalize && std::string(argv[3]) != "--open") {
    std::cerr << "store_generator accepts only the optional --open flag\n";
    return 2;
  }

  std::exception_ptr failure;
  std::thread writer([&failure, &argv, finalize] {
    try {
      p0s::GenerateSyntheticStore(
          {std::filesystem::path(argv[2]), std::filesystem::path(argv[1]),
           finalize});
    } catch (...) {
      failure = std::current_exception();
    }
  });
  writer.join();

  if (failure != nullptr) {
    try {
      std::rethrow_exception(failure);
    } catch (const std::exception& error) {
      std::cerr << "store generation failed: " << error.what() << '\n';
      return 1;
    }
  }
  std::cout << "synthetic_store_status="
            << (finalize ? "finalized" : "open") << " status=pass\n";
  return 0;
}
