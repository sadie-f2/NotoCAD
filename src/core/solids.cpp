// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "ncad/solids.hpp"

#include "ncad/database.hpp"
#include "ncad/ecs.hpp"
#include "ncad/entities.hpp"
#include "ncad/sysvar.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace ncad {
namespace {

// The engine is a Python package, so what we look for is the package
// directory rather than a binary. Checking for the module file itself rather
// than just the directory means a half-made sibling checkout fails discovery
// instead of failing the first request.
bool looks_like_engine(const std::string& dir) {
    struct stat st {};
    return ::stat((dir + "/nsolid/protocol.py").c_str(), &st) == 0;
}

std::string home_dir() {
    const char* h = ::getenv("HOME");
    return h ? std::string(h) : std::string();
}

void append_point(std::string& s, const Vec3& p) {
    char buf[96];
    std::snprintf(buf, sizeof buf, " %.12g %.12g %.12g", p.x, p.y, p.z);
    s += buf;
}

void append_real(std::string& s, double v) {
    char buf[48];
    std::snprintf(buf, sizeof buf, " %.12g", v);
    s += buf;
}

// Reads whitespace-separated numbers out of a reply line. Returns false when
// the line is short, which is the only malformed case worth distinguishing --
// anything else is a protocol version mismatch and reads the same way.
bool take(std::istringstream& in, double* out, int n) {
    for (int i = 0; i < n; ++i) {
        if (!(in >> out[i])) return false;
    }
    return true;
}

Vec3 take_point(const double* v) { return Vec3{v[0], v[1], v[2]}; }

}  // namespace

// --- discovery ---------------------------------------------------------------

SolidEngine& SolidEngine::instance() {
    static SolidEngine engine;
    return engine;
}

void SolidEngine::set_root(std::string dir) {
    root_ = std::move(dir);
    why_.clear();
    discovered_ = true;
}

void SolidEngine::discover() {
    if (discovered_) return;
    discovered_ = true;

    // NCAD_NSOLID first, exactly as NCAD_IPERL overrides the script path, so a
    // test or an unusual layout needs no code change.
    if (const char* env = ::getenv("NCAD_NSOLID")) {
        if (looks_like_engine(env)) {
            root_ = env;
            return;
        }
        why_ = std::string("NCAD_NSOLID is set to \"") + env +
               "\" but there is no nsolid/protocol.py there";
        return;
    }

    const std::string home = home_dir();
    const std::string candidates[] = {
        "../Nsolid",
        "../../Nsolid",
        home.empty() ? std::string() : home + "/src/Nsolid",
    };
    for (const std::string& c : candidates) {
        if (!c.empty() && looks_like_engine(c)) {
            root_ = c;
            return;
        }
    }
    why_ =
        "the solids engine was not found. Set NCAD_NSOLID to the directory "
        "holding nsolid/, or put it beside this tree as ../Nsolid";
}

bool SolidEngine::available() {
    discover();
    return !root_.empty();
}

const std::string& SolidEngine::why_unavailable() {
    discover();
    return why_;
}

