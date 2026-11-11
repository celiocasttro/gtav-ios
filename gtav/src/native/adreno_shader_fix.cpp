// gtav - Adreno fixes for the game's translated shaders (see adreno_shader_fix.h).
//
// Both passes follow the Python originals step for step, including the order in which new ids are
// handed out, so the phone produces the same SPIR-V as tools/android/make_spirv.sh.

#include "adreno_shader_fix.h"

#include <cstdlib>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#if GTAV_HAVE_SPIRV_TOOLS
#include <spirv-tools/libspirv.h>
#endif

namespace gtav::adreno {

//=== ubo_constants.py ============================================================================

namespace {

const char kDecls[] =
    "\nstruct XenosAluConstants { float4 c[256]; };\n"
    "struct XenosSharedConstants { uint4 u[112]; };\n"
    "[[vk::binding(0, 4)]] ConstantBuffer<XenosAluConstants> g_VSC;\n"
    "[[vk::binding(1, 4)]] ConstantBuffer<XenosAluConstants> g_PSC;\n"
    "[[vk::binding(2, 4)]] ConstantBuffer<XenosSharedConstants> g_SC;\n";

const char* Alu(const std::string& stage) { return stage == "Vertex" ? "g_VSC" : "g_PSC"; }

const char* Cast(const std::string& type) {
  if (type == "float") return "asfloat";
  if (type == "int") return "asint";
  return "";
}

std::string SharedWord(long off) {
  return "g_SC.u[" + std::to_string(off / 16) + "][" + std::to_string((off % 16) / 4) + "]";
}

// re.sub with a function, on one line.
template <typename F>
std::string Sub(const std::string& line, const std::regex& re, F&& fn) {
  std::string out;
  auto begin = std::sregex_iterator(line.begin(), line.end(), re), end = std::sregex_iterator();
  if (begin == end) return line;
  size_t last = 0;
  for (auto it = begin; it != end; ++it) {
    const std::smatch& m = *it;
    out.append(line, last, size_t(m.position(0)) - last);
    out += fn(m);
    last = size_t(m.position(0) + m.length(0));
  }
  out.append(line, last, std::string::npos);
  return out;
}

}  // namespace

bool UboConstants(std::string& hlsl, std::string* error) {
  static const std::regex kAlu(
      R"(vk::RawBufferLoad<float4>\(g_PushConstants\.(Vertex|Pixel)ShaderConstants \+ (\d+), 0x10\))");
  static const std::regex kAluIndexed(
      R"(vk::RawBufferLoad<float4>\(g_PushConstants\.(Vertex|Pixel)ShaderConstants \+ (\(.*?\)) \* 16, 0x10\))");
  static const std::regex kShared(
      R"(vk::RawBufferLoad<(uint|float|int)>\(g_PushConstants\.SharedConstants \+ (\d+)(?: \+ (\d+))?\))");
  static const std::regex kColorScale(
      R"(vk::RawBufferLoad<float4>\(g_PushConstants\.SharedConstants \+ (\d+) \+ \(INDEX\) \* 16\))");
  static const std::regex kLeftAlu(R"(RawBufferLoad<[^>]*>\(g_PushConstants\.(Vertex|Pixel)ShaderConstants)");
  static const std::regex kLeftShared(R"(RawBufferLoad<[^>]*>\(g_PushConstants\.SharedConstants \+ \d)");

  std::string out;
  out.reserve(hlsl.size());
  std::vector<std::string> left;
  size_t pos = 0;
  while (pos <= hlsl.size()) {
    size_t nl = hlsl.find('\n', pos);
    bool last = nl == std::string::npos;
    std::string line = hlsl.substr(pos, last ? std::string::npos : nl - pos);
    if (line.find("RawBufferLoad") != std::string::npos) {
      line = Sub(line, kAlu, [](const std::smatch& m) {
        return std::string(Alu(m[1])) + ".c[" + std::to_string(std::stol(m[2]) / 16) + "]";
      });
      line = Sub(line, kAluIndexed, [](const std::smatch& m) {
        return std::string(Alu(m[1])) + ".c[" + m[2].str() + "]";
      });
      line = Sub(line, kShared, [](const std::smatch& m) {
        long off = std::stol(m[2]) + (m[3].matched ? std::stol(m[3]) : 0);
        return std::string(Cast(m[1])) + "(" + SharedWord(off) + ")";
      });
      line = Sub(line, kColorScale, [](const std::smatch& m) {
        return "asfloat(g_SC.u[" + std::to_string(std::stol(m[1]) / 16) + " + (INDEX)])";
      });
    }
    // text.replace('(uint)(', '(')
    for (size_t p = 0; (p = line.find("(uint)(", p)) != std::string::npos;) line.replace(p, 7, "(");
    if (std::regex_search(line, kLeftAlu) || std::regex_search(line, kLeftShared)) {
      if (left.size() < 5) left.push_back(line);
    }
    out += line;
    if (last) break;
    out += '\n';
    pos = nl + 1;
  }
  if (!left.empty()) {
    if (error) {
      *error = "unmapped constant loads:";
      for (auto& l : left) *error += "\n" + l;
    }
    return false;
  }
  const std::string anchor = "[[vk::push_constant]] ConstantBuffer<PushConstants> g_PushConstants;";
  size_t a = out.find(anchor);
  if (a != std::string::npos && out.find("g_VSC;") == std::string::npos)
    out.insert(a + anchor.size(), std::string("\n") + kDecls);
  hlsl = std::move(out);
  return true;
}

//=== lower_int64.py ==============================================================================

namespace {

bool IsId(const std::string& t) {  // %\d+ (whole token)
  if (t.size() < 2 || t[0] != '%') return false;
  for (size_t i = 1; i < t.size(); ++i)
    if (t[i] < '0' || t[i] > '9') return false;
  return true;
}

// The %\d+ prefix of a token ("" when it does not start with one).
std::string IdPrefix(const std::string& t) {
  if (t.size() < 2 || t[0] != '%' || t[1] < '0' || t[1] > '9') return "";
  size_t i = 1;
  while (i < t.size() && t[i] >= '0' && t[i] <= '9') ++i;
  return t.substr(0, i);
}

bool IsWord(const std::string& t) {  // \w+
  if (t.empty()) return false;
  for (char c : t)
    if (!(isalnum((unsigned char)c) || c == '_')) return false;
  return true;
}

std::vector<std::string> Split(const std::string& s) {
  std::vector<std::string> v;
  std::istringstream in(s);
  for (std::string t; in >> t;) v.push_back(t);
  return v;
}

std::string Strip(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n\f\v");
  if (b == std::string::npos) return "";
  size_t e = s.find_last_not_of(" \t\r\n\f\v");
  return s.substr(b, e - b + 1);
}

struct Unhandled {
  std::string what;
};

class Lowerer {
 public:
  explicit Lowerer(const std::string& text) {
    size_t pos = 0;
    while (pos < text.size()) {
      size_t nl = text.find('\n', pos);
      if (nl == std::string::npos) nl = text.size();
      lines_.push_back(text.substr(pos, nl - pos));
      pos = nl + 1;
    }
    // str.splitlines() drops a final empty line; a trailing "\n" gives none here either.
    long max_id = 0;
    for (size_t i = 0; i < text.size(); ++i) {
      if (text[i] != '%') continue;
      size_t j = i + 1;
      long v = 0;
      while (j < text.size() && text[j] >= '0' && text[j] <= '9') v = v * 10 + (text[j++] - '0');
      if (j > i + 1 && v > max_id) max_id = v;
    }
    next_id_ = max_id + 1;
  }

