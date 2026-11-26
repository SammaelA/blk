// Minimal test runner for blk, no framework on purpose.
//   blk_tests                  run everything in one process
//   blk_tests <test_name>      run one API test
//   blk_tests --case f.blk     run one file from cases/
// Prefer ctest: it runs every test in its own process, so a crash fails only that test.
//
// cases/*.blk:      must load; save(load(x)) must be stable; if x.out.blk exists, save(load(x)) must match it.
// cases/bad_*.blk:  must be rejected (load returns false, no crash, no exception).
#include "blk.h"
#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <sys/stat.h>

static int g_failed = 0;
#define CHECK(...) do { if (!(__VA_ARGS__)) { ++g_failed; std::printf("  CHECK failed %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__); } } while (0)

static std::map<std::string, void (*)()> &registry() { static std::map<std::string, void (*)()> r; return r; }
struct Reg { Reg(const char *n, void (*f)()) { registry()[n] = f; } };
// TEST(name) must start at column 0: CMake greps for it to create the ctest entry.
#define TEST(name) static void test_##name(); static Reg reg_##name(#name, test_##name); static void test_##name()

static BlkEnumLoader mode_enum("Mode", {{"FAST", 1}, {"SLOW", 2}});
REGISTER_ENUM(Color, (std::vector<std::pair<std::string, unsigned>>{{"RED", 10}, {"GREEN", 20}, {"BLUE", 30}}))

