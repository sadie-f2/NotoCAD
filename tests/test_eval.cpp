// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "ncad/lisp/eval.hpp"
#include "ncad/lisp/value.hpp"

#include <sstream>
#include <string>

using namespace ncad;
using namespace ncad::lisp;

namespace {

// Evaluates source and returns the last value in prin1 form, so tests read as
// source -> result. Errors come back as "<error: ...>" rather than throwing.
struct Fixture {
    Context ctx;
    Interp in{ctx};
    std::ostringstream out;

    Fixture() { in.set_output(&out); }

    std::string eval(const std::string& source) {
        in.clear_error();
        Value v;
        if (!in.eval_string(source, v)) return "<error: " + in.error().message() + ">";
        return prin1(v);
    }

    EvalStatus status(const std::string& source) {
        in.clear_error();
        Value v;
        in.eval_string(source, v);
        return in.error().status;
    }

    std::string printed() { return out.str(); }
};

}  // namespace

TEST_CASE("eval: atoms evaluate to themselves") {
    Fixture f;
    CHECK(f.eval("42") == "42");
    CHECK(f.eval("1.5") == "1.5");
    CHECK(f.eval("\"hi\"") == "\"hi\"");
    CHECK(f.eval("nil") == "nil");
    CHECK(f.eval("T") == "T");
}

TEST_CASE("eval: quote suppresses evaluation") {
    Fixture f;
    CHECK(f.eval("'foo") == "FOO");
    CHECK(f.eval("'(1 2 3)") == "(1 2 3)");
    CHECK(f.eval("(quote (a b))") == "(A B)");
    // Without the quote, the list is a call to an undefined function.
    CHECK(f.status("(a b)") == EvalStatus::UndefinedFunction);
}

TEST_CASE("eval: unbound symbols are an error, not nil") {
    Fixture f;
    CHECK(f.status("undefined-thing") == EvalStatus::UnboundVariable);
    CHECK(f.eval("(setq x 1)") == "1");
    CHECK(f.eval("x") == "1");
}

TEST_CASE("eval: arithmetic keeps integers integral") {
    Fixture f;
    // This is the R12 rule that LISP files depend on: (/ 7 2) is 3, not 3.5.
    CHECK(f.eval("(/ 7 2)") == "3");
    CHECK(f.eval("(/ 7.0 2)") == "3.5");
    CHECK(f.eval("(+ 1 2 3)") == "6");
    CHECK(f.eval("(+ 1 2.0)") == "3.0");
    CHECK(f.eval("(* 2 3 4)") == "24");
    CHECK(f.eval("(- 10 1 2)") == "7");
    CHECK(f.eval("(- 5)") == "-5");
    CHECK(f.eval("(+)") == "0");
    CHECK(f.eval("(rem 7 2)") == "1");
    CHECK(f.eval("(expt 2 10)") == "1024");
    CHECK(f.eval("(expt 2.0 0.5)") == "1.4142135623730951");
    CHECK(f.eval("(fix 3.7)") == "3");
    CHECK(f.eval("(float 3)") == "3.0");
    CHECK(f.eval("(abs -4)") == "4");
    CHECK(f.eval("(max 1 5 3)") == "5");
    CHECK(f.eval("(min 1 5 3)") == "1");
}

TEST_CASE("eval: division by zero is an error rather than a trap") {
    Fixture f;
    CHECK(f.status("(/ 1 0)") == EvalStatus::DivideByZero);
    CHECK(f.status("(/ 1.0 0.0)") == EvalStatus::DivideByZero);
    CHECK(f.status("(rem 1 0)") == EvalStatus::DivideByZero);
}

TEST_CASE("eval: comparisons chain and handle strings") {
    Fixture f;
    CHECK(f.eval("(< 1 2 3)") == "T");
    CHECK(f.eval("(< 1 3 2)") == "nil");
    CHECK(f.eval("(= 2 2.0)") == "T");
    CHECK(f.eval("(>= 3 3 2)") == "T");
    CHECK(f.eval("(= \"ab\" \"ab\")") == "T");
    CHECK(f.eval("(< \"a\" \"b\")") == "T");
    CHECK(f.eval("(/= 1 2)") == "T");
}

