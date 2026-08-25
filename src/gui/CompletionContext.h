#pragma once

#include <QString>

/**
 * @brief What the grammar allows at the caret.
 *
 * Determined without parsing: the buffer is usually malformed while being typed,
 * so this is a tolerant scan rather than a use of the AST.
 */
enum class CompletionContext {
  /// Nothing may be completed here - inside a comment or a string literal.
  None,
  /// A module may be instantiated: statement start, a block body, or the child of a transform.
  Statement,
  /// A value is expected: functions and variables, never modules.
  Expression,
  /// Directly after '(' or ',' in a call, where a named argument or a value may follow.
  ArgumentName,
  /// Could not be determined; callers should filter nothing rather than guess.
  Unknown,
};

/**
 * @brief What the caret sits in.
 */
struct CaretContext {
  CompletionContext kind = CompletionContext::Unknown;
  /// Callable whose argument list encloses the caret, empty when none does.
  /// Reports the innermost call, which is the one an argument belongs to.
  QString enclosingCall;
};

/**
 * @brief Classify the caret position from the text preceding it.
 *
 * @param before Buffer text up to the caret. Any partial identifier at its end is
 *               ignored - the word being typed is not its own context.
 */
CaretContext classifyCaret(const QString& before);
