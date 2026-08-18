// SPDX-License-Identifier: AGPL-3.0-only
//
// The conformance suite as CTest sees it (issues #14, #34): one case per
// vector file of the pinned SingleStepTests/8088 v2 set, all of them
// expected to pass.
//
// Registered dynamically rather than written out as 323 TEST() macros —
// which is the reason GoogleTest was chosen for this project in the first
// place (cmake/AmberfolioGoogleTest.cmake). The list comes from the
// generated manifest, so a case exists for every file the pin has, and
// the manifest's length is checked against the pin at configure time. A
// suite that runs 322 files cannot report itself as a passing one.
//
// One way is left for a case to be skipped, and it is not allowed to
// happen in CI: the vectors are not on this machine at all. That is the
// normal state for a contributor who has not run the fetch script, and
// the reason `ctest` is green for them rather than 323 shades of red —
// but in CI it would be a silently unrun suite, so CI sets
// AMBERFOLIO_CONFORMANCE_REQUIRED and the skip becomes a failure.
//
// Unlike the unit-test binary this one has a main() of its own, at the
// bottom: cases that are registered rather than declared need somewhere
// for the registering to happen.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>

#include "machine.h"
#include "registry.h"
#include "vectors.h"

namespace amberfolio::conformance {
namespace {

/// How many failing vectors get a full report before the rest are
/// counted rather than printed. A newly-wired family can fail thousands,
/// and a CI log with ten thousand register diffs in it is one nobody
/// reads.
constexpr std::size_t reports_per_file = 10;

[[nodiscard]] bool vectors_are_required() {
  const char* value = std::getenv("AMBERFOLIO_CONFORMANCE_REQUIRED");
  return value != nullptr && *value != '\0' && std::string_view{value} != "0";
}

class vector_case : public ::testing::Test {
 public:
  explicit vector_case(std::string_view stem) : stem_(stem) {}

 private:
  void TestBody() override {
    const std::filesystem::path path = vector_path(stem_);
    if (!std::filesystem::exists(path)) {
      const std::string missing =
          "no condensed vectors at " + path.string() +
          "\n  run: python3 scripts/fetch-conformance-vectors.py";
      if (vectors_are_required()) {
        FAIL() << missing;
      }
      GTEST_SKIP() << missing;
    }

    vector_file file;
    try {
      file = load_vectors(stem_, test_limit());
    } catch (const std::exception& error) {
      FAIL() << error.what();
    }

    RecordProperty("vectors", static_cast<int>(file.tests.size()));

    vector_machine machine;
    std::size_t failed = 0;
    for (const vector_test& test : file.tests) {
      const std::string report = machine.run(test);
      if (report.empty()) {
        continue;
      }
      ++failed;
      if (failed <= reports_per_file) {
        ADD_FAILURE() << stem_ << " " << report;
      }
    }

    if (failed > reports_per_file) {
      ADD_FAILURE() << stem_ << ": " << failed << " of " << file.tests.size()
                    << " vectors failed; the first " << reports_per_file
                    << " are above.";
    }
  }

  std::string stem_;
};

void register_vector_cases() {
  for (const std::string_view stem : all_stems()) {
    const std::string name = case_name(stem);
    ::testing::RegisterTest(
        "conformance", name.c_str(), nullptr, nullptr, __FILE__, __LINE__,
        [stem]() -> ::testing::Test* { return new vector_case(stem); });
  }
}

}  // namespace
}  // namespace amberfolio::conformance

/// This binary has a main of its own rather than linking gmock_main,
/// because the cases are built rather than declared and something has to
/// build them. It only has to happen before RUN_ALL_TESTS — a static
/// initialiser would do it too, but a line in main says when it happens
/// and cannot throw from somewhere nothing can catch.
int main(int argc, char** argv) {
  ::testing::InitGoogleMock(&argc, argv);
  try {
    amberfolio::conformance::register_vector_cases();
  } catch (const std::exception& error) {
    // A binary that could not name its own tests must not go on to report
    // that none of them failed.
    std::fprintf(stderr, "cannot register the conformance cases: %s\n",
                 error.what());
    return 1;
  }
  return RUN_ALL_TESTS();
}
