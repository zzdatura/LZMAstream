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

#include "lzma.h"

class LZMAStreamBuf : public std::streambuf
{
public:
    LZMAStreamBuf(std::istream* pIn)
        : m_nBufLen(10000) // free to chose
        , m_pIn(pIn)
        , m_nCalls(0)
        , m_lzmaStream(LZMA_STREAM_INIT)
    {
        m_pCompressedBuf.reset(new char[m_nBufLen]);
        m_pDecompressedBuf.reset(new char[m_nBufLen]);

        // Initially indicate that the buffer is empty
        setg(&m_pDecompressedBuf[0], &m_pDecompressedBuf[1], &m_pDecompressedBuf[1]);

        // try to open the encoder:
        lzma_ret ret = lzma_stream_decoder
               (&m_lzmaStream, std::numeric_limits<uint64_t>::max(), LZMA_CONCATENATED);
        if(ret != LZMA_OK)
            throw std::runtime_error("LZMA decoder could not be opened\n");

        m_lzmaStream.avail_in = 0;        
    }

    virtual ~LZMAStreamBuf()
    {
    }

    virtual int underflow() override final
    {
        lzma_action action = LZMA_RUN;
        lzma_ret ret = LZMA_OK;

        // Do nothing if data is still available (sanity check)
        if(this->gptr() < this->egptr())
            return traits_type::to_int_type(*this->gptr());

        while(true)
        {
            m_lzmaStream.next_out = 
                   reinterpret_cast<unsigned char*>(m_pDecompressedBuf.get());
            m_lzmaStream.avail_out = m_nBufLen;

            if(m_lzmaStream.avail_in == 0)
            {
                // Read from the file, maximum m_nBufLen bytes
                m_pIn->read(&m_pCompressedBuf[0], m_nBufLen);

                // check for possible I/O error
                if(m_pIn->bad())
                    throw std::runtime_error
                     ("LZMAStreamBuf: Error while reading the provided input stream!");

                m_lzmaStream.next_in = 
                     reinterpret_cast<unsigned char*>(m_pCompressedBuf.get());
                m_lzmaStream.avail_in = m_pIn->gcount();
            }

            // check for eof of the compressed file;
            // if yes, forward this information to the LZMA decoder
            if(m_pIn->eof())
                action = LZMA_FINISH;

            // DO the decoding
            ret = lzma_code(&m_lzmaStream, action);

            // check for data
            // NOTE: avail_out gives that amount of data which is available for LZMA to write!
            //         NOT the size of data which has been written for us!
            if(m_lzmaStream.avail_out < m_nBufLen)
            {
                const size_t nDataAvailable = m_nBufLen - m_lzmaStream.avail_out;

                // Let std::streambuf know how much data is available in the buffer now
                setg(&m_pDecompressedBuf[0], &m_pDecompressedBuf[0], 
                                   &m_pDecompressedBuf[0] + nDataAvailable);
                return traits_type::to_int_type(m_pDecompressedBuf[0]);
            }

            if(ret != LZMA_OK)
            {
                if(ret == LZMA_STREAM_END)
                {
                    // This return code is desired if eof of the source file has been reached
                    assert(action == LZMA_FINISH);
                    assert(m_pIn->eof());
                    assert(m_lzmaStream.avail_out == m_nBufLen);
                    return traits_type::eof();
                }

                // an error has occurred while decoding; reset the buffer
                setg(nullptr, nullptr, nullptr);

                // Throwing an exception will set the bad bit of the istream object
                std::stringstream err;
                err << "Error " << ret << " occurred while decoding LZMA file!";
                throw std::runtime_error(err.str().c_str());
            }            
        }
    }

private:
    std::istream* m_pIn;
    std::unique_ptr<char[]> m_pCompressedBuf, m_pDecompressedBuf;
    const size_t m_nBufLen;
    lzma_stream m_lzmaStream;
};