TEST_CASE("eval: eq is identity where equal is structural") {
    Fixture f;
    CHECK(f.eval("(equal '(1 2) '(1 2))") == "T");
    CHECK(f.eval("(eq '(1 2) '(1 2))") == "nil");
    CHECK(f.eval("(setq a '(1 2))") == "(1 2)");
    CHECK(f.eval("(eq a a)") == "T");
}

TEST_CASE("eval: if, cond and the missing-else case") {
    Fixture f;
    CHECK(f.eval("(if T 1 2)") == "1");
    CHECK(f.eval("(if nil 1 2)") == "2");
    CHECK(f.eval("(if nil 1)") == "nil");
    // Only nil is false; 0 is true.
    CHECK(f.eval("(if 0 'yes 'no)") == "YES");

    CHECK(f.eval("(cond ((= 1 2) 'a) ((= 1 1) 'b) (T 'c))") == "B");
    CHECK(f.eval("(cond (nil 'a))") == "nil");
    CHECK(f.eval("(cond (T))") == "T");  // no body: the test value is returned
}

TEST_CASE("eval: and/or short-circuit") {
    Fixture f;
    CHECK(f.eval("(and 1 2 3)") == "3");
    CHECK(f.eval("(and 1 nil 3)") == "nil");
    CHECK(f.eval("(and)") == "T");
    CHECK(f.eval("(or nil nil 3)") == "3");
    CHECK(f.eval("(or)") == "nil");
    // The right-hand side must not be evaluated, so an error there is invisible.
    CHECK(f.eval("(and nil (/ 1 0))") == "nil");
    CHECK(f.eval("(or 1 (/ 1 0))") == "1");
}

TEST_CASE("eval: while and repeat") {
    Fixture f;
    CHECK(f.eval("(setq i 0) (while (< i 5) (setq i (1+ i))) i") == "5");
    CHECK(f.eval("(setq n 0) (repeat 3 (setq n (+ n 2))) n") == "6");
    CHECK(f.eval("(repeat 0 'never)") == "nil");
    CHECK(f.status("(repeat 1.5 nil)") == EvalStatus::BadArgumentType);
}

TEST_CASE("eval: defun and recursion") {
    Fixture f;
    CHECK(f.eval("(defun sq (x) (* x x))") == "SQ");
    CHECK(f.eval("(sq 7)") == "49");

    f.eval("(defun fact (n) (if (<= n 1) 1 (* n (fact (1- n)))))");
    CHECK(f.eval("(fact 10)") == "3628800");

    // Arity is checked.
    CHECK(f.status("(sq 1 2)") == EvalStatus::WrongArgumentCount);
    CHECK(f.status("(sq)") == EvalStatus::WrongArgumentCount);
}

TEST_CASE("eval: defun locals are restored after the call") {
    Fixture f;
    f.eval("(setq tmp 'outer)");
    f.eval("(defun uses-local (x / tmp) (setq tmp (* x 2)) tmp)");
    CHECK(f.eval("(uses-local 5)") == "10");
    // The caller's binding must survive the callee's use of the same name.
    CHECK(f.eval("tmp") == "OUTER");
    // Locals start nil.
    f.eval("(defun peek (/ fresh) fresh)");
    CHECK(f.eval("(peek)") == "nil");
}

TEST_CASE("eval: scoping is dynamic, as R12 has it") {
    Fixture f;
    // callee reads a variable bound by its caller's parameter list. Under
    // lexical scope this would be an unbound-variable error.
    f.eval("(defun callee () passed)");
    f.eval("(defun caller (passed) (callee))");
    CHECK(f.eval("(caller 99)") == "99");
    // And the binding is gone once the caller returns.
    CHECK(f.status("passed") == EvalStatus::UnboundVariable);
}

TEST_CASE("eval: bindings unwind even when the call fails") {
    Fixture f;
    f.eval("(setq v 'original)");
    f.eval("(defun boom (v) (/ 1 0))");
    CHECK(f.status("(boom 'clobbered)") == EvalStatus::DivideByZero);
    CHECK(f.eval("v") == "ORIGINAL");
}

TEST_CASE("eval: runaway recursion is an error, not a crash") {
    Fixture f;
    f.in.set_max_depth(200);
    f.eval("(defun forever (n) (forever (1+ n)))");
    CHECK(f.status("(forever 0)") == EvalStatus::StackOverflow);
    // The interpreter is still usable afterwards.
    CHECK(f.eval("(+ 1 1)") == "2");
}