bool SolidEngine::request(const std::string& body, std::vector<std::string>& reply,
                          std::string& error) {
    reply.clear();
    error.clear();
    if (!available()) {
        error = why_unavailable();
        return false;
    }

    // Both directions go through temporary files rather than a pair of pipes.
    // A single-threaded parent writing a large request while the child writes
    // a large reply can deadlock once either pipe buffer fills, and a profile
    // of a few thousand segments is large enough to get there. Files cannot
    // deadlock, and they leave the exact conversation on disk when something
    // needs debugging, which a pipe does not.
    char in_path[] = "/tmp/ncad_solid_in_XXXXXX";
    char out_path[] = "/tmp/ncad_solid_out_XXXXXX";
    const int in_fd = ::mkstemp(in_path);
    if (in_fd < 0) {
        error = "could not create a temporary file for the request";
        return false;
    }
    const int out_fd = ::mkstemp(out_path);
    if (out_fd < 0) {
        ::close(in_fd);
        ::unlink(in_path);
        error = "could not create a temporary file for the reply";
        return false;
    }

    const ssize_t wrote = ::write(in_fd, body.data(), body.size());
    ::lseek(in_fd, 0, SEEK_SET);
    if (wrote != static_cast<ssize_t>(body.size())) {
        ::close(in_fd);
        ::close(out_fd);
        ::unlink(in_path);
        ::unlink(out_path);
        error = "could not write the request";
        return false;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(in_fd);
        ::close(out_fd);
        ::unlink(in_path);
        ::unlink(out_path);
        error = "could not start the solids engine";
        return false;
    }

    if (pid == 0) {
        ::dup2(in_fd, STDIN_FILENO);
        ::dup2(out_fd, STDOUT_FILENO);
        ::close(in_fd);
        ::close(out_fd);
        if (::chdir(root_.c_str()) != 0) ::_exit(127);
        ::execlp("python3", "python3", "-m", "nsolid", "--serve",
                 static_cast<char*>(nullptr));
        ::_exit(127);
    }

    int status = 0;
    ::waitpid(pid, &status, 0);
    ::close(in_fd);

    std::string text;
    ::lseek(out_fd, 0, SEEK_SET);
    char buf[4096];
    ssize_t n = 0;
    while ((n = ::read(out_fd, buf, sizeof buf)) > 0) text.append(buf, static_cast<std::size_t>(n));
    ::close(out_fd);
    ::unlink(in_path);
    ::unlink(out_path);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
        error = "could not run python3 for the solids engine";
        return false;
    }

    std::istringstream lines(text);
    std::string line;
    bool started = false;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("FAIL", 0) == 0) {
            error = line.size() > 5 ? line.substr(5) : "the engine refused the request";
            return false;
        }
        if (line == "OK") {
            started = true;
            continue;
        }
        if (line == "DONE") return true;
        if (started) reply.push_back(line);
    }
    if (!started) {
        error = text.empty() ? "the solids engine produced no reply"
                             : "the solids engine produced no usable reply";
        return false;
    }
    error = "the reply from the solids engine was truncated";
    return false;
}

// --- entities out ------------------------------------------------------------

bool solids_profile_request(const Database& db, const std::vector<Handle>& handles,
                            std::string& out, std::string& error) {
    out = "PROFILE\n";
    std::size_t used = 0;

    for (const Handle h : handles) {
        const Entity* e = db.get(h);
        if (!e) continue;

        switch (e->type()) {
            case EntityType::Line: {
                const auto* l = static_cast<const Line*>(e);
                std::string s = "LINE";
                append_point(s, l->start());
                append_point(s, l->end());
                out += s + "\n";
                ++used;
                break;
            }
            case EntityType::Circle: {
                const auto* c = static_cast<const Circle*>(e);
                std::string s = "CIRCLE";
                append_point(s, c->center());
                append_point(s, c->props().normal);
                append_real(s, c->radius());
                out += s + "\n";
                ++used;
                break;
            }
            case EntityType::Arc: {
                const auto* a = static_cast<const Arc*>(e);
                // The ECS x-axis is what the arc's angles are measured from,
                // so it has to cross with them. Sending the angles alone and
                // letting the far side guess a reference is how a tilted arc
                // lands rotated -- the same trap ELLIPSE's world-space major
                // axis exists to avoid on the read side.
                const Mat4 to_world = ecs_to_world(a->props().normal);
                const Vec3 xdir = to_world.transform_vector(Vec3{1.0, 0.0, 0.0});
                std::string s = "ARC";
                append_point(s, a->center());
                append_point(s, a->props().normal);
                append_point(s, xdir);
                append_real(s, a->radius());
                append_real(s, a->start_angle());
                append_real(s, a->end_angle());
                out += s + "\n";
                ++used;
                break;
            }
            case EntityType::Polyline: {
                // Sent as its segments. Bulges become arcs, so a polyline with
                // curved segments crosses exactly rather than as a chord --
                // which is the whole reason segment_arc() exists.
                const auto* p = static_cast<const Polyline*>(e);
                const std::size_t segs = p->segment_count();
                for (std::size_t i = 0; i < segs; ++i) {
                    Vec3 centre{};
                    double radius = 0.0;
                    double a0 = 0.0;
                    double a1 = 0.0;
                    if (p->segment_arc(i, &centre, &radius, &a0, &a1)) {
                        std::string s = "ARC";
                        append_point(s, centre);
                        append_point(s, p->props().normal);
                        append_point(s, Vec3{1.0, 0.0, 0.0});
                        append_real(s, radius);
                        append_real(s, a0);
                        append_real(s, a1);
                        out += s + "\n";
                    } else {
                        const std::vector<PolyVertex>& v = p->vertices();
                        const std::size_t j = (i + 1) % v.size();
                        std::string s = "LINE";
                        append_point(s, v[i].pos);
                        append_point(s, v[j].pos);
                        out += s + "\n";
                    }
                    ++used;
                }
                break;
            }
            default:
                error = std::string("a ") + e->type_name() +
                        " cannot be part of a profile -- select lines, arcs, "
                        "circles or polylines";
                return false;
        }
    }

    if (used == 0) {
        error = "nothing selected that can form a profile";
        return false;
    }
    out += "ENDPROFILE\n";
    return true;
}

