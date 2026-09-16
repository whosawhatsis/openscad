#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "gui/ScadLexer.h"

// The editor's keyword lists are hand-maintained, so a builtin registered in
// Builtins.cc is highlighted only if someone also remembers to add it here.
// material() was missed exactly that way and shipped unhighlighted.
//
// Note this exercises Lex, the lexertl lexer behind ScadLexer2. ScadLexer, the
// QsciLexerCPP one with the keywordSet[] tables, is compiled out whenever
// ENABLE_LEXERTL is 1 - which it always is - so testing those tables would
// prove nothing about what the editor actually does.
namespace {

class StyleRecorder : public LexInterface
{
public:
  std::vector<char> styles;

  void highlightingMultiple(int start, int length, char *s) override
  {
    if (styles.size() < static_cast<size_t>(start + length)) styles.resize(start + length, 0);
    for (int i = 0; i < length; ++i) styles[start + i] = s[i];
  }
  int getStyleAt(int /*position*/) override { return 0; }
};

// The style the lexer assigns to the first character of `word` in `source`.
int styleOfWord(const std::string& source, const std::string& word)
{
  Lex lex;
  lex.default_rules();
  lex.finalize_rules();

  StyleRecorder recorder;
  lex.lex_results(source, 0, &recorder);

  const auto at = source.find(word);
  if (at == std::string::npos || at >= recorder.styles.size()) return -1;
  return recorder.styles[at];
}

}  // namespace

TEST_CASE("editor highlights the wrapper modules")
{
  const std::string source = "color(\"red\") cube(1);\nmaterial(\"PLA\") cube(1);\n";

  // color() is the module material() was modelled on, so it is the reference:
  // whatever style it gets, material() must get the same one.
  const int colorStyle = styleOfWord(source, "color");
  REQUIRE(colorStyle > 0);
  CHECK(styleOfWord(source, "material") == colorStyle);
}
