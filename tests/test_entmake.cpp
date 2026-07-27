// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "noto/database.hpp"
#include "noto/ecs.hpp"
#include "noto/entities.hpp"
#include "noto/lisp/eval.hpp"

#include <cmath>
#include <sstream>
#include <string>

using namespace noto;
using namespace noto::lisp;

namespace {

// The harness compares components; entity code deals in whole vectors.
#define CHECK_VEC3(a, b) CHECK_VEC((a), (b).x, (b).y, (b).z, 1e-9)


struct Fixture {
    Database db;
    Context ctx;
    Interp in{ctx};
    std::ostringstream out;

    Fixture() {
        in.set_output(&out);
        in.set_database(&db);
    }

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

    // The single entity in the drawing, for checking what actually got built.
    const Entity* only() const {
        return db.order().empty() ? nullptr : db.get(db.order().front());
    }
};

}  // namespace

TEST_CASE("entmake: builds a LINE in world coordinates") {
    Fixture f;
    f.eval(R"((entmake '((0 . "LINE") (10 1.0 2.0 3.0) (11 4.0 5.0 6.0))))");
    CHECK(f.db.size() == 1);

    const Entity* e = f.only();
    CHECK(e != nullptr);
    CHECK(e->type() == EntityType::Line);

    const Line* line = static_cast<const Line*>(e);
    CHECK_VEC3(line->start(), Vec3(1.0, 2.0, 3.0));
    CHECK_VEC3(line->end(), Vec3(4.0, 5.0, 6.0));
}

TEST_CASE("entmake: builds CIRCLE and ARC") {
    Fixture f;
    f.eval(R"((entmake '((0 . "CIRCLE") (10 1.0 2.0 0.0) (40 . 5.0))))");
    const Circle* c = static_cast<const Circle*>(f.only());
    CHECK(c->type() == EntityType::Circle);
    CHECK_VEC3(c->center(), Vec3(1.0, 2.0, 0.0));
    CHECK_NEAR(c->radius(), 5.0, 1e-12);

    Fixture g;
    g.eval(R"((entmake '((0 . "ARC") (10 0.0 0.0 0.0) (40 . 2.0) (50 . 0.0) (51 . 1.5708))))");
    const Arc* a = static_cast<const Arc*>(g.only());
    CHECK(a->type() == EntityType::Arc);
    // Angles are radians in AutoLISP, so 1.5708 is a quarter turn, not 1.5708 degrees.
    CHECK_NEAR(a->start_angle(), 0.0, 1e-12);
    CHECK_NEAR(a->end_angle(), 1.5708, 1e-12);
    CHECK_NEAR(a->sweep(), 1.5708, 1e-12);
}

TEST_CASE("entmake: group 10 is in the entity coordinate system") {
    // The one that matters. For a tilted circle, group 10 is NOT the world
    // centre -- it is the centre expressed in the plane the extrusion defines.
    // Getting this backwards produces entities that look right in the database
    // and land somewhere else entirely on disk.
    Fixture f;
    const Vec3 normal(1.0, 0.0, 0.0);
    const Vec3 ecs_center(5.0, 0.0, 15.0);

    f.eval(R"((entmake '((0 . "CIRCLE") (10 5.0 0.0 15.0) (40 . 3.0) (210 1.0 0.0 0.0))))");

    const Circle* c = static_cast<const Circle*>(f.only());
    CHECK(c != nullptr);
    CHECK_VEC3(c->props().normal, normal);

    // Independently computed: ECS -> world through the arbitrary axis algorithm.
    const Vec3 expected = ecs_to_world(normal).transform_point(ecs_center);
    CHECK_VEC3(c->center(), expected);
    // For normal (1,0,0) the basis is ax=(0,1,0), ay=(0,0,1), so the world
    // centre is (15, 5, 0). Spelled out so a regression is legible.
    CHECK_VEC3(c->center(), Vec3(15.0, 5.0, 0.0));
}

TEST_CASE("entmake: a LINE is world-coordinate even when extruded") {
    // LINE is the R12 exception: both endpoints stay in world space regardless
    // of group 210, so the extrusion must not be applied to them.
    Fixture f;
    f.eval(R"((entmake '((0 . "LINE") (10 1.0 2.0 3.0) (11 4.0 5.0 6.0) (210 1.0 0.0 0.0))))");
    const Line* line = static_cast<const Line*>(f.only());
    CHECK_VEC3(line->start(), Vec3(1.0, 2.0, 3.0));
    CHECK_VEC3(line->end(), Vec3(4.0, 5.0, 6.0));
    CHECK_VEC3(line->props().normal, Vec3(1.0, 0.0, 0.0));
}