  std::optional<std::string> Run() {
    // Pass 1: types.
    for (auto& l : lines_) {
      auto t = Split(l);
      if (t.size() >= 3 && IsId(t[0]) && t[1] == "=" && t[2].rfind("OpType", 0) == 0 && IsWord(t[2])) {
        size_t p = l.find(t[2], l.find(" = "));
        SetType(t[0], Strip(l.substr(p)));
      }
    }
    std::vector<std::string> t64;
    for (auto& [id, d] : types_)
      if (d == "OpTypeInt 64 0" || d == "OpTypeInt 64 1") t64.push_back(id);
    if (t64.empty()) return std::nullopt;
    uint_ = FindType("OpTypeInt 32 0");
    int_ = FindType("OpTypeInt 32 1");
    bool_ = FindType("OpTypeBool");
    std::string insert_uint;
    if (uint_.empty()) {
      uint_ = insert_uint = New();
      SetType(uint_, "OpTypeInt 32 0");
    }
    std::string v2 = FindType("OpTypeVector " + uint_ + " 2");
    v2_ = t64[0];
    std::unordered_map<std::string, std::string> rename;
    for (size_t i = 1; i < t64.size(); ++i) rename[t64[i]] = v2_;
    if (!v2.empty()) rename[v2] = v2_;
    auto ren = [&](const std::string& id) {
      auto it = rename.find(id);
      return it == rename.end() ? id : it->second;
    };
    for (auto& l : lines_) {
      auto t = Split(l);
      if (t.size() >= 5 && IsId(t[0]) && t[1] == "=" && t[2] == "OpTypePointer" && IsWord(t[3])) {
        std::string pointee = IdPrefix(t[4]);
        if (!pointee.empty()) ptr_target_.push_back({t[0], {t[3], ren(pointee)}});
      }
    }

    std::vector<std::string> out;
    long first_function = -1;
    t64set_.insert(t64.begin(), t64.end());
    inline_ = nullptr;
    for (const std::string& orig_line : lines_) {
      auto ot = Split(orig_line);
      bool is_def = ot.size() >= 4 && IsId(ot[0]) && ot[1] == "=" && ot[2].rfind("Op", 0) == 0 &&
                    IsWord(ot[2]);
      std::string orig3 = is_def ? IdPrefix(ot[3]) : "";
      bool orig_dup = ot.size() >= 4 && IsId(ot[0]) && ot[1] == "=" &&
                      (ot[2] == "OpTypeInt" || ot[2] == "OpTypeVector") && rename.count(ot[0]);
      cur64_ = !orig3.empty() && t64set_.count(orig3) && ot[2].rfind("OpType", 0) != 0;
      if (cur64_) ids64_.insert(ot[0]);
      // Unify the 64-bit types (and any existing uvec2) into one uvec2 id.
      std::string l;
      l.reserve(orig_line.size());
      for (size_t i = 0; i < orig_line.size();) {
        if (orig_line[i] == '%' && i + 1 < orig_line.size() && isdigit((unsigned char)orig_line[i + 1])) {
          size_t j = i + 1;
          while (j < orig_line.size() && isdigit((unsigned char)orig_line[j])) ++j;
          l += ren(orig_line.substr(i, j - i));
          i = j;
        } else {
          l += orig_line[i++];
        }
      }
      std::string s = Strip(l);
      if (s == "OpCapability Int64" || orig_dup) continue;
      auto st = Split(s);
      bool m = st.size() >= 3 && IsId(st[0]) && st[1] == "=" && st[2].rfind("Op", 0) == 0 && IsWord(st[2]);
      if (m && st[0] == v2_ && st[2] == "OpTypeInt") {
        if (!insert_uint.empty()) out.push_back(insert_uint + " = OpTypeInt 32 0");
        out.push_back(v2_ + " = OpTypeVector " + uint_ + " 2");
        continue;
      }
      if (s.rfind("OpFunction", 0) == 0 || (m && st[2] == "OpFunction")) {
        if (first_function < 0) first_function = long(out.size());
      }
      if (m) {
        const std::string& rid = st[0];
        const std::string& op = st[2];
        std::vector<std::string> rest(st.begin() + 3, st.end());
        static const std::unordered_set<std::string> kNoResultType = {
            "OpTypePointer", "OpTypeStruct",   "OpTypeVector",       "OpTypeArray",
            "OpTypeFunction", "OpTypeRuntimeArray"};
        if (!rest.empty() && rest[0][0] == '%' && !kNoResultType.count(op)) result_type_[rid] = rest[0];
        if (op == "OpConstant" && cur64_) {
          uint64_t v = rest[1][0] == '-' ? uint64_t(std::strtoll(rest[1].c_str(), nullptr, 0))
                                         : std::strtoull(rest[1].c_str(), nullptr, 0);
          inline_ = &out;  // scalar halves go right here, before their first use
          std::string lo_c = Const(v & 0xFFFFFFFFu), hi_c = Const(v >> 32);
          inline_ = nullptr;
          out.push_back(rid + " = OpConstantComposite " + v2_ + " " + lo_c + " " + hi_c);
          continue;
        }
        if (op == "OpBitcast" && rest.size() >= 2) {
          auto pt = PtrTarget(rest[0]);
          if (pt && pt->first == "PhysicalStorageBuffer") ptr_from_addr_[rid] = rest[1];
        }
        std::vector<std::string> expanded;
        if (Expand(rid, op, rest, expanded)) {
          out.insert(out.end(), expanded.begin(), expanded.end());
          continue;
        }
      }
      out.push_back(s);
    }
    if (first_function < 0) throw Unhandled{"no function"};
    // Globals right before the first function (all their dependencies are declared above).
    out.insert(out.begin() + first_function, global_add_.begin(), global_add_.end());
    std::string text;
    for (size_t i = 0; i < out.size(); ++i) {
      if (i) text += '\n';
      text += out[i];
    }
    return text + "\n";
  }

