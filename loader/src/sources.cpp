// gtav launcher - disc image, disc folder and STFS package readers (see sources.h).
#include "sources.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#endif

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

namespace loader {

#ifdef _WIN32
std::wstring Widen(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
  return w;
}

std::string Narrow(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}
#else
// wchar_t is UTF-32 here. Invalid bytes become U+FFFD.
std::wstring Widen(const std::string& s) {
  std::wstring w;
  for (size_t i = 0; i < s.size();) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
    if (!len || i + len > s.size()) {
      w += L'\uFFFD';
      ++i;
      continue;
    }
    uint32_t cp = len == 1 ? c : c & (0x7F >> len);
    for (int k = 1; k < len; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    w += wchar_t(cp);
    i += len;
  }
  return w;
}

std::string Narrow(const std::wstring& w) {
  std::string s;
  for (wchar_t wc : w) {
    uint32_t cp = uint32_t(wc);
    if (cp < 0x80) {
      s += char(cp);
    } else if (cp < 0x800) {
      s += char(0xC0 | (cp >> 6));
      s += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      s += char(0xE0 | (cp >> 12));
      s += char(0x80 | ((cp >> 6) & 0x3F));
      s += char(0x80 | (cp & 0x3F));
    } else {
      s += char(0xF0 | (cp >> 18));
      s += char(0x80 | ((cp >> 12) & 0x3F));
      s += char(0x80 | ((cp >> 6) & 0x3F));
      s += char(0x80 | (cp & 0x3F));
    }
  }
  return s;
}
#endif

namespace {

std::string Lower(std::string s) {
  for (char& c : s) c = char(tolower(static_cast<unsigned char>(c)));
  return s;
}

#ifdef _WIN32
std::wstring LastErrorText(const wchar_t* what, const std::wstring& path) {
  wchar_t buf[512] = {};
  FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, GetLastError(),
                 0, buf, 512, nullptr);
  return std::wstring(what) + L" " + path + L": " + buf;
}

class FileBlob : public Blob {
 public:
  FileBlob(HANDLE h, uint64_t size) : h_(h), size_(size) {}
  ~FileBlob() override { CloseHandle(h_); }
  bool ReadAt(uint64_t offset, void* dst, size_t size) override {
    uint8_t* out = static_cast<uint8_t*>(dst);
    while (size) {
      OVERLAPPED ov = {};
      ov.Offset = DWORD(offset);
      ov.OffsetHigh = DWORD(offset >> 32);
      DWORD want = DWORD(std::min<size_t>(size, 64u << 20)), got = 0;
      if (!ReadFile(h_, out, want, &got, &ov) || got == 0) return false;
      out += got;
      offset += got;
      size -= got;
    }
    return true;
  }
  uint64_t size() const override { return size_; }

 private:
  HANDLE h_;
  uint64_t size_;
};
#else
std::wstring LastErrorText(const wchar_t* what, const std::wstring& path) {
  return std::wstring(what) + L" " + path + L": " + Widen(strerror(errno));
}

class FileBlob : public Blob {
 public:
  FileBlob(int fd, uint64_t size) : fd_(fd), size_(size) {}
  ~FileBlob() override { close(fd_); }
  bool ReadAt(uint64_t offset, void* dst, size_t size) override {
    uint8_t* out = static_cast<uint8_t*>(dst);
    while (size) {
      ssize_t got = pread(fd_, out, std::min<size_t>(size, 64u << 20), off_t(offset));
      if (got < 0 && errno == EINTR) continue;
      if (got <= 0) return false;
      out += got;
      offset += uint64_t(got);
      size -= size_t(got);
    }
    return true;
  }
  uint64_t size() const override { return size_; }

 private:
  int fd_;
  uint64_t size_;
};
#endif

// A window [base, base + size) of another blob.
class SliceBlob : public Blob {
 public:
  SliceBlob(Blob* parent, uint64_t base, uint64_t size) : parent_(parent), base_(base), size_(size) {}
  bool ReadAt(uint64_t offset, void* dst, size_t size) override {
    if (offset + size > size_) return false;
    return parent_->ReadAt(base_ + offset, dst, size);
  }
  uint64_t size() const override { return size_; }

 private:
  Blob* parent_;
  uint64_t base_, size_;
};

bool StreamBlob(Blob& blob, uint64_t offset, uint64_t size, const ChunkSink& sink) {
  std::vector<uint8_t> buf(8u << 20);
  while (size) {
    size_t n = size_t(std::min<uint64_t>(size, buf.size()));
    if (!blob.ReadAt(offset, buf.data(), n)) return false;
    if (!sink(buf.data(), n)) return false;
    offset += n;
    size -= n;
  }
  return true;
}

//-----------------------------------------------------------------------------
// XDVDFS disc image
//-----------------------------------------------------------------------------

constexpr uint64_t kSector = 2048;
constexpr char kXdvdfsMagic[] = "MICROSOFT*XBOX*MEDIA";
// Game partition offsets: plain XISO, XGD3, XGD1, XGD2.
constexpr uint64_t kPartitions[] = {0, 0x2080000, 0xFD90000, 0x18300000};

class DiscImage : public Source {
 public:
  explicit DiscImage(std::unique_ptr<Blob> blob, std::wstring path)
      : blob_(std::move(blob)), path_(std::move(path)) {}

