// gtav launcher - readers for the player's own files: an Xbox 360 disc image (XDVDFS, plain or
// XGD2/XGD3 partition), an extracted disc folder, and STFS packages (disc-1 install partitions,
// the title update). Everything here is our own code, written from the public format notes in
// scripts/xiso.py and scripts/stfs_extract.py.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace loader {

// Random-access bytes: a file on disk, or a contiguous file inside a disc image.
class Blob {
 public:
  virtual ~Blob() = default;
  virtual bool ReadAt(uint64_t offset, void* dst, size_t size) = 0;
  virtual uint64_t size() const = 0;
};

std::unique_ptr<Blob> OpenFileBlob(const std::wstring& path, std::wstring* error);

// One file inside a disc or package. path uses '/' separators.
struct Entry {
  std::string path;
  uint64_t size = 0;
  uint64_t offset = 0;  // reader specific
  uint32_t start = 0;   // reader specific
  uint32_t blocks = 0;  // reader specific
  bool contiguous = true;
};

// Receives each chunk of a file being read, in order. Return false to stop.
using ChunkSink = std::function<bool(const uint8_t* data, size_t size)>;

// A read-only tree of files: a disc image, a disc folder or an STFS package.
class Source {
 public:
  virtual ~Source() = default;
  const std::vector<Entry>& entries() const { return entries_; }
  // Streams one entry to sink. false on a read error or when sink returned false.
  virtual bool Read(const Entry& e, const ChunkSink& sink, std::wstring* error) = 0;
  // Random access to one entry (discs and folders only; nullptr for STFS packages).
  virtual std::unique_ptr<Blob> OpenEntry(const Entry& e, std::wstring* error) = 0;
  // Case-insensitive lookup by '/' path.
  const Entry* Find(const std::string& path) const;
  virtual std::wstring Describe() const = 0;

 protected:
  std::vector<Entry> entries_;
};

// Opens an ISO (XDVDFS) or an extracted disc folder.
std::unique_ptr<Source> OpenDisc(const std::wstring& path, std::wstring* error);
// Opens an STFS package (CON / LIVE / PIRS).
std::unique_ptr<Source> OpenStfs(std::unique_ptr<Blob> blob, std::wstring* error);
// True when the file starts like an STFS package.
bool LooksLikeStfs(Blob& blob);

std::wstring Widen(const std::string& s);
std::string Narrow(const std::wstring& s);

}  // namespace loader