TEST_CASE("eval: list primitives") {
    Fixture f;
    CHECK(f.eval("(list 1 2 3)") == "(1 2 3)");
    CHECK(f.eval("(car '(1 2 3))") == "1");
    CHECK(f.eval("(cdr '(1 2 3))") == "(2 3)");
    CHECK(f.eval("(cadr '(1 2 3))") == "2");
    CHECK(f.eval("(caddr '(1 2 3))") == "3");
    CHECK(f.eval("(cons 1 2)") == "(1 . 2)");
    CHECK(f.eval("(cons 1 '(2))") == "(1 2)");
    CHECK(f.eval("(length '(1 2 3))") == "3");
    CHECK(f.eval("(nth 1 '(a b c))") == "B");
    CHECK(f.eval("(nth 9 '(a b c))") == "nil");
    CHECK(f.eval("(last '(1 2 3))") == "3");
    CHECK(f.eval("(reverse '(1 2 3))") == "(3 2 1)");
    CHECK(f.eval("(append '(1 2) '(3) '(4 5))") == "(1 2 3 4 5)");
    CHECK(f.eval("(member 2 '(1 2 3))") == "(2 3)");
    CHECK(f.eval("(subst 'x 'b '(a b c b))") == "(A X C X)");
    // car and cdr of nil are nil, not errors.
    CHECK(f.eval("(car nil)") == "nil");
    CHECK(f.eval("(cdr nil)") == "nil");
}

TEST_CASE("eval: assoc on a DXF-style alist") {
    Fixture f;
    // This is the shape entmake will consume, so it has to work exactly.
    const std::string alist = "(setq e '((0 . \"LINE\") (8 . \"WALLS\") (10 1.0 2.0 0.0)))";
    f.eval(alist);
    CHECK(f.eval("(cdr (assoc 0 e))") == "\"LINE\"");
    CHECK(f.eval("(cdr (assoc 8 e))") == "\"WALLS\"");
    CHECK(f.eval("(cdr (assoc 10 e))") == "(1.0 2.0 0.0)");
    CHECK(f.eval("(assoc 62 e)") == "nil");
}

TEST_CASE("eval: predicates and type") {
    Fixture f;
    CHECK(f.eval("(null nil)") == "T");
    CHECK(f.eval("(atom 'a)") == "T");
    CHECK(f.eval("(atom '(1))") == "nil");
    CHECK(f.eval("(listp '(1))") == "T");
    CHECK(f.eval("(numberp 1.5)") == "T");
    CHECK(f.eval("(zerop 0)") == "T");
    CHECK(f.eval("(minusp -1)") == "T");
    CHECK(f.eval("(boundp 'nope)") == "nil");
    CHECK(f.eval("(type 1)") == "INT");
    CHECK(f.eval("(type 1.0)") == "REAL");
    CHECK(f.eval("(type \"s\")") == "STR");
    CHECK(f.eval("(type '(1))") == "LIST");
    CHECK(f.eval("(type nil)") == "nil");
}

TEST_CASE("eval: mapcar, foreach, apply and lambda") {
    Fixture f;
    CHECK(f.eval("(mapcar '1+ '(1 2 3))") == "(2 3 4)");
    CHECK(f.eval("(mapcar '+ '(1 2) '(10 20))") == "(11 22)");
    // Stops at the shortest list.
    CHECK(f.eval("(mapcar '+ '(1 2 3) '(10))") == "(11)");
    CHECK(f.eval("(apply '+ '(1 2 3))") == "6");
    CHECK(f.eval("(apply 'strcat '(\"a\" \"b\"))") == "\"ab\"");

    CHECK(f.eval("(mapcar '(lambda (x) (* x 10)) '(1 2 3))") == "(10 20 30)");
    CHECK(f.eval("((lambda (a b) (+ a b)) 3 4)") == "7");

    CHECK(f.eval("(setq total 0) (foreach n '(1 2 3) (setq total (+ total n))) total") == "6");
    // foreach unbinds its variable afterwards.
    CHECK(f.status("n") == EvalStatus::UnboundVariable);
}

TEST_CASE("eval: builtin names evaluate to callable values") {
    Fixture f;
    // Passing a builtin without quoting it has to work, since (mapcar car ...)
    // is common in real files.
    CHECK(f.eval("(mapcar car '((1 2) (3 4)))") == "(1 3)");
}