  bool Load(std::wstring* error) {
    char magic[20];
    bool found = false;
    for (uint64_t base : kPartitions) {
      if (base + 33 * kSector > blob_->size()) continue;
      if (blob_->ReadAt(base + 32 * kSector, magic, 20) && !memcmp(magic, kXdvdfsMagic, 20)) {
        base_ = base;
        found = true;
        break;
      }
    }
    if (!found) {
      *error = path_ + L" is not an Xbox 360 disc image (no XDVDFS volume found).";
      return false;
    }
    uint32_t root[2];
    if (!blob_->ReadAt(base_ + 32 * kSector + 20, root, 8)) {
      *error = L"Could not read " + path_;
      return false;
    }
    if (!Walk(root[0], root[1], "", 0)) {
      *error = path_ + L": the disc's directory tables are damaged.";
      return false;
    }
    std::sort(entries_.begin(), entries_.end(),
              [](const Entry& a, const Entry& b) { return a.path < b.path; });
    return true;
  }

  bool Read(const Entry& e, const ChunkSink& sink, std::wstring* error) override {
    if (!StreamBlob(*blob_, e.offset, e.size, sink)) {
      *error = L"Read error in " + path_ + L" (" + Widen(e.path) + L")";
      return false;
    }
    return true;
  }

  std::unique_ptr<Blob> OpenEntry(const Entry& e, std::wstring*) override {
    return std::make_unique<SliceBlob>(blob_.get(), e.offset, e.size);
  }

  std::wstring Describe() const override { return path_; }

 private:
  bool Walk(uint32_t sector, uint32_t size, const std::string& prefix, int depth) {
    if (size == 0) return true;
    if (depth > 32 || size > (64u << 20)) return false;
    std::vector<uint8_t> table(size);
    if (!blob_->ReadAt(base_ + sector * kSector, table.data(), size)) return false;
    std::vector<uint32_t> stack = {0};
    std::set<uint32_t> seen;
    while (!stack.empty()) {
      uint32_t off = stack.back();
      stack.pop_back();
      if (seen.count(off) || off + 14 > size) continue;
      seen.insert(off);
      uint16_t left, right;
      uint32_t start, length;
      memcpy(&left, &table[off], 2);
      memcpy(&right, &table[off + 2], 2);
      memcpy(&start, &table[off + 4], 4);
      memcpy(&length, &table[off + 8], 4);
      uint8_t attr = table[off + 12], nlen = table[off + 13];
      if (left == 0xFFFF && right == 0xFFFF) continue;
      if (off + 14 + nlen > size) return false;
      std::string name(reinterpret_cast<const char*>(&table[off + 14]), nlen);
      std::string path = prefix + name;
      if (attr & 0x10) {
        if (!Walk(start, length, path + "/", depth + 1)) return false;
      } else {
        Entry e;
        e.path = path;
        e.size = length;
        e.offset = base_ + uint64_t(start) * kSector;
        if (e.offset + e.size > blob_->size()) return false;
        entries_.push_back(e);
      }
      if (left) stack.push_back(uint32_t(left) * 4);
      if (right) stack.push_back(uint32_t(right) * 4);
    }
    return true;
  }

  std::unique_ptr<Blob> blob_;
  std::wstring path_;
  uint64_t base_ = 0;
};

//-----------------------------------------------------------------------------
// Extracted disc folder
//-----------------------------------------------------------------------------

class DiscFolder : public Source {
 public:
  explicit DiscFolder(fs::path root) : root_(std::move(root)) {}

  bool Load(std::wstring* error) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root_, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (!it->is_regular_file(ec)) continue;
      Entry e;
      e.path = Narrow(fs::relative(it->path(), root_, ec).generic_wstring());
      e.size = it->file_size(ec);
      entries_.push_back(e);
    }
    if (ec) {
      *error = L"Could not list " + root_.wstring() + L": " + Widen(ec.message());
      return false;
    }
    std::sort(entries_.begin(), entries_.end(),
              [](const Entry& a, const Entry& b) { return a.path < b.path; });
    return true;
  }

  bool Read(const Entry& e, const ChunkSink& sink, std::wstring* error) override {
    auto blob = OpenEntry(e, error);
    if (!blob) return false;
    if (!StreamBlob(*blob, 0, e.size, sink)) {
      *error = L"Read error in " + (root_ / Widen(e.path)).wstring();
      return false;
    }
    return true;
  }

  std::unique_ptr<Blob> OpenEntry(const Entry& e, std::wstring* error) override {
    return OpenFileBlob((root_ / Widen(e.path)).wstring(), error);
  }

  std::wstring Describe() const override { return root_.wstring(); }

 private:
  fs::path root_;
};

