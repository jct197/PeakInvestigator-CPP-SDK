// --------------------------------------------------------------------------
//  PeakInvestigator-API -- C++ library for accessing the public API of
//                              PeakInvestigator.
// --------------------------------------------------------------------------
// Copyright Veritomyx, Inc. 2016.
//
// This software is released under a three-clause BSD license:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of any author or any participating institution
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
// For a full list of authors, refer to the file AUTHORS.
// --------------------------------------------------------------------------
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL ANY OF THE AUTHORS OR THE CONTRIBUTING
// INSTITUTIONS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// --------------------------------------------------------------------------
// $Maintainer: Adam Tenderholt $
// $Author: Adam Tenderholt $
// --------------------------------------------------------------------------
//
//

#include <stdexcept>
#include <iostream>
#include <ctime>

#include <zlib.h>
#include <tarball.h>

#include <spdlog/spdlog.h>

#include "TarFile.h"

#define BUFFER_SIZE 32768
#define TARHEADER_SIZE sizeof(Tar::PosixTarHeader)
#define TARFILE_LOGNAME "TarFile"

using namespace Veritomyx::PeakInvestigator;

typedef Tar::PosixTarHeader TarHeader;

std::shared_ptr<spdlog::logger> TarFile::log_ = spdlog::stdout_color_mt(TARFILE_LOGNAME);

TarFile::TarFile(std::string filename, Mode mode)
{
  switch(mode)
  {
  case LOAD:
    file_ = gzopen(filename.c_str(), "rb");
    break;
  case SAVE:
    file_ = gzopen(filename.c_str(), "wb");
    break;
  default:
    throw std::runtime_error("TarFile mode should be LOAD OR SAVE.");
  }

  if(file_ == NULL)
  {
    throw std::runtime_error("Unable to open tarfile: " + filename);
  }

  filename_ = filename;
  mode_ = mode;
  isOpen_ = true;
}

TarFile::~TarFile()
{
  if(isOpen_)
  {
    close();
  }

}

void TarFile::setDebug(bool debug)
{
  debug_ = debug;
  spdlog::get(TARFILE_LOGNAME)->set_level(debug ? spdlog::level::debug : spdlog::level::info);
}

void TarFile::close()
{
  // close out tar format by writing two NULL header entries
  if(mode_ == SAVE)
  {
    TarHeader header;
    std::size_t size = sizeof(TarHeader);
    std::memset(&header, 0, size);
    gzwrite(file_, &header, size);
    gzwrite(file_, &header, size);
  }

  int retval = gzclose(file_);
  if(retval != Z_OK)
  {
    throw std::runtime_error("Unable to close tarfile: " + filename_);
  }

  isOpen_ = false;
}

void TarFile::writeFile(const std::string& filename, std::istream& contents)
{
  auto log = spdlog::get(TARFILE_LOGNAME);
  log->debug("Writing {}...", filename);

  const std::streampos start = contents.tellg();
  contents.ignore(std::numeric_limits<std::streamsize>::max());
  const std::streampos size = contents.gcount();
  contents.clear();
  contents.seekg(start);
  log->debug(".. State after seeking: {}", contents.rdstate());

  writeHeader_(filename, size);
  char buffer[BUFFER_SIZE];
  unsigned int total = 0;
  while(total < size)
  {
    contents.read(buffer, BUFFER_SIZE);
    log->debug(".. State after read: {}", contents.rdstate());
    if(contents.fail() && !contents.eof())
    {
      log->error(".... unable to read from istream buffer ({})", contents.rdstate());
      break;
    }

    const int read = contents.gcount();

    int written = gzwrite(file_, &buffer, read);
    if(read != written)
    {
      throw std::runtime_error("Problem writing data for " + filename);
    }
    total += written;

    log->debug("...... {} of {} bytes written.", total, size);
  }

  if (total != size)
  {
    throw std::runtime_error("Number of total bytes written != size.");
  }

  // fill remaining 512-byte block with NULL
  while(total % TARHEADER_SIZE != 0)
  {
    gzputc(file_, 0);
    total++;
  }

  log->debug("...Done!");
}

std::string TarFile::readNextFile(std::ostream& contents)
{
  TarHeader header;
  unsigned int read = gzread(file_, &header, TARHEADER_SIZE);
  if(read == 0)
  {
    return "";
  }
  else if(read != TARHEADER_SIZE)
  {
    throw std::runtime_error("Problem reading a tar header of " + filename_);
  }
  else if (header.name[0] == '\0')
  {
	  return "";
  }

  long long size;
  std::sscanf(header.size, "%011llo", &size);

  if (size == 0)
  {
    return header.name;
  }

  char buffer[BUFFER_SIZE];

  // gzread doesn't necessarily stop reading
  while(size > BUFFER_SIZE)
  {
    read = gzread(file_, buffer, BUFFER_SIZE);
    contents.write(buffer, read);
    size -= read;
  }

  read = gzread(file_, buffer, size);
  contents.write(buffer, read);

  // read rest of entry (NULLs)
  unsigned int bytes_over = size % TARHEADER_SIZE;
  if (bytes_over > 0)
  {
    gzread(file_, buffer, TARHEADER_SIZE - bytes_over);
  }

  return header.name;
}

void TarFile::writeHeader_(const std::string& filename, unsigned long long size)
{
  TarHeader header;

  // initialize header
  std::memset(&header, 0, TARHEADER_SIZE);

  // set archive name
  std::sprintf(header.name, "%s", filename.c_str());

  // set tar format
  std::sprintf(header.magic, "ustar");
  std::memcpy(header.version, "  ", sizeof(header.version));

  // set modification time, mode, and filetype
  std::sprintf(header.mtime, "%011lo", std::time(NULL));
  std::sprintf(header.mode, "%07o", 0644);
  header.typeflag[0] = 0;

  // set size of file
  std::sprintf(header.size, "%011llo", size);

  // set header checksum
  std::sprintf(header.checksum, "%06o", Tar::headerChecksum(&header));


  // now write archive header (required before writing data)
  int written = gzwrite(file_, &header, TARHEADER_SIZE);
  if(written != TARHEADER_SIZE)
  {
    throw std::runtime_error("Problem writing tarball header for: " + filename);
  }

}
