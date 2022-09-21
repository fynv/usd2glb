// Simple image writer
// supported file format: PNG(use fpng), JPEG(use stb_image), OpenEXR(use
// tinyexr), TIFF(use tinydng)
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "nonstd/expected.hpp"
#include "tinyusdz.hh"

namespace tinyusdz {
namespace image {

enum class WriteImageFormat { Autodetect, PNG, JPEG, EXR, TIFF };

struct WriteOption {
  WriteImageFormat format{WriteImageFormat::Autodetect};
  bool half{false};  // Use half float for EXR
};

///
/// @param[in] filename Output filename
/// @param[in] image Image data
/// @param[in] option Image write option(optional)
///
/// @return true upon success. or error message(std::string) when failed.
///
nonstd::expected<bool, std::string> WriteImageToFile(
    const std::string &filename, const Image &image,
    WriteOption option = WriteOption());

///
/// @param[in] image Image data
/// @param[in] option Image write option(optional)
/// @return Serialized image data
///
nonstd::expected<std::vector<uint8_t>, std::string> WriteImageToMemory(
    const Image &image, const WriteOption option = WriteOption());

}  // namespace image
}  // namespace tinyusdz