TEST_CASE("eval: string operations") {
    Fixture f;
    CHECK(f.eval("(strcat \"a\" \"b\" \"c\")") == "\"abc\"");
    CHECK(f.eval("(strlen \"hello\")") == "5");
    // substr is 1-based.
    CHECK(f.eval("(substr \"hello\" 2)") == "\"ello\"");
    CHECK(f.eval("(substr \"hello\" 2 3)") == "\"ell\"");
    CHECK(f.eval("(substr \"hello\" 99)") == "\"\"");
    CHECK(f.eval("(strcase \"aBc\")") == "\"ABC\"");
    CHECK(f.eval("(strcase \"aBc\" T)") == "\"abc\"");
    CHECK(f.eval("(itoa 42)") == "\"42\"");
    CHECK(f.eval("(atoi \"42abc\")") == "42");
    CHECK(f.eval("(atof \"3.5\")") == "3.5");
    CHECK(f.eval("(rtos 3.14159 2 2)") == "\"3.14\"");
    CHECK(f.eval("(chr 65)") == "\"A\"");
    CHECK(f.eval("(ascii \"A\")") == "65");
}

TEST_CASE("eval: output goes to the interpreter's stream") {
    Fixture f;
    f.eval("(princ \"hello\")");
    CHECK(f.printed() == "hello");
    // princ prints strings raw where prin1 prints them readably.
    Fixture g;
    g.eval("(prin1 \"a\\nb\")");
    CHECK(g.printed() == "\"a\\nb\"");
}

TEST_CASE("eval: argument type errors are reported, not ignored") {
    Fixture f;
    CHECK(f.status("(+ 1 \"two\")") == EvalStatus::BadArgumentType);
    CHECK(f.status("(strcat \"a\" 1)") == EvalStatus::BadArgumentType);
    CHECK(f.status("(car 5)") == EvalStatus::BadArgumentType);
    CHECK(f.status("(1+)") == EvalStatus::WrongArgumentCount);
    // The error message names the offender.
    f.status("(+ 1 \"two\")");
    CHECK(f.in.error().message().find("two") != std::string::npos);
}

TEST_CASE("eval: reader errors surface as eval errors with a position") {
    Fixture f;
    CHECK(f.status("(+ 1 2") == EvalStatus::ReadFailed);
    CHECK(f.in.error().detail.find("line") != std::string::npos);
}

TEST_CASE("eval: a whole program runs end to end") {
    Fixture f;
    const std::string program = R"(
        (defun make-point (x y) (list x y 0.0))
        (defun grid (n / pts i)
          (setq pts nil i 0)
          (while (< i n)
            (setq pts (cons (make-point (float i) (float (* i i))) pts))
            (setq i (1+ i)))
          (reverse pts))
        (grid 4)
    )";
    CHECK(f.eval(program) == "((0.0 0.0 0.0) (1.0 1.0 0.0) (2.0 4.0 0.0) (3.0 9.0 0.0))");
}

TEST_CASE("eval: bulk allocation stays in the arena") {
    // The mesh path builds tens of thousands of cells; this is that shape in
    // miniature, and it must not leak or blow the C++ stack.
    Fixture f;
    f.eval("(defun build (n / out i) (setq out nil i 0)"
           " (while (< i n) (setq out (cons i out)) (setq i (1+ i))) out)");
    CHECK(f.eval("(length (build 20000))") == "20000");
    CHECK(f.ctx.arena().bytes_used() > 20000 * sizeof(Cons));
}

TEST_CASE("eval: PI is predefined") {
    // AutoLISP files assume it and never set it.
    Fixture f;
    CHECK(f.eval("(> pi 3.14159)") == "T");
    CHECK(f.eval("(< pi 3.1416)") == "T");
    CHECK(f.eval("(equal PI Pi)") == "T");  // symbols are case-insensitive
}

TEST_CASE("eval: quit stops evaluation without being an error") {
    Fixture f;
    CHECK(f.in.quit_requested() == false);
    CHECK(f.eval("(setq a 1) (quit) (setq a 2)") == "nil");
    CHECK(f.in.quit_requested() == true);
    CHECK(f.in.error().ok());

    // The form after (quit) never ran.
    f.in.clear_quit();
    CHECK(f.eval("a") == "1");

    // It takes effect inside a loop too, rather than at the end of it.
    f.in.clear_quit();
    f.eval("(setq n 0)");
    f.eval("(while (< n 100) (setq n (1+ n)) (if (= n 3) (exit)))");
    CHECK(f.eval("n") == "3");
}