//-----------------------------------------------------------------------------
// STFS package
//-----------------------------------------------------------------------------

constexpr uint64_t kBlock = 0x1000;
constexpr uint32_t kPerHash = 170;  // data blocks covered by one hash table

uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
uint32_t Le24(const uint8_t* p) { return uint32_t(p[0]) | p[1] << 8 | p[2] << 16; }
uint32_t Be24(const uint8_t* p) { return uint32_t(p[0]) << 16 | p[1] << 8 | p[2]; }

class Stfs : public Source {
 public:
  explicit Stfs(std::unique_ptr<Blob> blob) : blob_(std::move(blob)) {}

  bool Load(std::wstring* error) {
    uint8_t h[0x971A];
    if (blob_->size() < sizeof(h) || !blob_->ReadAt(0, h, sizeof(h))) {
      *error = L"The file is too small to be an Xbox 360 package.";
      return false;
    }
    if (memcmp(h, "CON ", 4) && memcmp(h, "LIVE", 4) && memcmp(h, "PIRS", 4)) {
      *error = L"The file is not an Xbox 360 package (STFS).";
      return false;
    }
    uint32_t header_size = Be32(h + 0x340);
    const uint8_t* vd = h + 0x379;
    uint8_t flags = vd[2];
    uint32_t ft_blocks = uint32_t(vd[3]) | uint32_t(vd[4]) << 8;
    uint32_t ft_start = Le24(vd + 5);
    base_ = (uint64_t(header_size) + 0xFFF) & ~uint64_t(0xFFF);
    // Read-only (signed) packages keep one hash table per level, others two.
    tables_ = (flags & 1) ? 1 : 2;

    std::vector<uint8_t> raw;
    uint32_t b = ft_start;
    for (uint32_t i = 0; i < ft_blocks; ++i) {
      size_t at = raw.size();
      raw.resize(at + kBlock);
      if (!blob_->ReadAt(Offset(b), &raw[at], kBlock)) {
        *error = L"Read error in the package's file table.";
        return false;
      }
      if (!NextBlock(b, &b)) {
        *error = L"Read error in the package's hash tables.";
        return false;
      }
    }
    struct Raw {
      std::string name;
      int16_t parent;
      bool dir;
    };
    std::vector<Raw> all;
    for (size_t i = 0; i + 0x40 <= raw.size(); i += 0x40) {
      const uint8_t* e = &raw[i];
      uint8_t nl = e[0x28] & 0x3F;
      if (nl == 0) break;
      Raw r{std::string(reinterpret_cast<const char*>(e), nl),
            int16_t(uint16_t(e[0x32]) << 8 | e[0x33]), bool(e[0x28] & 0x80)};
      all.push_back(r);
      if (r.dir) continue;
      Entry en;
      en.contiguous = e[0x28] & 0x40;
      en.blocks = Le24(e + 0x29);
      en.start = Le24(e + 0x2F);
      en.size = Be32(e + 0x34);
      en.offset = all.size() - 1;  // index, resolved to a path below
      entries_.push_back(en);
    }
    for (Entry& en : entries_) {
      std::string path;
      int idx = int(en.offset);
      for (int guard = 0; idx >= 0 && idx < int(all.size()) && guard < 64; ++guard) {
        path = path.empty() ? all[idx].name : all[idx].name + "/" + path;
        idx = all[idx].parent;
      }
      en.path = path;
    }
    return true;
  }

  bool Read(const Entry& e, const ChunkSink& sink, std::wstring* error) override {
    uint64_t remaining = e.size;
    uint32_t b = e.start;
    std::vector<uint8_t> buf(kPerHash * kBlock);
    uint32_t left = e.blocks;
    while (remaining && left) {
      // Within one group of 170 data blocks a contiguous file is contiguous on disk too.
      uint32_t run = 1;
      if (e.contiguous) {
        run = std::min<uint32_t>(kPerHash - b % kPerHash, left);
      }
      size_t bytes = size_t(std::min<uint64_t>(remaining, uint64_t(run) * kBlock));
      if (!blob_->ReadAt(Offset(b), buf.data(), bytes) || !sink(buf.data(), bytes)) {
        *error = L"Read error in package file " + Widen(e.path);
        return false;
      }
      remaining -= bytes;
      left -= run;
      if (e.contiguous) {
        b += run;
      } else if (remaining && !NextBlock(b, &b)) {
        *error = L"Read error in the package's hash tables.";
        return false;
      }
    }
    if (remaining) {
      *error = L"Package file " + Widen(e.path) + L" is truncated.";
      return false;
    }
    return true;
  }