// --- entities back -----------------------------------------------------------

void solids_apply_props(Entity& e, const EntityProps& props) {
    // Layer, colour, linetype and thickness come from the drawing. The NORMAL
    // does not: it is geometry the engine determined, and it is the one field
    // here that must survive. See the header for what happens when it does not.
    const Vec3 computed = e.props().normal;
    e.props() = props;
    e.props().normal = computed;
}

bool solids_wire_entities(const std::vector<std::string>& reply,
                          std::vector<EntityPtr>& out,
                          std::vector<std::string>& notes, std::string& error) {
    out.clear();
    notes.clear();

    for (std::size_t i = 0; i < reply.size(); ++i) {
        std::istringstream in(reply[i]);
        std::string word;
        in >> word;
        if (word.empty()) continue;

        if (word == "NOTE") {
            std::string rest;
            std::getline(in, rest);
            if (!rest.empty() && rest.front() == ' ') rest.erase(0, 1);
            notes.push_back(rest);
            continue;
        }

        double v[16] = {};
        if (word == "LINE") {
            if (!take(in, v, 6)) { error = "malformed LINE in the reply"; return false; }
            out.push_back(std::make_unique<Line>(take_point(v), take_point(v + 3)));
        } else if (word == "CIRCLE") {
            if (!take(in, v, 7)) { error = "malformed CIRCLE in the reply"; return false; }
            out.push_back(std::make_unique<Circle>(take_point(v), v[6], take_point(v + 3)));
        } else if (word == "ARC") {
            if (!take(in, v, 12)) { error = "malformed ARC in the reply"; return false; }
            auto arc = std::make_unique<Arc>(take_point(v), v[9], v[10], v[11],
                                             take_point(v + 3));
            out.push_back(std::move(arc));
        } else if (word == "ELLIPSE") {
            if (!take(in, v, 12)) { error = "malformed ELLIPSE in the reply"; return false; }
            out.push_back(std::make_unique<Ellipse>(take_point(v), take_point(v + 6),
                                                    v[9], v[10], v[11],
                                                    take_point(v + 3)));
        } else if (word == "PLINE") {
            // A polyline standing in for a curve with no exact form. The
            // tolerance it was computed to arrives with it and is dropped
            // here, because the database has nowhere to keep it -- which is a
            // real gap and is recorded in the handoff rather than papered over.
            int count = 0;
            int closed = 0;
            double tol = 0.0;
            if (!(in >> count >> closed >> tol) || count < 2) {
                error = "malformed PLINE in the reply";
                return false;
            }
            auto pl = std::make_unique<Polyline>();
            pl->set_closed(closed != 0);
            for (int k = 0; k < count; ++k) {
                if (++i >= reply.size()) { error = "PLINE ended early"; return false; }
                std::istringstream vin(reply[i]);
                std::string tag;
                vin >> tag;
                if (tag != "V" || !take(vin, v, 3)) {
                    error = "malformed PLINE vertex in the reply";
                    return false;
                }
                pl->add(take_point(v));
            }
            out.push_back(std::move(pl));
        } else {
            error = "unknown record \"" + word + "\" from the solids engine";
            return false;
        }
    }
    return true;
}

// --- the named solid table ---------------------------------------------------

SolidTable& SolidTable::instance() {
    static SolidTable table;
    return table;
}

std::string SolidTable::define(const std::string& body_template,
                               std::vector<std::string> deps) {
    const std::string name = "S" + std::to_string(++next_);
    std::string body = body_template;
    for (std::size_t at = body.find("@NAME@"); at != std::string::npos;
         at = body.find("@NAME@")) {
        body.replace(at, 6, name);
    }
    solids_[name] = Record{std::move(body), std::move(deps), {}};
    return name;
}

