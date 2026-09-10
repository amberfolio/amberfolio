// SPDX-License-Identifier: AGPL-3.0-only
//
// Where the desktop host looks for an OCR engine (#382).
//
// The order is the claim: beside the binary first, then `PATH` in the
// order the platform gave it. A search that quietly reordered `PATH`
// would answer a different question from the one the player's shell
// answers, and a player debugging "which tesseract is it using" would
// have nowhere to stand.

#include "ocr_discovery.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

namespace amberfolio::sdl {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

/// The same join the unit makes, so a test on Windows is not a test of
/// which slash somebody typed.
[[nodiscard]] std::string joined(const char* directory, const char* file) {
  return (std::filesystem::path(directory) / file).lexically_normal().string();
}

TEST(OcrDiscovery, LooksBesideTheBinaryFirst) {
  EXPECT_THAT(ocr_candidates("/opt/amberfolio", "/usr/bin:/usr/local/bin", ':',
                             "tesseract"),
              ElementsAre(joined("/opt/amberfolio", "tesseract"),
                          joined("/usr/bin", "tesseract"),
                          joined("/usr/local/bin", "tesseract")));
}

TEST(OcrDiscovery, KeepsThePathsOwnOrder) {
  EXPECT_THAT(ocr_candidates({}, "/second/../first:/second", ':', "tesseract"),
              ElementsAre(joined("/first", "tesseract"),
                          joined("/second", "tesseract")));
}

TEST(OcrDiscovery, SkipsEmptyEntries) {
  // An empty `PATH` entry means the current directory on Windows, and
  // the directory a player happened to launch from is not a place to go
  // looking for a program to run.
  EXPECT_THAT(ocr_candidates({}, ":/usr/bin::", ':', "tesseract"),
              ElementsAre(joined("/usr/bin", "tesseract")));
  EXPECT_THAT(ocr_candidates({}, {}, ':', "tesseract"), IsEmpty());
}

TEST(OcrDiscovery, NamesAPlaceOnce) {
  // A `PATH` with a repeat in it is ordinary; a report that said the
  // same directory four times would read like a bug in the report.
  EXPECT_THAT(
      ocr_candidates("/usr/bin", "/usr/bin:/usr/bin:/opt", ':', "tesseract"),
      ElementsAre(joined("/usr/bin", "tesseract"),
                  joined("/opt", "tesseract")));
}

TEST(OcrDiscovery, WindowsSeparatorIsJustAnotherCharacter) {
  EXPECT_THAT(ocr_candidates({}, R"(C:\bin;C:\tools)", ';', "tesseract.exe"),
              ElementsAre(joined(R"(C:\bin)", "tesseract.exe"),
                          joined(R"(C:\tools)", "tesseract.exe")));
}

TEST(OcrDiscovery, AnEngineWithNoNameIsNoSearch) {
  EXPECT_THAT(ocr_candidates("/opt", "/usr/bin", ':', {}), IsEmpty());
}

}  // namespace
}  // namespace amberfolio::sdl