TEST_CASE("entmake: common properties are applied") {
    Fixture f;
    f.eval(R"((entmake '((0 . "LINE") (8 . "WALLS") (62 . 3) (39 . 1.5)
                         (10 0.0 0.0 0.0) (11 1.0 0.0 0.0))))");
    const Entity* e = f.only();
    CHECK(e->props().color == 3);
    CHECK_NEAR(e->props().thickness, 1.5, 1e-12);
    // The layer is created on demand rather than having to be declared first.
    CHECK(f.db.layer(e->props().layer).name == "WALLS");
    CHECK(f.db.find_layer("WALLS") != kInvalidLayer);
}

TEST_CASE("entmake: two-coordinate points are legal and mean Z = 0") {
    Fixture f;
    f.eval(R"((entmake '((0 . "LINE") (10 1.0 2.0) (11 3.0 4.0))))");
    const Line* line = static_cast<const Line*>(f.only());
    CHECK_VEC3(line->start(), Vec3(1.0, 2.0, 0.0));
    CHECK_VEC3(line->end(), Vec3(3.0, 4.0, 0.0));
}

TEST_CASE("entmake: malformed data is an error, not a silent nil") {
    Fixture f;
    // Twenty thousand faces into a mesh build, nil is not a diagnosis.
    CHECK(f.status(R"((entmake '((0 . "LINE") (10 1.0 2.0 3.0))))") ==
          EvalStatus::BadArgumentType);
    CHECK(f.status(R"((entmake '((10 1.0 2.0 3.0))))") == EvalStatus::BadArgumentType);
    CHECK(f.status(R"((entmake '((0 . "CIRCLE") (10 0.0 0.0 0.0) (40 . "big"))))") ==
          EvalStatus::BadArgumentType);
    CHECK(f.status(R"((entmake '((0 . "CIRCLE") (10 0.0 0.0 0.0) (40 . -1.0))))") ==
          EvalStatus::BadArgumentType);
    CHECK(f.status(R"((entmake '((0 . "CIRCLE") (10 0.0) (40 . 1.0))))") ==
          EvalStatus::BadArgumentType);
    CHECK(f.status(R"((entmake '((0 . "LINE") (10 1.0 2.0 3.0) (11 0.0 0.0 0.0)
                                 (210 0.0 0.0 0.0))))") == EvalStatus::BadArgumentType);
    CHECK(f.status(R"((entmake '((0 . "LINE") (6 . "DASHED") (10 1.0 2.0) (11 3.0 4.0))))") ==
          EvalStatus::BadArgumentType);
    // Nothing was added by any of those.
    CHECK(f.db.empty());
}

TEST_CASE("entmake: an unimplemented entity kind returns nil") {
    Fixture f;
    // A condition AutoLISP code tests for, distinct from bad data.
    CHECK(f.eval(R"((entmake '((0 . "PFACE") (10 0.0 0.0 0.0))))") == "nil");
    CHECK(f.db.empty());
    CHECK(f.in.error().ok());
}

TEST_CASE("entget: round-trips an entity back to an association list") {
    Fixture f;
    f.eval(R"((entmake '((0 . "CIRCLE") (8 . "PARTS") (10 1.0 2.0 0.0) (40 . 5.0))))");
    CHECK(f.eval("(cdr (assoc 0 (entget (entlast))))") == "\"CIRCLE\"");
    CHECK(f.eval("(cdr (assoc 8 (entget (entlast))))") == "\"PARTS\"");
    CHECK(f.eval("(cdr (assoc 40 (entget (entlast))))") == "5.0");
    CHECK(f.eval("(cdr (assoc 10 (entget (entlast))))") == "(1.0 2.0 0.0)");
    CHECK(f.eval("(type (cdr (assoc -1 (entget (entlast)))))") == "ENAME");
    // Group 210 is omitted when the entity lies in the world plane.
    CHECK(f.eval("(assoc 210 (entget (entlast)))") == "nil");
}

TEST_CASE("entget: a tilted entity survives the full alist round trip") {
    // entmake -> entget -> entmake must land in the same place. This is the
    // test that catches an ECS conversion applied in only one direction.
    Fixture f;
    f.eval(R"((entmake '((0 . "CIRCLE") (10 5.0 0.0 15.0) (40 . 3.0) (210 1.0 1.0 1.0))))");
    f.eval("(setq data (entget (entlast)))");
    f.eval("(entmake (cdr data))");  // drop the -1 pair; entmake ignores it anyway
    CHECK(f.db.size() == 2);

    const Circle* a = static_cast<const Circle*>(f.db.get(f.db.order()[0]));
    const Circle* b = static_cast<const Circle*>(f.db.get(f.db.order()[1]));
    CHECK_VEC3(a->center(), b->center());
    CHECK_VEC3(a->props().normal, b->props().normal);
    CHECK_NEAR(a->radius(), b->radius(), 1e-12);
}

TEST_CASE("entget: arc angles round-trip in radians") {
    Fixture f;
    f.eval(R"((entmake '((0 . "ARC") (10 0.0 0.0 0.0) (40 . 2.0) (50 . 1.0) (51 . 2.0))))");
    CHECK(f.eval("(cdr (assoc 50 (entget (entlast))))") == "1.0");
    CHECK(f.eval("(cdr (assoc 51 (entget (entlast))))") == "2.0");
}