void SolidTable::forget(const std::string& name) {
    const auto it = solids_.find(name);
    if (it == solids_.end()) return;
    solids_.erase(it);
    if (name == "S" + std::to_string(next_)) --next_;
}

bool SolidTable::has(const std::string& name) const {
    return solids_.find(name) != solids_.end();
}

std::vector<std::string> SolidTable::names() const {
    std::vector<std::string> out;
    out.reserve(solids_.size());
    for (const auto& kv : solids_) out.push_back(kv.first);
    return out;
}

void SolidTable::record_drawn(const std::string& name, std::vector<Handle> handles) {
    auto it = solids_.find(name);
    if (it != solids_.end()) it->second.drawn = std::move(handles);
}

std::size_t SolidTable::erase_drawn(Database& db, const std::string& name) {
    auto it = solids_.find(name);
    if (it == solids_.end()) return 0;
    // The list is NOT cleared. Erasing is journalled, so UNDO can put those
    // entities back -- and if the table has forgotten which solid they
    // belonged to, the body is on screen and orphaned, and every later boolean
    // on that solid erases nothing. That is exactly what happened: union,
    // undo, then intersect, and both operands stayed.
    //
    // Keeping the handles is safe because `Database` guarantees they are
    // stable for an entity's lifetime and never reused, so a stale one can
    // never name a DIFFERENT entity -- within one drawing. Across NEW and
    // OPEN it can, which is why those clear the whole table.
    std::size_t n = 0;
    for (const Handle h : it->second.drawn) {
        if (db.erase(h)) ++n;
    }
    return n;
}

void SolidTable::clear() {
    solids_.clear();
    next_ = 0;
}

bool SolidTable::collect(const std::string& name, std::vector<std::string>& order,
                         std::vector<std::string>& seen, std::string& error) const {
    for (const std::string& s : seen) {
        if (s == name) {
            // Impossible through the commands, which only ever combine solids
            // that already exist -- but a cycle would recurse forever and the
            // check is two lines.
            error = "the solids reference each other in a cycle";
            return false;
        }
    }
    for (const std::string& s : order) {
        if (s == name) return true;         // already emitted
    }

    const auto it = solids_.find(name);
    if (it == solids_.end()) {
        error = "there is no solid named \"" + name + "\"";
        return false;
    }

    seen.push_back(name);
    for (const std::string& d : it->second.deps) {
        if (!collect(d, order, seen, error)) return false;
    }
    seen.pop_back();
    order.push_back(name);
    return true;
}

bool SolidTable::request_for(const std::string& name, std::string& out,
                             std::string& error) const {
    std::vector<std::string> order;
    std::vector<std::string> seen;
    if (!collect(name, order, seen, error)) return false;

    out.clear();
    for (const std::string& n : order) {
        out += solids_.at(n).body;
    }
    out += "WIRE " + name + "\n";
    return true;
}

// --- the commands ------------------------------------------------------------

Step SolidSweepCommand::start(CommandContext& ctx) {
    SolidEngine& engine = SolidEngine::instance();
    if (!engine.available()) return Step::failed(engine.why_unavailable());
    state_ = State::Selecting;
    return Step::ask(select_.prompt(ctx));
}