static std::string save(const Block &b) { std::string s; save_block_to_string(s, b); return s; }
static bool reload(const Block &b, Block &out) { return load_block_from_string(save(b), out); }
static std::string read_file(const std::string &p) { std::ifstream f(p); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
static void write_file(const std::string &p, const std::string &s) { std::filesystem::remove(p); std::ofstream(p) << s; }

// strips trailing whitespace on every line and at the end
static std::string normalize(const std::string &s)
{
  std::string res, line;
  std::istringstream in(s);
  while (std::getline(in, line))
    res += line.substr(0, line.find_last_not_of(" \t\r") + 1) + "\n";
  return res.substr(0, res.find_last_not_of("\n") + 1);
}

static std::string printable(const std::string &s)
{
  std::string r;
  for (unsigned char c : s)
    if (c >= 32 && c < 127) r += char(c);
    else { char buf[8]; std::snprintf(buf, sizeof(buf), "\\x%02X", c); r += buf; }
  return r;
}

TEST(getters_defaults)
{
  Block b;
  CHECK(load_block_from_string("{ i:i = 5 s:s = \"x\" t:tag }", b));
  CHECK(b.get_int("i") == 5);
  CHECK(b.get_int("missing", 7) == 7);
  CHECK(b.get_int("s", 7) == 7); // wrong type -> default
  CHECK(b.get_string("i", "d") == "d");
  CHECK(b.has_tag("t") && !b.has_tag("i") && !b.has_tag("missing"));
  CHECK(b.get_type("s") == Block::STRING);
  CHECK(b.get_block("i") == nullptr);
}

TEST(all_scalar_types_api)
{
  Block b, r;
  b.add_bool("bt", true);
  b.add_bool("bf", false);
  b.add_int("i", -123456);
  b.add_uint64("u", 18446744073709551615ULL);
  b.add_double("d", -42.125);
  b.add_vec2("v2", float2(1.5f, -2.5f));
  b.add_vec3("v3", float3(1.0f, 2.0f, 3.0f));
  b.add_vec4("v4", float4(1.0f, 2.0f, 3.0f, 4.0f));
  b.add_ivec2("iv2", int2(-1, 2));
  b.add_ivec3("iv3", int3(3, -4, 5));
  b.add_ivec4("iv4", int4(-6, 7, -8, 9));
  float4x4 m;
  for (int k = 0; k < 16; k++)
    m(k / 4, k % 4) = float(k + 1);
  b.add_mat4("m4", m);
  b.add_enum("e", "Color", 20);
  b.add_string("s", "hello");

  // overwrite every scalar via set_*
  b.set_bool("bt", false);
  b.set_bool("bf", true);
  b.set_int("i", 987654);
  b.set_uint64("u", 42ULL);
  b.set_double("d", 3.5);
  b.set_vec2("v2", float2(-3.0f, 4.0f));
  b.set_vec3("v3", float3(4.0f, 5.0f, 6.0f));
  b.set_vec4("v4", float4(5.0f, 6.0f, 7.0f, 8.0f));
  b.set_ivec2("iv2", int2(10, 20));
  b.set_ivec3("iv3", int3(30, 40, 50));
  b.set_ivec4("iv4", int4(60, 70, 80, 90));
  float4x4 m2;
  for (int k = 0; k < 16; k++)
    m2(k / 4, k % 4) = float(16 - k);
  b.set_mat4("m4", m2);
  b.set_enum("e", "Color", 30);
  b.set_string("s", "world");

  CHECK(b.size() == 14);
  CHECK(reload(b, r));
  CHECK(r.size() == 14);

  // check by name and by id
  CHECK(r.get_bool("bt") == false && r.get_bool(0) == false);
  CHECK(r.get_bool("bf") == true && r.get_bool(1) == true);
  CHECK(r.get_int("i") == 987654 && r.get_int(2) == 987654);
  CHECK(r.get_uint64("u") == 42ULL && r.get_uint64(3) == 42ULL);
  CHECK(r.get_double("d") == 3.5 && r.get_double(4) == 3.5);
  float2 v2 = r.get_vec2("v2");
  CHECK(v2.x == -3.0f && v2.y == 4.0f);
  float3 v3 = r.get_vec3("v3");
  CHECK(v3.x == 4.0f && v3.y == 5.0f && v3.z == 6.0f);
  float4 v4 = r.get_vec4("v4");
  CHECK(v4.x == 5.0f && v4.y == 6.0f && v4.z == 7.0f && v4.w == 8.0f);
  int2 iv2 = r.get_ivec2("iv2");
  CHECK(iv2.x == 10 && iv2.y == 20);
  int3 iv3 = r.get_ivec3("iv3");
  CHECK(iv3.x == 30 && iv3.y == 40 && iv3.z == 50);
  int4 iv4 = r.get_ivec4("iv4");
  CHECK(iv4.x == 60 && iv4.y == 70 && iv4.z == 80 && iv4.w == 90);
  float4x4 mr = r.get_mat4("m4");
  for (int k = 0; k < 16; k++)
    CHECK(mr(k / 4, k % 4) == float(16 - k));
  CHECK(r.get_enum("e") == 30 && r.get_enum(12) == 30);
  CHECK(r.get_string("s") == "world" && r.get_string(13) == "world");

  // out-of-bounds id & default fallbacks
  CHECK(r.get_name(-1).empty() && r.get_name(r.size()).empty());
  CHECK(r.get_name(0) == "bt");
  CHECK(r.get_type(-1) == Block::EMPTY && r.get_type(r.size()) == Block::EMPTY);
  CHECK(r.get_bool("missing", true) == true);
  CHECK(r.get_uint64("missing", 99) == 99);
  CHECK(r.get_double("missing", 1.25) == 1.25);
  CHECK(r.get_vec2("missing", float2(7, 8)).x == 7 && r.get_vec2("missing", float2(7, 8)).y == 8);
  CHECK(r.get_ivec4("missing", int4(1, 2, 3, 4)).w == 4);
  CHECK(r.get_enum("missing", 55) == 55);
}

TEST(set_overwrites)
{
  Block b, inner;
  inner.add_int("x", 1);
  b.add_int("n", 1);          b.set_int("n", 2);
  b.set_string("s", "a");     b.set_string("s", "b");
  b.set_block("blk", &inner); b.set_block("blk", &inner);
  b.set_arr("arr", std::vector<int>{1, 2}); b.set_arr("arr", std::vector<int>{3});
  CHECK(b.size() == 4);
  CHECK(b.get_int("n") == 2);
  CHECK(b.get_string("s") == "b");
  CHECK(b.get_block("blk") && b.get_block("blk")->get_int("x") == 1);
  std::vector<int> a;
  CHECK(b.get_arr("arr", a) && a == std::vector<int>{3});
}

TEST(numbers_roundtrip_exactly)
{
  Block b, r;
  b.add_double("big", 1234567.0);
  b.add_double("pi", 3.141592653589793);
  b.add_vec3("v", float3(0.1f, 123456.7f, -1.0f / 3.0f));
  std::vector<double> arr{0.1, 1e10 + 1};
  b.add_arr("arr", arr);
  CHECK(reload(b, r));
  CHECK(r.get_double("big") == 1234567.0);
  CHECK(r.get_double("pi") == 3.141592653589793);
  float3 v = r.get_vec3("v");
  CHECK(v.x == 0.1f && v.y == 123456.7f && v.z == -1.0f / 3.0f);
  std::vector<double> arr2;
  CHECK(r.get_arr("arr", arr2) && arr2 == arr);
}

TEST(mat4_roundtrip)
{
  float4x4 m;
  for (int i = 0; i < 16; i++)
    m(i / 4, i % 4) = float(i + 1); // not symmetric
  Block b, r;
  b.add_mat4("m", m);
  CHECK(reload(b, r));
  float4x4 m2 = r.get_mat4("m");
  bool same = true;
  for (int i = 0; i < 16; i++)
    same = same && m(i / 4, i % 4) == m2(i / 4, i % 4);
  CHECK(same);
}

TEST(strings_roundtrip)
{
  const std::vector<std::string> cases = {
      "plain", "tab\tnl\n\"q\" \\ ?", "\a\b\f\r\v\'", "\x01", std::string("\x01") + "A", "\x7F",
      "\xC3\xA9 utf-8", std::string("a\0b", 3)};
  for (const std::string &c : cases)
  {
    Block b, r;
    b.add_string("s", c);
    if (!reload(b, r) || r.get_string("s") != c)
    {
      ++g_failed;
      std::printf("  string round-trip failed: \"%s\" -> \"%s\"\n", printable(c).c_str(), printable(r.get_string("s")).c_str());
    }
  }
}

TEST(string_escapes_parsing)
{
  Block b;
  CHECK(load_block_from_string(
      "{ a:s = \"\\a\\b\\f\\n\\r\\t\\v\\'\\\"\\\\\\?\" "
      "  oct1:s = \"\\0\\7\" "
      "  oct2:s = \"\\77z\" "
      "  oct3:s = \"\\101\\377\" "
      "  hex1:s = \"\\x9g\" "
      "  end_oct:s = \"a\\1\" "
      "  end_hex:s = \"a\\x41\" }", b));
  CHECK(b.get_string("a") == "\a\b\f\n\r\t\v\'\"\\\?");
  CHECK(b.get_string("oct1") == std::string("\x00\x07", 2));
  CHECK(b.get_string("oct2") == std::string("\x3F") + "z");
  CHECK(b.get_string("oct3") == std::string("A\xFF", 2));
  CHECK(b.get_string("hex1") == std::string("\x09") + "g");
  CHECK(b.get_string("end_oct") == std::string("a\x01", 2));
  CHECK(b.get_string("end_hex") == "aA");
}

TEST(save_string_utf8_and_del)
{
  // UTF-8 bytes >= 0x80 should be preserved literally (not mangled by signed-char < 32),
  // while 0x7F (DEL) is a non-printable control character and should be escaped.
  Block b;
  b.add_string("utf8", "caf\xC3\xA9");
  b.add_string("del", std::string(1, '\x7F'));
  std::string out = save(b);
  CHECK(out.find("\"caf\xC3\xA9\"") != std::string::npos);
  CHECK(out.find("\\x7F") != std::string::npos);
}

TEST(nested_blocks)
{
  Block b;
  CHECK(load_block_from_string("{ a { b { c { v:i = 3 } } } }", b));
  Block *c = b.get_block_rec("a.b.c");
  CHECK(c && c->get_int("v") == 3);
  CHECK(b.get_block_rec("a.x.c") == nullptr);

  Block inner, outer;
  inner.add_int("x", 1);
  outer.add_block("in", &inner);
  inner.set_int("x", 2); // add_block must copy
  CHECK(outer.get_block("in") && outer.get_block("in")->get_int("x") == 1);

  outer.add_block("empty", nullptr);
  CHECK(outer.get_block("empty") != nullptr && outer.get_block("empty")->size() == 0);
}

TEST(set_block_null_creates_empty_block)
{
  Block b;
  b.set_block("empty", nullptr);
  CHECK(b.get_block("empty") != nullptr && b.get_block("empty")->size() == 0);
}

TEST(assignment_is_deep)
{
  Block a, b;
  a.add_string("s", "x");
  b = a;
  a.clear();
  CHECK(b.get_string("s") == "x");
}

TEST(self_assignment_keeps_data)
{
  Block b;
  b.add_string("s", "x");
  b.add_int("i", 1);
  Block &alias = b;
  b = alias;
  CHECK(b.size() == 2);
  CHECK(b.get_string("s") == "x");
}

TEST(move_assignment)
{
  Block a, b;
  a.add_string("s", "from_a");
  a.add_arr("arr", std::vector<std::string>{"x", "y"});
  b.add_string("old", "will_be_cleared");
  b = std::move(a);
  CHECK(b.size() == 2 && b.get_string("s") == "from_a");
  std::vector<std::string> out;
  CHECK(b.get_arr("arr", out) && out == (std::vector<std::string>{"x", "y"}));
  // self-move
  Block &alias = b;
  b = std::move(alias);
  CHECK(b.size() == 2 && b.get_string("s") == "from_a");
}

TEST(copy_shrink_and_self_copy)
{
  // Copying a smaller block over a larger block with heap values must not leak.
  Block small, large;
  small.add_int("a", 1);
  large.add_string("s1", "one");
  large.add_string("s2", "two");
  large.add_block("sub", &small);
  large.add_arr("arr", std::vector<std::string>{"x", "y"});
  large.copy(&small);
  CHECK(large.size() == 1 && large.get_int("a") == 1);

  // Self-copy must preserve data.
  Block b;
  b.add_string("s", "keep");
  b.copy(&b);
  CHECK(b.size() == 1 && b.get_string("s") == "keep");
}

TEST(value_self_copy_and_ownership)
{
  // Value self-assignment must keep data.
  Block::Value v;
  v.type = Block::STRING;
  v.s = new std::string("hello");
  Block::Value &alias = v;
  v = alias;
  CHECK(v.type == Block::STRING && v.s && *v.s == "hello");
  v.clear();

  // add_value / set_value must not share raw pointers with the caller's Block.
  Block a, b;
  a.add_string("s", "owned");
  b.add_value("s", a.values[0]);
  a.clear();
  CHECK(b.get_string("s") == "owned");
}

TEST(duplicate_keys_and_next_id)
{
  Block b, r;
  CHECK(load_block_from_string("{ item:i = 10 other:s = \"hi\" item:i = 20 item:i = 30 }", b));
  int id1 = b.get_id("item");
  int id2 = b.get_next_id("item", id1 + 1);
  int id3 = b.get_next_id("item", id2 + 1);
  int id4 = b.get_next_id("item", id3 + 1);
  CHECK(id1 == 0 && id2 == 2 && id3 == 3 && id4 == -1);
  CHECK(b.get_int(id1) == 10 && b.get_int(id2) == 20 && b.get_int(id3) == 30);

  CHECK(reload(b, r));
  CHECK(r.size() == 4 && r.get_int(0) == 10 && r.get_int(2) == 20 && r.get_int(3) == 30);

  b.set_int("item", 99); // overwrites first occurrence
  CHECK(b.get_int(0) == 99 && b.get_int(2) == 20);
}

TEST(detalization_api)
{
  Block base, det;
  CHECK(load_block_from_string("{ a:i = 1 b:s = \"old\" sub { x:i = 1 } change_type:i = 5 }", base));
  CHECK(load_block_from_string("{ b:s = \"new\" c:i = 3 sub { y:i = 2 } change_type:r = 2.5 }", det));
  base.add_detalization(det);
  CHECK(base.get_int("a") == 1);
  CHECK(base.get_string("b") == "new");
  CHECK(base.get_int("c") == 3);
  CHECK(base.get_block("sub") && base.get_block("sub")->get_int("x") == 1 && base.get_block("sub")->get_int("y") == 2);
  // overriding a field with a different type should apply, not silently drop
  CHECK(base.get_type("change_type") == Block::DOUBLE && base.get_double("change_type") == 2.5);
}

TEST(parse_state_is_reset_between_calls)
{
  Block b;
  CHECK(!load_block_from_string("{ a:i = 1 // comment at end of input", b));
  CHECK(load_block_from_string("{ b:i = 2 }", b));
  CHECK(b.get_int("b") == 2);
}

TEST(empty_and_whitespace_inputs)
{
  Block b;
  CHECK(!load_block_from_string("", b));
  CHECK(!load_block_from_string("   \n\t // only a comment\n", b));
  CHECK(load_block_from_string("{}", b) && b.size() == 0);
  CHECK(load_block_from_string("  // leading\n { \n } ", b) && b.size() == 0);
}

TEST(crlf_line_endings)
{
  Block b;
  const std::string crlf =
      "// header comment\r\n"
      "{\r\n"
      "  flag:b = true\r\n"
      "  off:b = false\r\n"
      "  count:i = 42\r\n"
      "  mode:e_Mode = SLOW\r\n"
      "  marker:tag\r\n"
      "  base {\r\n"
      "    x:i = 1\r\n"
      "  }\r\n"
      "  derived extends base\r\n"
      "  {\r\n"
      "    y:i = 2\r\n"
      "  }\r\n"
      "}\r\n";
  CHECK(load_block_from_string(crlf, b));
  CHECK(b.get_bool("flag") == true);
  CHECK(b.get_bool("off") == false);
  CHECK(b.get_int("count") == 42);
  CHECK(b.get_enum("mode") == 2);
  CHECK(b.has_tag("marker"));
  CHECK(b.get_block("derived") && b.get_block("derived")->get_int("x") == 1 && b.get_block("derived")->get_int("y") == 2);
}

TEST(comments_without_spaces)
{
  Block b;
  CHECK(load_block_from_string("{ a:i = 1//comment\n b:i = 2 }", b));
  CHECK(b.get_int("a") == 1 && b.get_int("b") == 2);
}

TEST(bool_case_variants)
{
  Block b;
  CHECK(load_block_from_string("{ a:b = true b:b = True c:b = TRUE d:b = false e:b = False f:b = FALSE }", b));
  CHECK(b.get_bool("a") && b.get_bool("b") && b.get_bool("c"));
  CHECK(!b.get_bool("d") && !b.get_bool("e") && !b.get_bool("f"));
}

TEST(deep_nesting_does_not_crash)
{
  std::string deep;
  const int depth = 50000;
  deep.reserve(depth * 4 + 2);
  deep += "{";
  for (int i = 0; i < depth; i++)
    deep += "a{";
  for (int i = 0; i < depth; i++)
    deep += "}";
  deep += "}";
  Block b;
  // Must not stack-overflow: either reject cleanly or load cleanly without crashing.
  (void)load_block_from_string(deep, b);
  CHECK(true);
}

TEST(enums)
{
  Block b, r;
  b.add_enum("m", "Mode", 2);
  CHECK(b.get_enum("m") == 2);
  b.set_enum("m", "Mode", 1);
  CHECK(b.get_enum("m") == 1);
  CHECK(reload(b, r) && r.get_enum("m") == 1);
  CHECK(get_enum_info("Mode") && get_enum_info("Mode")->size() == 2);
  CHECK(get_enum_info("Color") && get_enum_info("Color")->size() == 3);
  CHECK(get_enum_info("Nope") == nullptr);

  // Invalid add_enum / set_enum must not insert or corrupt entries
  int sz = b.size();
  b.add_enum("bad1", "NoSuchEnum", 1);
  b.add_enum("bad2", "Mode", 999);
  b.set_enum("m", "Mode", 999);
  CHECK(b.size() == sz && b.get_enum("m") == 1);
}

TEST(enum_bounds_and_validation)
{
  // Out-of-bounds type_id must return nullptr instead of UB / crash
  CHECK(get_enum_names(999999) == nullptr);

  // Empty enum value name must be rejected
  register_enum_info("BadEmptyEnum", {{"", 0}, {"VALID", 1}});
  Block b;
  b.add_enum("x", "BadEmptyEnum", 0);
  CHECK(b.size() == 0);
  b.add_enum("y", "BadEmptyEnum", 1);
  CHECK(b.size() == 1 && b.get_enum("y") == 1);
}

TEST(arrays_api)
{
  Block b, r;
  b.add_arr("ad", std::vector<double>{1.5, -2.5});
  b.add_arr("af", std::vector<float>{0.5f, 1.5f});
  b.add_arr("ai", std::vector<int>{1, 2, 3});
  b.add_arr("au", std::vector<unsigned>{10u, 20u});
  b.add_arr("as", std::vector<short>{-5, 6});
  b.add_arr("aus", std::vector<unsigned short>{7, 8});
  b.add_arr("astr", std::vector<std::string>{"one", "two"});

  std::vector<float> f{9};
  CHECK(b.get_arr("ai", f) && f.size() == 4); // appends by default
  CHECK(b.get_arr("ai", f, true) && f == (std::vector<float>{1, 2, 3}));
  std::vector<std::string> s;
  CHECK(!b.get_arr("ai", s)); // wrong element type
  CHECK(b.get_arr("astr", s) && s == (std::vector<std::string>{"one", "two"}));

  // set_arr for every overload
  b.set_arr("ad", std::vector<double>{3.25});
  b.set_arr("af", std::vector<float>{2.25f});
  b.set_arr("au", std::vector<unsigned>{30u});
  b.set_arr("as", std::vector<short>{-11});
  b.set_arr("aus", std::vector<unsigned short>{12});
  b.set_arr("astr", std::vector<std::string>{"three"});

  CHECK(reload(b, r));
  std::vector<double> vd;
  std::vector<unsigned> vu;
  std::vector<short> vs;
  std::vector<unsigned short> vus;
  CHECK(r.get_arr("ad", vd, true) && vd == (std::vector<double>{3.25}));
  CHECK(r.get_arr("af", f, true) && f == (std::vector<float>{2.25f}));
  CHECK(r.get_arr("au", vu, true) && vu == (std::vector<unsigned>{30u}));
  CHECK(r.get_arr("as", vs, true) && vs == (std::vector<short>{-11}));
  CHECK(r.get_arr("aus", vus, true) && vus == (std::vector<unsigned short>{12}));
  CHECK(r.get_arr("astr", s, true) && s == (std::vector<std::string>{"three"}));
}

TEST(empty_string_array_roundtrip)
{
  Block b, r;
  b.add_arr("empty_s", std::vector<std::string>{});
  std::vector<std::string> s{"sentinel"};
  CHECK(b.get_arr("empty_s", s, true) && s.empty());
  CHECK(reload(b, r));
  s = {"sentinel"};
  CHECK(r.get_arr("empty_s", s, true) && s.empty());
}

TEST(files)
{
  write_file("ok.blk", "{ a:i = 1 }");
  Block b, r;
  CHECK(load_block_from_file("ok.blk", b) && b.get_int("a") == 1);
  CHECK(!load_block_from_file("does_not_exist.blk", r));
  save_block_to_file("saved.blk", b);
  CHECK(load_block_from_file("saved.blk", r) && r.get_int("a") == 1);
}

TEST(load_readonly_file)
{
  write_file("readonly.blk", "{ a:i = 1 }");
  chmod("readonly.blk", 0444);
  Block b;
  CHECK(load_block_from_file("readonly.blk", b) && b.get_int("a") == 1);
}

TEST(load_malformed_file_fails)
{
  write_file("malformed.blk", "{ a:i = 1");
  Block b;
  CHECK(!load_block_from_file("malformed.blk", b));
}

TEST(include)
{
  write_file("inc_child.blk", "{ x:i = 1 y:s = \"c\" }");
  write_file("inc_parent.blk", "{ #include \"inc_child.blk\" z:i = 2 }");
  Block b;
  CHECK(load_block_from_file("inc_parent.blk", b));
  CHECK(b.size() == 3 && b.get_int("x") == 1 && b.get_string("y") == "c" && b.get_int("z") == 2);
}

TEST(cyclic_include_fails)
{
  write_file("cyc_a.blk", "{ #include \"cyc_b.blk\" a:i = 1 }");
  write_file("cyc_b.blk", "{ #include \"cyc_a.blk\" b:i = 2 }");
  Block b;
  CHECK(!load_block_from_file("cyc_a.blk", b));
}

static void run_case(const std::string &path)
{
  Block b, r;
  bool loaded = load_block_from_string(read_file(path), b);
  if (std::filesystem::path(path).stem().string().rfind("bad_", 0) == 0)
  {
    CHECK(!loaded);
    return;
  }
  CHECK(loaded);
  std::string s1 = save(b);
  CHECK(load_block_from_string(s1, r));
  CHECK(s1 == save(r)); // save(load(x)) must be a fixed point

  std::string expected_path = path.substr(0, path.size() - 4) + ".out.blk";
  if (std::filesystem::exists(expected_path) && normalize(s1) != normalize(read_file(expected_path)))
  {
    ++g_failed;
    std::printf("  output differs from %s\n--- got ---\n%s\n--- expected ---\n%s\n", expected_path.c_str(),
                normalize(s1).c_str(), normalize(read_file(expected_path)).c_str());
  }
}

static void run(const std::string &label, const std::function<void()> &f)
{
  int before = g_failed;
  try { f(); }
  catch (const std::exception &e) { ++g_failed; std::printf("  threw: %s\n", e.what()); }
  std::printf("[%s] %s\n", g_failed == before ? " OK " : "FAIL", label.c_str());
}

int main(int argc, char **argv)
{
  std::setvbuf(stdout, nullptr, _IONBF, 0); // keep output ordered if a sanitizer aborts
  if (argc == 3 && std::string(argv[1]) == "--case")
    run(argv[2], [&] { run_case(argv[2]); });
  else if (argc == 2)
  {
    auto it = registry().find(argv[1]);
    if (it == registry().end()) { std::printf("unknown test %s\n", argv[1]); return 2; }
    run(it->first, it->second);
  }
  else
  {
    for (auto &[name, f] : registry())
      run(name, f);
    std::vector<std::string> files;
    for (auto &e : std::filesystem::directory_iterator(BLK_CASES_DIR))
      if (e.path().extension() == ".blk" && e.path().string().find(".out.blk") == std::string::npos)
        files.push_back(e.path().string());
    std::sort(files.begin(), files.end());
    for (auto &p : files)
      run(p, [&] { run_case(p); });
  }
  return g_failed ? 1 : 0;
}
