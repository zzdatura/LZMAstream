// Copyright (c) 2019 Philipp Sch
// https://www.codeproject.com
//        /Tips/5098831/Reading-Compressed-LZMA-Files-on-the-fly-using-a-C
//
// This library is free software; you can redistribute it and/or modify it
// under the terms of the Code Project Open License (CPOL); either version
// 1.0 of the License, or (at your option) any later version.
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
//***************************************************************************
//
// This is a derived work.
//
// I first came across the CodeProject article while looking for a way to
// directly read/write compressed files from C++.  The article taught me how
// to create a custom streambuf using LZMA but it was only for reading a
// compressed file.  I expanded it for output as well, while also heavily
// modifying everything else for my needs.  Therefore only (part of) this
// file actually comes from CodeProject.
//
//***************************************************************************

#include "LZMAstream.hpp"
#include <limits>

template<typename T>
bool const is_ifstream = std::is_same_v<T, std::ifstream>;
template<typename T>
bool const is_ofstream = std::is_same_v<T, std::ofstream>;

std::string const err_base  {"LZMAstreambuf: Error while "};
std::string const err_open  {err_base + "opening file."};
std::string const err_read  {err_base + "reading input file."};
std::string const err_write {err_base + "writting output file."};
std::string const err_coder {err_base + "setting up coder."};
std::string const err_dec   {err_base + "decoding data. Code: "};
std::string const err_enc   {err_base + "encoding data. Code: "};

template<typename T>
LZMAstreambuf<T>::LZMAstreambuf(T &&iofile, size_t buffer_size)
    : buflen{buffer_size}
    , file{move(iofile)}
    , ibuffer{new char[buflen]}
    , obuffer{new char[buflen]}
    , action{LZMA_RUN}
    , lzma(LZMA_STREAM_INIT) {

  if (!file.is_open())
    throw std::runtime_error(err_open);

  lzma_ret status{[this]() {
    if constexpr (is_ifstream<T>) {
      // set empty buffer
      setg(&obuffer[0], &obuffer[1], &obuffer[1]);

      // try to open the decoder
      uint64_t const maxint = std::numeric_limits<uint64_t>::max();
      return lzma_stream_decoder(&lzma, maxint, LZMA_CONCATENATED);
    }
    else { // if constexpr (is_ofstream<T>)
      // set empty buffer
      setp(&ibuffer[0], &ibuffer[buflen]);

      // multi threaded mode
      uint32_t threads = std::max(1u, lzma_cputhreads());
      if (threads > 1) {
        lzma_mt mt {
          .flags      = 0,
          .threads    = threads,
          .block_size = 0,
          .preset     = 9u,
          .filters    = NULL,
          .check      = LZMA_CHECK_CRC64
        };
        return lzma_stream_encoder_mt(&lzma, &mt);
      }

      // single threaded mode
      return lzma_easy_encoder(&lzma, 9u, LZMA_CHECK_CRC64);
    }
  }()};

  if (status != LZMA_OK)
    throw std::runtime_error(err_coder);

  lzma.avail_in = 0;
}

template<typename T>
LZMAstreambuf<T>::~LZMAstreambuf() {
  if constexpr (is_ofstream<T>) {
    action = LZMA_FINISH;
    sync();
  }
  lzma_end(&lzma);
}

template<typename T>
int LZMAstreambuf<T>::overflow(int c) {
  if constexpr (is_ofstream<T>) {
    if (sync() == 0 && c != traits_type::eof())
      sputc(c);
    else
      return traits_type::eof();
    return traits_type::to_int_type(c);
  }
  else return std::streambuf::overflow(c);
}

template<typename T>
int LZMAstreambuf<T>::underflow() {
  if constexpr (is_ifstream<T>) {
    lzma_ret status{LZMA_OK};

    // Do nothing if data is still available (sanity check)
    if (gptr() < egptr())
      return traits_type::to_int_type(*gptr());

    while (true) {
      lzma.next_out = reinterpret_cast<unsigned char*>(obuffer.get());
      lzma.avail_out = buflen;

      if (lzma.avail_in == 0) {
        // read from the file, maximum buflen bytes
        file.read(ibuffer.get(), buflen);

        // check for possible I/O error
        if (file.bad())
          throw std::runtime_error(err_read);

        lzma.next_in = reinterpret_cast<unsigned char*>(ibuffer.get());
        lzma.avail_in = file.gcount();
      }

      // check end of the compressed file and decode
      if (file.eof())
        action = LZMA_FINISH;
      status = lzma_code(&lzma, action);

      // check for data
      // NOTE: avail_out gives the amount of data which is available for LZMA
      //       to write, NOT the size of data which has been written for us!
      if (lzma.avail_out < buflen) {
        // let streambuf know how much data is available in the buffer now
        setg(&obuffer[0], &obuffer[0], &obuffer[buflen - lzma.avail_out]);
        return traits_type::to_int_type(obuffer[0]);
      }

      if (status != LZMA_OK) {
        // if end of source, it's okay
        if (status == LZMA_STREAM_END)
          return traits_type::eof();

        // an error has occurred while decoding; reset the buffer
        setg(nullptr, nullptr, nullptr);
        // throwing an exception will set the bad bit of the istream object
        throw std::runtime_error(err_dec + std::to_string(status));
      }
    }
  }
  else return std::streambuf::underflow();
}

#include <iostream>
template<typename T>
int LZMAstreambuf<T>::sync() {
  if constexpr (is_ofstream<T>) {
    if (pptr() != pbase() || action == LZMA_FINISH) {
      // setup encoding
      lzma.next_in = reinterpret_cast<unsigned char*>(pbase());
      lzma.avail_in = pptr() - pbase();

      do {
        lzma.next_out = reinterpret_cast<unsigned char*>(obuffer.get());
        lzma.avail_out = buflen;

        // encode data
        lzma_ret status{lzma_code(&lzma, action)};

        // check errors
        if (status != LZMA_OK && status != LZMA_STREAM_END) {
          // an error has occurred while decoding; reset the buffer
          setp(nullptr, nullptr);
          return 1;
        }

        // write data to file
        if (lzma.avail_out < buflen) {
          file.write(obuffer.get(), buflen - lzma.avail_out);
          if (file.bad())
            return 1;
        }
      } while (lzma.avail_out == 0);
      setp(&ibuffer[0], &ibuffer[buflen]);
    }
    return 0;
  }
  else return std::streambuf::sync();
}

template<typename stream>
LZMAstream<stream>::LZMAstream(std::string_view filename)
  : stream{&streambuf}
  , streambuf{fstreamT{filename.data(), std::ios::binary}} {
}

template class LZMAstream<std::istream>;
template class LZMAstream<std::ostream>;
