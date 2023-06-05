// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "binary_data_utils.h"

#include <openssl/evp.h>
#include <openssl/md5.h>
#include <sys/stat.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>  

namespace {

// Number of hex digits in a byte.
const int kNumHexDigitsInByte = 2;

}  // namespace

namespace quipper {

static uint64_t Md5Prefix(const unsigned char* data,
                          unsigned long length) {  
  uint64_t digest_prefix = 0;
  unsigned char digest[MD5_DIGEST_LENGTH + 1];

  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  EVP_DigestInit_ex(ctx, EVP_md5(), NULL);
  EVP_DigestUpdate(ctx, data, length);
  EVP_DigestFinal(ctx, digest, NULL);
  EVP_MD_CTX_free(ctx);
  // We need 64-bits / # of bits in a byte.
  for (size_t i = 0; i < sizeof(uint64_t) / sizeof(unsigned char); i++) {
    digest_prefix = digest_prefix << (sizeof(unsigned char) * 8) | digest[i];
  }
  return digest_prefix;
}

uint64_t Md5Prefix(const std::string& input) {
  auto data = reinterpret_cast<const unsigned char*>(input.data());
  return Md5Prefix(data, input.size());
}

uint64_t Md5Prefix(const std::vector<char>& input) {
  auto data = reinterpret_cast<const unsigned char*>(input.data());
  return Md5Prefix(data, input.size());
}

std::string RawDataToHexString(const u8* array, size_t length) {
  // Convert the bytes to hex digits one at a time.
  // There will be kNumHexDigitsInByte hex digits, and 1 char for NUL.
  char buffer[kNumHexDigitsInByte + 1];
  std::string result = "";
  for (size_t i = 0; i < length; ++i) {
    snprintf(buffer, sizeof(buffer), "%02x", array[i]);
    result += buffer;
  }
  return result;
}

std::string RawDataToHexString(const std::string& str) {
  return RawDataToHexString(reinterpret_cast<const u8*>(str.data()),
                            str.size());
}

bool HexStringToRawData(const std::string& str, u8* array, size_t length) {
  const int kHexRadix = 16;
  char* err;
  // Loop through kNumHexDigitsInByte characters at a time (to get one byte)
  // Stop when there are no more characters, or the array has been filled.
  for (size_t i = 0; (i + 1) * kNumHexDigitsInByte <= str.size() && i < length;
       ++i) {
    std::string one_byte =
        str.substr(i * kNumHexDigitsInByte, kNumHexDigitsInByte);
    array[i] = strtol(one_byte.c_str(), &err, kHexRadix);
    if (*err) return false;
  }
  return true;
}

}  // namespace quipper