Step SolidSweepCommand::next(CommandContext& ctx, const InputValue& value) {
    switch (state_) {
        case State::Selecting: {
            switch (select_.feed(ctx, value)) {
                case SelectionPrompter::Result::Selecting:
                    return Step::ask(select_.prompt(ctx));
                case SelectionPrompter::Result::Finished: {
                    handles_.assign(ctx.selection.handles().begin(),
                                    ctx.selection.handles().end());
                    if (handles_.empty()) return Step::cancelled();
                    if (mode_ == Mode::Extrude) {
                        state_ = State::Height;
                        Prompt p;
                        p.kind = PromptKind::Distance;
                        p.message = "Specify height of extrusion";
                        return Step::ask(p);
                    }
                    state_ = State::AxisStart;
                    Prompt p;
                    p.kind = PromptKind::Point;
                    p.message = "Specify start point of axis";
                    return Step::ask(p);
                }
                case SelectionPrompter::Result::Rejected:
                    break;
            }
            return Step::failed("an entity is required");
        }

        case State::Height: {
            if (value.kind != InputKind::Real && value.kind != InputKind::Point) {
                return Step::failed("a distance is required");
            }
            const double h = value.kind == InputKind::Real
                                 ? value.real
                                 : length(value.point - Vec3{0.0, 0.0, 0.0});
            if (std::fabs(h) < 1e-12) return Step::failed("the height cannot be zero");
            std::string op = "EXTRUDE NORMAL";
            append_real(op, h);
            op += "\n";
            return run(ctx, op);
        }

        case State::AxisStart: {
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            axis_start_ = value.point;
            state_ = State::AxisEnd;
            Prompt p;
            p.kind = PromptKind::Point;
            p.message = "Specify end point of axis";
            p.base = axis_start_;
            p.has_base = true;
            return Step::ask(p);
        }

        case State::AxisEnd: {
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            axis_end_ = value.point;
            if (length(axis_end_ - axis_start_) < 1e-12) {
                return Step::failed("the axis has no length");
            }
            state_ = State::Angle;
            Prompt p;
            p.kind = PromptKind::Angle;
            p.message = "Specify angle of revolution <360>";
            p.allow_empty = true;
            return Step::ask(p);
        }

        case State::Angle: {
            double degrees = 360.0;
            if (value.kind == InputKind::Real) {
                degrees = value.real;
            } else if (value.kind != InputKind::None) {
                return Step::failed("an angle is required");
            }
            if (degrees <= 0.0 || degrees > 360.0 + 1e-9) {
                return Step::failed("the angle must be between 0 and 360 degrees");
            }
            const Vec3 dir = normalize(axis_end_ - axis_start_);
            // Built with the same %.12g helper the profile uses, and not with
            // an ostringstream: the default stream precision is six
            // significant digits, which rounded a full turn UP past 2*pi and
            // had the engine reject it as out of range. A protocol carrying
            // numbers has to say how many digits it carries.
            std::string op = "REVOLVE";
            append_point(op, axis_start_);
            append_point(op, dir);
            append_real(op, degrees >= 360.0 ? ncad::kFullTurn
                                             : degrees * ncad::kFullTurn / 360.0);
            op += "\n";
            return run(ctx, op);
        }
    }
    return Step::failed("internal state error");
}

namespace {

// Everything a completed solid needs doing once: send it, add what comes back,
// remember which entities it was. Shared so the sweep commands and the boolean
// commands cannot drift in how they report or what they record.
Step deliver(CommandContext& ctx, const std::string& name) {
    std::string body;
    std::string error;
    if (!SolidTable::instance().request_for(name, body, error)) {
        return Step::failed(error);
    }

    // The chord tolerance the engine flattens transcendental curves to, how
    // many longitudinal lines a curved face gets, and how finely it is
    // gridded. All three are display conventions; SOLIDMESH is a sysvar
    // because it is the one worth changing -- it makes a curved face readable
    // and it costs real time on a boolean.
    const int mesh = ctx.db.sysvars().get_int(Sysvar::SolidMesh);
    body += "TOL 0.01\nSEAMS 4\nMESH " + std::to_string(mesh) + "\nGO\n";

    std::vector<std::string> reply;
    if (!SolidEngine::instance().request(body, reply, error)) return Step::failed(error);

    std::vector<EntityPtr> made;
    std::vector<std::string> notes;
    if (!solids_wire_entities(reply, made, notes, error)) return Step::failed(error);

    const EntityProps props = ctx.db.current_props();
    std::vector<Handle> drawn;
    drawn.reserve(made.size());
    for (EntityPtr& e : made) {
        solids_apply_props(*e, props);
        drawn.push_back(ctx.db.add(std::move(e)));
    }
    SolidTable::instance().record_drawn(name, drawn);

    std::string msg = std::to_string(drawn.size()) + " wireframe entities added";
    msg += "  --  solid " + name;
    for (const std::string& n : notes) msg += "\n" + n;
    return Step::done(msg);
}

}  // namespace