  std::unique_ptr<Blob> OpenEntry(const Entry&, std::wstring* error) override {
    *error = L"random access into STFS packages is not supported";
    return nullptr;
  }

  std::wstring Describe() const override { return L"package"; }

 private:
  uint64_t BackingBlock(uint64_t index) const {
    uint64_t block = index, base = kPerHash;
    for (int level = 0; level < 3; ++level) {
      block += ((index + base) / base) * tables_;
      if (index < base) break;
      base *= kPerHash;
    }
    return block;
  }

  uint64_t Offset(uint64_t index) const { return base_ + BackingBlock(index) * kBlock; }

  bool NextBlock(uint32_t index, uint32_t* next) {
    // The level-0 hash table sits right before its group of 170 data blocks.
    uint64_t group_first = index - index % kPerHash;
    uint64_t table = BackingBlock(group_first) - tables_;
    uint8_t entry[0x18];
    if (!blob_->ReadAt(base_ + table * kBlock + (index % kPerHash) * 0x18, entry, sizeof(entry)))
      return false;
    *next = Be24(entry + 0x15);
    return true;
  }

  std::unique_ptr<Blob> blob_;
  uint64_t base_ = 0;
  uint32_t tables_ = 2;
};

}  // namespace

#ifdef _WIN32
std::unique_ptr<Blob> OpenFileBlob(const std::wstring& path, std::wstring* error) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    *error = LastErrorText(L"Could not open", path);
    return nullptr;
  }
  LARGE_INTEGER size;
  GetFileSizeEx(h, &size);
  return std::make_unique<FileBlob>(h, uint64_t(size.QuadPart));
}
#else
std::unique_ptr<Blob> OpenFileBlob(const std::wstring& path, std::wstring* error) {
  std::string n = Narrow(path);
  int fd = -1;
#ifdef __ANDROID__
  // The Android setup screen passes documents from the Storage Access Framework as
  // /proc/self/fd/<fd>. Opening that path again goes back through the shared-storage
  // permission check, which the app fails (scoped storage, EACCES). Read the descriptor
  // the picker granted instead.
  constexpr const char kFdPrefix[] = "/proc/self/fd/";
  if (n.rfind(kFdPrefix, 0) == 0) {
    int src = atoi(n.c_str() + sizeof(kFdPrefix) - 1);
    fd = fcntl(src, F_DUPFD_CLOEXEC, 0);
    if (fd >= 0) lseek(fd, 0, SEEK_SET);
  } else
#endif
    fd = open(n.c_str(), O_RDONLY | O_CLOEXEC);
  struct stat st = {};
  bool ok = fd >= 0 && fstat(fd, &st) == 0;
  if (ok && S_ISDIR(st.st_mode)) {
    errno = EISDIR;
    ok = false;
  }
  if (!ok) {
    *error = LastErrorText(L"Could not open", path);
    if (fd >= 0) close(fd);
    return nullptr;
  }
#ifdef __APPLE__
  fcntl(fd, F_RDAHEAD, 1);
#else
  posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif
  return std::make_unique<FileBlob>(fd, uint64_t(st.st_size));
}
#endif

const Entry* Source::Find(const std::string& path) const {
  std::string want = Lower(path);
  for (const Entry& e : entries_) {
    if (Lower(e.path) == want) return &e;
  }
  return nullptr;
}

std::unique_ptr<Source> OpenDisc(const std::wstring& path, std::wstring* error) {
  std::error_code ec;
  if (fs::is_directory(path, ec)) {
    auto folder = std::make_unique<DiscFolder>(path);
    if (!folder->Load(error)) return nullptr;
    return folder;
  }
  auto blob = OpenFileBlob(path, error);
  if (!blob) return nullptr;
  auto disc = std::make_unique<DiscImage>(std::move(blob), path);
  if (!disc->Load(error)) return nullptr;
  return disc;
}

bool LooksLikeStfs(Blob& blob) {
  char magic[4];
  return blob.size() > 0x971A && blob.ReadAt(0, magic, 4) &&
         (!memcmp(magic, "CON ", 4) || !memcmp(magic, "LIVE", 4) || !memcmp(magic, "PIRS", 4));
}

std::unique_ptr<Source> OpenStfs(std::unique_ptr<Blob> blob, std::wstring* error) {
  auto stfs = std::make_unique<Stfs>(std::move(blob));
  if (!stfs->Load(error)) return nullptr;
  return stfs;
}

}  // namespace loader