 private:
  std::string New() { return "%" + std::to_string(next_id_++); }

  void SetType(const std::string& id, const std::string& decl) {
    for (auto& [i, d] : types_)
      if (i == id) {
        d = decl;
        return;
      }
    types_.push_back({id, decl});
  }

  std::string FindType(const std::string& decl) const {
    for (auto& [i, d] : types_)
      if (d == decl) return i;
    return "";
  }

  std::string GetType(const std::string& decl) {
    auto it = type_cache_.find(decl);
    if (it != type_cache_.end()) return it->second;
    std::string t = FindType(decl);
    if (t.empty()) {
      t = New();
      global_add_.push_back(t + " = " + decl);
      types_.push_back({t, decl});
    }
    type_cache_[decl] = t;
    return t;
  }

  std::string Const(uint64_t value) {
    auto it = const_cache_.find(value);
    if (it != const_cache_.end()) return it->second;
    std::string c = New();
    (inline_ ? *inline_ : global_add_).push_back(c + " = OpConstant " + uint_ + " " + std::to_string(value));
    const_cache_[value] = c;
    return c;
  }

  std::optional<std::pair<std::string, std::string>> PtrTarget(const std::string& id) const {
    for (auto& [p, t] : ptr_target_)
      if (p == id) return t;
    return std::nullopt;
  }