Step SolidSweepCommand::run(CommandContext& ctx, const std::string& op) {
    std::string profile;
    std::string error;
    if (!solids_profile_request(ctx.db, handles_, profile, error)) {
        return Step::failed(error);
    }

    // Named rather than anonymous, so a boolean has something to refer to.
    // The name is assigned and REPORTED rather than asked for, which is this
    // program's idiom everywhere else -- a default computed for you and shown.
    SolidTable& table = SolidTable::instance();
    const std::string name = table.define(
        "SOLID @NAME@\n" + profile + op + "ENDSOLID\n", {});

    // Registered before the engine has seen it, because the request needs the
    // name -- so a refusal has to take it back out again. Leaving it in was a
    // real bug: a profile the engine rejected kept its name, and because the
    // whole tree is re-sent every time, EVERY later boolean that reached it
    // failed with the same complaint about geometry from minutes ago.
    Step step = deliver(ctx, name);
    if (step.kind != StepKind::Done) table.forget(name);
    return step;
}

// --- booleans ----------------------------------------------------------------

const char* SolidBooleanCommand::name() const {
    switch (op_) {
        case Op::Union: return "UNION";
        case Op::Subtract: return "SUBTRACT";
        case Op::Intersect: return "INTERSECT";
    }
    return "UNION";
}

namespace {

Prompt solid_prompt(const std::string& message) {
    Prompt p;
    p.kind = PromptKind::String;
    const std::vector<std::string> names = SolidTable::instance().names();
    std::string list;
    for (const std::string& n : names) {
        if (!list.empty()) list += " ";
        list += n;
    }
    p.message = message + " [" + list + "]";
    return p;
}

}  // namespace

Step SolidBooleanCommand::start(CommandContext& ctx) {
    (void)ctx;
    SolidEngine& engine = SolidEngine::instance();
    if (!engine.available()) return Step::failed(engine.why_unavailable());
    if (SolidTable::instance().names().size() < 2) {
        return Step::failed(
            "a boolean needs two solids -- build them with EXTRUDE or REVOLVE "
            "first, and SOLIDS lists what there is");
    }
    state_ = State::First;
    const char* what = op_ == Op::Subtract ? "Specify solid to subtract FROM"
                                           : "Specify first solid";
    return Step::ask(solid_prompt(what));
}

Step SolidBooleanCommand::next(CommandContext& ctx, const InputValue& value) {
    if (value.kind != InputKind::String && value.kind != InputKind::Keyword) {
        return Step::failed("a solid name is required");
    }
    std::string given = value.text;
    for (char& c : given) c = static_cast<char>(std::toupper(c));

    SolidTable& table = SolidTable::instance();
    if (!table.has(given)) {
        return Step::failed("there is no solid named \"" + given + "\"");
    }

    if (state_ == State::First) {
        first_ = given;
        state_ = State::Second;
        const char* what = op_ == Op::Subtract ? "Specify solid to subtract"
                                               : "Specify second solid";
        return Step::ask(solid_prompt(what));
    }

    if (given == first_) {
        return Step::failed("a solid cannot be combined with itself");
    }

    const char* verb = op_ == Op::Union ? "UNION"
                     : op_ == Op::Subtract ? "DIFF" : "INTER";
    const std::string name = table.define(
        std::string(verb) + " @NAME@ " + first_ + " " + given + "\n",
        {first_, given});

    // Deliver FIRST, consume the operands only if it worked. The other order
    // destroyed the operands' wireframe and then reported a failure, which is
    // the worst of both: nothing gained and something lost.
    Step done = deliver(ctx, name);
    if (done.kind != StepKind::Done) {
        table.forget(name);
        return done;
    }

    // Consumed, as AutoCAD's booleans consume theirs. The wireframe is
    // ordinary line work and the erase is journalled, so UNDO puts it back.
    const std::size_t gone = table.erase_drawn(ctx.db, first_)
                           + table.erase_drawn(ctx.db, given);
    if (gone > 0) {
        done.message += "\n" + std::to_string(gone) +
                        " entities from " + first_ + " and " + given + " erased";
    }
    return done;
}

// --- SOLIDS ------------------------------------------------------------------

Step SolidListCommand::start(CommandContext& ctx) {
    (void)ctx;
    const std::vector<std::string> names = SolidTable::instance().names();
    if (names.empty()) {
        return Step::done("No solids in this session.");
    }
    std::string msg = std::to_string(names.size()) + " solid(s):";
    for (const std::string& n : names) msg += "\n  " + n;
    msg += "\n\nThese live for this session only and are not saved.";
    return Step::done(msg);
}

Step SolidListCommand::next(CommandContext& ctx, const InputValue& value) {
    (void)ctx;
    (void)value;
    return Step::done();
}

}  // namespace ncad
