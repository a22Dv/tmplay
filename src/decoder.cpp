#include <filesystem>

#include "utils.hpp"
#include "decoder.hpp"

namespace trm {

Decoder::Decoder(const std::filesystem::path path) {
    require(std::filesystem::exists(path), Error::DOES_NOT_EXIST);
    
}


} // namespace trm