TEST_CASE("entlast, entnext and entdel walk and edit the drawing") {
    Fixture f;
    f.eval(R"((entmake '((0 . "LINE") (10 0.0 0.0 0.0) (11 1.0 0.0 0.0))))");
    f.eval(R"((entmake '((0 . "LINE") (10 1.0 0.0 0.0) (11 2.0 0.0 0.0))))");
    f.eval(R"((entmake '((0 . "LINE") (10 2.0 0.0 0.0) (11 3.0 0.0 0.0))))");
    CHECK(f.db.size() == 3);

    // entnext with no argument starts at the first entity.
    CHECK(f.eval("(equal (entnext) (entnext))") == "T");
    CHECK(f.eval("(equal (entnext (entnext (entnext))) (entlast))") == "T");
    // Walking off the end yields nil.
    CHECK(f.eval("(entnext (entlast))") == "nil");

    // Counting the drawing by walking it, which is the idiom real files use.
    CHECK(f.eval("(setq n 0 e (entnext))"
                 "(while e (setq n (1+ n)) (setq e (entnext e))) n") == "3");

    f.eval("(entdel (entlast))");
    CHECK(f.db.size() == 2);
    CHECK(f.eval("(entdel (entlast))") != "nil");
    CHECK(f.db.size() == 1);
}

TEST_CASE("entget on a deleted entity is nil rather than an error") {
    Fixture f;
    f.eval(R"((entmake '((0 . "LINE") (10 0.0 0.0 0.0) (11 1.0 0.0 0.0))))");
    f.eval("(setq e (entlast))");
    f.eval("(entdel e)");
    CHECK(f.eval("(entget e)") == "nil");
    CHECK(f.in.error().ok());
}

TEST_CASE("entmod: modifies in place and keeps the entity name valid") {
    Fixture f;
    f.eval(R"((entmake '((0 . "CIRCLE") (10 0.0 0.0 0.0) (40 . 1.0))))");
    f.eval("(setq e (entlast))");
    f.eval("(setq data (entget e))");
    f.eval("(setq data (subst (cons 40 9.0) (assoc 40 data) data))");
    CHECK(f.eval("(if (entmod data) 'ok 'failed)") == "OK");

    CHECK(f.db.size() == 1);  // modified, not duplicated
    const Circle* c = static_cast<const Circle*>(f.only());
    CHECK_NEAR(c->radius(), 9.0, 1e-12);
    // The ename held by LISP still refers to the same entity.
    CHECK(f.eval("(cdr (assoc 40 (entget e)))") == "9.0");
    CHECK(f.eval("(equal e (entlast))") == "T");
}

TEST_CASE("entmod: reports a missing entity name") {
    Fixture f;
    CHECK(f.status(R"((entmod '((0 . "LINE") (10 0.0 0.0) (11 1.0 1.0))))") ==
          EvalStatus::BadArgumentType);
}

TEST_CASE("entity functions report a missing drawing rather than crashing") {
    // The interpreter is usable for pure computation with no drawing attached.
    Context ctx;
    Interp in(ctx);
    Value v;
    CHECK(in.eval_string(R"((entmake '((0 . "LINE") (10 0.0 0.0) (11 1.0 1.0))))", v) == false);
    CHECK(in.error().status == EvalStatus::BadArgumentType);
    in.clear_error();
    CHECK(in.eval_string("(+ 1 2)", v) == true);
}

TEST_CASE("entity names are not forgeable from integers") {
    Fixture f;
    CHECK(f.status("(entget 1)") == EvalStatus::BadArgumentType);
    CHECK(f.status("(entdel \"not-an-ename\")") == EvalStatus::BadArgumentType);
}

TEST_CASE("entmake: a LISP-driven mesh of entities, which is the point") {
    // The stated workload: geometry generated procedurally rather than drawn.
    Fixture f;
    const std::string program = R"(
        (defun ring (count radius / i angle step)
          (setq i 0 step (/ (* 2.0 pi) count))
          (while (< i count)
            (setq angle (* i step))
            (entmake (list '(0 . "CIRCLE")
                           '(8 . "RING")
                           (list 10 (* radius (cos angle)) (* radius (sin angle)) 0.0)
                           (cons 40 0.5)))
            (setq i (1+ i))))
        (setq pi 3.14159265358979)
        (ring 100 20.0)
    )";
    f.eval(program);
    CHECK(f.db.size() == 100);

    // Every circle landed on the ring, so the list-to-entity path is sound.
    for (const Handle h : f.db.order()) {
        const Circle* c = static_cast<const Circle*>(f.db.get(h));
        CHECK_NEAR(std::sqrt(c->center().x * c->center().x + c->center().y * c->center().y),
                   20.0, 1e-9);
    }
    CHECK(f.db.layer(f.only()->props().layer).name == "RING");
}