// --- crashes found by audit, 2026-08-17 -------------------------------------
//
// Every one of these segfaulted or corrupted memory before the fix beside it.
// They are grouped because they share a cause: the evaluator has been guarded
// against runaway recursion since it was written, and nothing else was.

TEST_CASE("append: no arguments is nil, not a wild read") {
    // `n - 1` on an unsigned zero is SIZE_MAX, so the loop ran and indexed
    // a[SIZE_MAX - 1]. APPEND declares min_args 0, so `(append)` was typeable.
    Fixture f;
    CHECK(f.eval("(append)") == "nil");
    CHECK(f.eval("(append '(1 2))") == "(1 2)");
    CHECK(f.eval("(append '(1) '(2) '(3))") == "(1 2 3)");
}

TEST_CASE("mapcar: a user-defined function survives the stack reallocating") {
    // mapcar's `a` points into Interp::stack_, and each apply() re-enters eval
    // and pushes onto that same vector. Re-reading a[0] per iteration therefore
    // read freed memory once the vector grew -- which it does on the FIRST call
    // when the function is a lambda rather than a builtin.
    //
    // In release this did not crash. It returned "no function definition" for a
    // function that was perfectly well defined.
    Fixture f;
    f.eval("(defun sq (x) (* x x))");
    CHECK(f.eval("(mapcar 'sq '(1 2 3 4 5))") == "(1 4 9 16 25)");

    // A builtin never reallocated the stack, which is why the bug hid.
    CHECK(f.eval("(mapcar '1+ '(1 2 3))") == "(2 3 4)");

    // Two lists, user function, enough elements to reallocate more than once.
    f.eval("(defun pair (a b) (+ (* 10 a) b))");
    CHECK(f.eval("(mapcar 'pair '(1 2 3) '(4 5 6))") == "(14 25 36)");
}

TEST_CASE("reader: nesting is bounded rather than blowing the C stack") {
    // 50k nested parens segfaulted the release build. read_form and read_list
    // recurse into each other once per level.
    Fixture f;

    std::string deep(6000, '(');
    deep.append(6000, ')');
    const std::string result = f.eval(deep);
    // An error, and specifically the depth one -- not a crash, and not a
    // spurious "malformed" that would hide the real limit.
    CHECK(result.find("nested too deeply") != std::string::npos);

    // The same shape via quote, which recurses through the same counter.
    std::string quoted;
    for (int i = 0; i < 6000; ++i) quoted += '\'';
    quoted += "x";
    CHECK(f.eval(quoted).find("nested too deeply") != std::string::npos);

    // And ordinary nesting still reads.
    CHECK(f.eval("(car '((1 2) 3))") == "(1 2)");
}

TEST_CASE("printer: a deeply nested list prints rather than crashing") {
    // Built ITERATIVELY, so eval depth stays at 2 and max_depth_ never sees it.
    // Printing then recursed once per level -- from the REPL echo, from (print),
    // and from prin1 inside an error message, so a bad call crashed while trying
    // to describe itself.
    Fixture f;
    f.eval("(setq x nil)");
    f.eval("(repeat 20000 (setq x (list x)))");

    const std::string printed = f.eval("(prin1 x)");
    CHECK(!printed.empty());
    // Truncated with an ellipsis rather than descending forever.
    CHECK(printed.find("...") != std::string::npos);

    // The error path prints too, and must survive the same structure.
    const std::string err = f.eval("(apply x '())");
    CHECK(err.find("<error:") != std::string::npos);
}

TEST_CASE("wcmatch: repeated negation does not recurse per tilde") {
    Fixture f;
    // Semantics first: each '~' flips, and they are an anchor at position zero.
    CHECK(f.eval("(wcmatch \"abc\" \"a*\")") == "T");
    CHECK(f.eval("(wcmatch \"abc\" \"~a*\")") == "nil");
    CHECK(f.eval("(wcmatch \"abc\" \"~~a*\")") == "T");

    // Then the crash: one C frame per tilde used to overflow the stack.
    f.eval("(setq p \"~\")");
    f.eval("(repeat 16 (setq p (strcat p p)))");
    const std::string r = f.eval("(wcmatch \"a\" p)");
    CHECK((r == "T" || r == "nil"));
}