  bool Is64(const std::string& id) const { return ids64_.count(id) != 0; }

  std::string ResultType(const std::string& id) const {
    auto it = result_type_.find(id);
    return it == result_type_.end() ? "" : it->second;
  }

  std::string Bool() {
    if (bool_.empty()) bool_ = GetType("OpTypeBool");
    return bool_;
  }

  // Python's `self.int or self.get_type('OpTypeInt 32 1')` (does not assign self.int).
  std::string IntOrGet() { return int_.empty() ? GetType("OpTypeInt 32 1") : int_; }

  bool Expand(const std::string& rid, const std::string& op, const std::vector<std::string>& rest,
              std::vector<std::string>& code) {
    if (rest.empty() || op.rfind("OpType", 0) == 0) return false;
    const std::string& rtype = rest[0];
    std::vector<std::string> ops(rest.begin() + 1, rest.end());
    const std::string v2 = v2_, uint = uint_;
    bool r64 = cur64_;
    bool any64 = r64;
    for (auto& o : ops)
      if (!o.empty() && o[0] == '%' && Is64(o)) any64 = true;
    if (!any64) return false;

    auto emit = [&](const std::string& body) {
      std::string r = New();
      code.push_back(r + " = " + body);
      return r;
    };
    auto lo = [&](const std::string& x) { return emit("OpCompositeExtract " + uint + " " + x + " 0"); };
    auto hi = [&](const std::string& x) { return emit("OpCompositeExtract " + uint + " " + x + " 1"); };
    auto add64 = [&](const std::string& a, const std::string& b) {
      std::string alo = lo(a), blo = lo(b), ahi = hi(a), bhi = hi(b);
      std::string s = emit("OpIAdd " + uint + " " + alo + " " + blo);
      std::string bool_t = Bool();
      std::string c = emit("OpULessThan " + bool_t + " " + s + " " + alo);
      std::string one = Const(1), zero = Const(0);
      std::string ci = emit("OpSelect " + uint + " " + c + " " + one + " " + zero);
      std::string h = emit("OpIAdd " + uint + " " + ahi + " " + bhi);
      std::string h2 = emit("OpIAdd " + uint + " " + h + " " + ci);
      return std::pair{s, h2};
    };
    auto to_uint = [&](const std::string& x) {
      if (ResultType(x) == uint) return x;
      return emit("OpBitcast " + uint + " " + x);
    };
    auto construct = [&](const std::string& a, const std::string& b) {
      code.push_back(rid + " = OpCompositeConstruct " + v2 + " " + a + " " + b);
      return true;
    };

    if (op == "OpIAdd" && r64) {
      auto [s, h] = add64(ops[0], ops[1]);
      return construct(s, h);
    }
    if (op == "OpISub" && r64) {
      std::string nb = emit("OpNot " + v2 + " " + ops[1]);
      std::string c1 = Const(1), c0 = Const(0);
      std::string one = emit("OpCompositeConstruct " + v2 + " " + c1 + " " + c0);
      auto [s1, h1] = add64(nb, one);
      std::string neg = emit("OpCompositeConstruct " + v2 + " " + s1 + " " + h1);
      auto [s, h] = add64(ops[0], neg);
      return construct(s, h);
    }
    if (op == "OpSConvert" && r64) {
      const std::string& x = ops[0];
      std::string xi = x;
      if (ResultType(x) == uint) {
        std::string ti = IntOrGet();
        xi = emit("OpBitcast " + ti + " " + x);
      }
      if (int_.empty()) int_ = GetType("OpTypeInt 32 1");
      std::string c31 = Const(31);
      std::string sh = emit("OpShiftRightArithmetic " + int_ + " " + xi + " " + c31);
      std::string a = to_uint(x);
      std::string b = to_uint(sh);
      return construct(a, b);
    }
    if (op == "OpUConvert" && r64) {
      std::string a = to_uint(ops[0]);
      std::string b = Const(0);
      return construct(a, b);
    }
    if ((op == "OpUConvert" || op == "OpSConvert") && Is64(ops[0])) {  // truncate
      std::string l0 = lo(ops[0]);
      if (rtype == uint)
        code.push_back(rid + " = OpCopyObject " + uint + " " + l0);
      else
        code.push_back(rid + " = OpBitcast " + rtype + " " + l0);
      return true;
    }
    if (op == "OpBitcast") {
      if (r64 && Is64(ops[0])) {
        code.push_back(rid + " = OpCopyObject " + v2 + " " + ops[0]);
        return true;
      }
      return false;  // pointer <-> uvec2 bitcasts are valid as they are
    }
    if (op == "OpIMul" && r64) {
      const std::string &a = ops[0], &b = ops[1];
      std::string stt = GetType("OpTypeStruct " + uint + " " + uint);
      std::string alo = lo(a), blo = lo(b), ahi = hi(a), bhi = hi(b);
      std::string mm = emit("OpUMulExtended " + stt + " " + alo + " " + blo);
      std::string mlo = emit("OpCompositeExtract " + uint + " " + mm + " 0");
      std::string mhi = emit("OpCompositeExtract " + uint + " " + mm + " 1");
      std::string t1 = emit("OpIMul " + uint + " " + alo + " " + bhi);
      std::string t2 = emit("OpIMul " + uint + " " + ahi + " " + blo);
      std::string h = emit("OpIAdd " + uint + " " + mhi + " " + t1);
      std::string h2 = emit("OpIAdd " + uint + " " + h + " " + t2);
      return construct(mlo, h2);
    }
    if (op == "OpSelect" && r64) {
      // A scalar condition selecting between vectors is SPIR-V 1.4+; Adreno's compiler asserts on
      // it ("src expected to have two components"): select per component.
      const std::string& cond = ops[0];
      std::string bool_t = Bool();
      std::string v2b = GetType("OpTypeVector " + bool_t + " 2");
      std::string c2 = emit("OpCompositeConstruct " + v2b + " " + cond + " " + cond);
      code.push_back(rid + " = OpSelect " + v2 + " " + c2 + " " + ops[1] + " " + ops[2]);
      return true;
    }
    static const std::unordered_set<std::string> kPassThrough = {
        "OpBitwiseAnd",      "OpBitwiseOr",        "OpBitwiseXor",       "OpNot",
        "OpPhi",             "OpCopyObject",       "OpStore",            "OpCompositeConstruct",
        "OpCompositeExtract", "OpCompositeInsert", "OpAccessChain",      "OpVariable",
        "OpFunctionParameter", "OpFunctionCall",   "OpUndef",            "OpConstantComposite",
        "OpConstantNull"};
    if (kPassThrough.count(op)) return false;  // componentwise on uvec2, or only the type changes
    if (op == "OpIEqual" || op == "OpINotEqual") {
      const std::string& bool_t = rtype;
      std::string v2b = GetType("OpTypeVector " + bool_t + " 2");
      std::string e = emit(op + " " + v2b + " " + ops[0] + " " + ops[1]);
      code.push_back(rid + " = " + (op == "OpIEqual" ? "OpAll" : "OpAny") + " " + bool_t + " " + e);
      return true;
    }
    if (op == "OpLoad" && r64) {
      const std::string& ptr = ops[0];
      auto it = ptr_from_addr_.find(ptr);
      if (it == ptr_from_addr_.end()) return false;  // push constants / function variables
      std::string addr = it->second;
      std::string pu = GetType("OpTypePointer PhysicalStorageBuffer " + uint);
      for (auto& [t, target] : ptr_target_)
        if (target.first == "PhysicalStorageBuffer" && target.second == uint) {
          pu = t;
          break;
        }
      std::string c4 = Const(4), c0 = Const(0);
      std::string four = emit("OpCompositeConstruct " + v2 + " " + c4 + " " + c0);
      auto [s, h] = add64(addr, four);
      std::string addr4 = emit("OpCompositeConstruct " + v2 + " " + s + " " + h);
      std::string p0 = emit("OpBitcast " + pu + " " + addr);
      std::string p1 = emit("OpBitcast " + pu + " " + addr4);
      std::string w0 = emit("OpLoad " + uint + " " + p0 + " Aligned 4");
      std::string w1 = emit("OpLoad " + uint + " " + p1 + " Aligned 4");
      return construct(w0, w1);
    }
    if (op == "OpLoad") return false;
    std::string what = "unhandled 64-bit op: " + op;
    for (auto& r : rest) what += " " + r;
    throw Unhandled{what};
  }

