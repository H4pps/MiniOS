#include "mini_os/line_editor.h"

#include <gtest/gtest.h>

using kernel::EditAction;
using kernel::LineEditor;

TEST(LineEditor, PrintableAsciiAndTerminator) {
    LineEditor editor;
    EXPECT_EQ(editor.length(), 0U);
    EXPECT_STREQ(editor.text(), "");
    for (uint8_t byte = 32; byte <= 126; ++byte) {
        EXPECT_EQ(editor.feed(byte), EditAction::appended);
        EXPECT_EQ(editor.text()[editor.length() - 1], static_cast<char>(byte));
        EXPECT_EQ(editor.text()[editor.length()], '\0');
    }
    EXPECT_EQ(editor.length(), 95U);
    EXPECT_EQ(editor.feed('\n'), EditAction::submitted);
    EXPECT_EQ(editor.length(), 95U); // Caller can render before clearing.
    editor.clear();
    EXPECT_STREQ(editor.text(), "");
}

TEST(LineEditor, BothDeletionKeysAndEmptyDeletion) {
    LineEditor editor;
    EXPECT_EQ(editor.feed(8), EditAction::ignored);
    EXPECT_EQ(editor.feed(127), EditAction::ignored);
    editor.feed('a');
    editor.feed('b');
    EXPECT_EQ(editor.feed(8), EditAction::erased);
    EXPECT_STREQ(editor.text(), "a");
    EXPECT_EQ(editor.feed(127), EditAction::erased);
    EXPECT_STREQ(editor.text(), "");
}

TEST(LineEditor, EnterVariantsEmptyLinesAndCrlfSuppressionSurviveClear) {
    LineEditor editor;
    EXPECT_EQ(editor.feed('\n'), EditAction::submitted);
    editor.clear();
    EXPECT_EQ(editor.feed('\r'), EditAction::submitted);
    editor.clear();
    EXPECT_EQ(editor.feed('\n'), EditAction::ignored);
    EXPECT_EQ(editor.feed('\n'), EditAction::submitted);
    editor.clear();
    editor.feed('\r');
    editor.clear();
    EXPECT_EQ(editor.feed('a'), EditAction::appended);
    EXPECT_EQ(editor.feed('\n'), EditAction::submitted);
    EXPECT_STREQ(editor.text(), "a");
}

TEST(LineEditor, UnsupportedBytesAreIgnored) {
    LineEditor editor;
    for (unsigned value = 0; value <= 255; ++value) {
        if ((value >= 32 && value <= 126) || value == 8 || value == 127 || value == 10 ||
            value == 13) {
            continue;
        }
        EXPECT_EQ(editor.feed(static_cast<uint8_t>(value)), EditAction::ignored);
    }
    EXPECT_STREQ(editor.text(), "");
}

TEST(LineEditor, ExactBoundaryCanSubmitOrEdit) {
    LineEditor editor;
    for (size_t i = 0; i < LineEditor::capacity; ++i) {
        ASSERT_EQ(editor.feed('x'), EditAction::appended);
    }
    EXPECT_EQ(editor.length(), 127U);
    EXPECT_EQ(editor.text()[127], '\0');
    EXPECT_EQ(editor.feed('\n'), EditAction::submitted);
    EXPECT_EQ(editor.feed(8), EditAction::erased);
    EXPECT_EQ(editor.feed('y'), EditAction::appended);
    EXPECT_EQ(editor.length(), 127U);
}

TEST(LineEditor, OverflowRingsOnceDiscardsWholeLineAndRecovers) {
    LineEditor editor;
    for (size_t i = 0; i < LineEditor::capacity; ++i) {
        editor.feed('x');
    }
    EXPECT_EQ(editor.feed('x'), EditAction::overflow);
    EXPECT_STREQ(editor.text(), "");
    EXPECT_EQ(editor.feed('y'), EditAction::ignored);
    EXPECT_EQ(editor.feed(8), EditAction::ignored);
    EXPECT_EQ(editor.feed(127), EditAction::ignored);
    EXPECT_EQ(editor.feed('\r'), EditAction::rejected_too_long);
    editor.clear();
    EXPECT_EQ(editor.feed('\n'), EditAction::ignored);
    EXPECT_EQ(editor.feed('z'), EditAction::appended);
    EXPECT_EQ(editor.feed('\n'), EditAction::submitted);
    EXPECT_STREQ(editor.text(), "z");
}

TEST(LineEditor, ReceiveErrorCancelsThroughEnterAndRecovers) {
    LineEditor editor;
    editor.feed('a');
    editor.cancel();
    EXPECT_STREQ(editor.text(), "");
    EXPECT_EQ(editor.feed('b'), EditAction::ignored);
    EXPECT_EQ(editor.feed(8), EditAction::ignored);
    editor.cancel(); // Multiple hardware errors still reject just one line.
    EXPECT_EQ(editor.feed('\r'), EditAction::rejected_receive_error);
    editor.clear();
    EXPECT_EQ(editor.feed('\n'), EditAction::ignored);
    EXPECT_EQ(editor.feed('c'), EditAction::appended);
    EXPECT_EQ(editor.feed('\n'), EditAction::submitted);
    EXPECT_STREQ(editor.text(), "c");
}

TEST(LineEditor, ReceiveErrorOverridesOverflowAndDoesNotSuppressRecoveryEnter) {
    LineEditor editor;
    for (size_t i = 0; i <= LineEditor::capacity; ++i) {
        editor.feed('x');
    }
    editor.cancel();
    EXPECT_EQ(editor.feed('\n'), EditAction::rejected_receive_error);
    editor.clear();
    editor.feed('\r');
    editor.clear();
    editor.cancel();
    EXPECT_EQ(editor.feed('\n'), EditAction::rejected_receive_error);
}
