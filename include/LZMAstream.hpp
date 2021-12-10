#ifndef LZMASTREAM_HPP
#define LZMASTREAM_HPP

#include <fstream>
#include <memory>
#include <streambuf>

#include <lzma.h>

template<typename fstreamT>
class LZMAstreambuf : public std::streambuf {
 public:
  LZMAstreambuf(fstreamT &&iofile, size_t buffer_size=10000);
  virtual ~LZMAstreambuf();

  virtual int sync      () override final;
  virtual int underflow () override final;
  virtual int overflow  (int c=traits_type::eof()) override final;

 private:
  size_t const            buflen;
  fstreamT                file;
  std::unique_ptr<char[]> ibuffer;
  std::unique_ptr<char[]> obuffer;
  lzma_action             action;
  lzma_stream             lzma;
};

template<typename stream>
class LZMAstream : public stream {
 public:
  LZMAstream(std::string_view filename);
  virtual ~LZMAstream() {}

 private:
  template<bool is_input>
  using streamT = std::conditional_t<is_input, std::ifstream, std::ofstream>;
  using fstreamT = streamT<std::is_same_v<stream, std::istream>>;
  LZMAstreambuf<fstreamT> streambuf;
};

using iLZMAstream = LZMAstream<std::istream>;
using oLZMAstream = LZMAstream<std::ostream>;
#endif