  std::vector<std::string> lines_;
  long next_id_ = 1;
  std::vector<std::pair<std::string, std::string>> types_;  // insertion order, like the dict
  std::unordered_map<std::string, std::string> result_type_;
  std::vector<std::pair<std::string, std::pair<std::string, std::string>>> ptr_target_;
  std::unordered_map<std::string, std::string> ptr_from_addr_;
  std::vector<std::string> global_add_;
  std::unordered_map<std::string, std::string> type_cache_;
  std::map<uint64_t, std::string> const_cache_;
  std::vector<std::string>* inline_ = nullptr;
  std::unordered_set<std::string> t64set_, ids64_;
  std::string uint_, int_, bool_, v2_;
  bool cur64_ = false;
};

}  // namespace

bool LowerInt64Text(const std::string& text, std::string* out, std::string* error) {
  try {
    auto r = Lowerer(text).Run();
    if (!r) return false;
    *out = std::move(*r);
    return true;
  } catch (const Unhandled& u) {
    if (error) *error = u.what;
    return false;
  }
}

#if GTAV_HAVE_SPIRV_TOOLS
bool LowerInt64(std::vector<uint32_t>& spirv, std::string* error) {
  spv_context ctx = spvContextCreate(SPV_ENV_VULKAN_1_1);
  spv_text text = nullptr;
  spv_diagnostic diag = nullptr;
  bool ok = false;
  // spirv-dis --raw-id: no friendly names; the header comments are skipped by the assembler.
  if (spvBinaryToText(ctx, spirv.data(), spirv.size(), SPV_BINARY_TO_TEXT_OPTION_NO_HEADER, &text,
                      &diag) != SPV_SUCCESS) {
    if (error) *error = std::string("disassemble: ") + (diag ? diag->error : "?");
  } else {
    std::string lowered, err;
    if (LowerInt64Text(std::string(text->str, text->length), &lowered, &err)) {
      spv_binary bin = nullptr;
      spv_diagnostic adiag = nullptr;
      if (spvTextToBinaryWithOptions(ctx, lowered.data(), lowered.size(),
                                     SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS, &bin,
                                     &adiag) == SPV_SUCCESS) {
        spirv.assign(bin->code, bin->code + bin->wordCount);
        ok = true;
      } else if (error) {
        *error = std::string("assemble: ") + (adiag ? adiag->error : "?");
      }
      spvBinaryDestroy(bin);
      spvDiagnosticDestroy(adiag);
    } else if (err.empty()) {
      ok = true;  // no 64-bit integers: unchanged
    } else if (error) {
      *error = err;
    }
  }
  spvTextDestroy(text);
  spvDiagnosticDestroy(diag);
  spvContextDestroy(ctx);
  return ok;
}
#else
bool LowerInt64(std::vector<uint32_t>&, std::string* error) {
  if (error) *error = "built without SPIRV-Tools";
  return false;
}
#endif

}  // namespace gtav::adreno
