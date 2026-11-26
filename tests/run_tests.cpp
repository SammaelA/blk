// Minimal test runner for blk, no framework on purpose.
//   blk_tests                  run everything in one process
//   blk_tests <test_name>      run one API test
//   blk_tests --case f.blk     run one file from cases/
// Prefer ctest: it runs every test in its own process, so a crash fails only that test.
//
// cases/*.blk:      must load; save(load(x)) must be stable; if x.out.blk exists, save(load(x)) must match it.
// cases/bad_*.blk:  must be rejected (load returns false, no crash, no exception).
#include "blk.h"
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

static std::string save(Block &b) { std::string s; save_block_to_string(s, b); return s; }
static bool reload(Block &b, Block &out) { return load_block_from_string(save(b), out); }
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
      "plain", "tab\tnl\n\"q\" \\ ?", "\x01", std::string("\x01") + "A", "\x7F",
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

TEST(parse_state_is_reset_between_calls)
{
  Block b;
  CHECK(!load_block_from_string("{ a:i = 1 // comment at end of input", b));
  CHECK(load_block_from_string("{ b:i = 2 }", b));
  CHECK(b.get_int("b") == 2);
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
  CHECK(get_enum_info("Nope") == nullptr);
}

TEST(arrays_api)
{
  Block b;
  std::vector<int> in{1, 2, 3};
  b.add_arr("a", in);
  std::vector<float> f{9};
  CHECK(b.get_arr("a", f) && f.size() == 4); // appends by default
  CHECK(b.get_arr("a", f, true) && f == std::vector<float>{1, 2, 3});
  std::vector<std::string> s;
  CHECK(!b.get_arr("a", s)); // wrong element type
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
