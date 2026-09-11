// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The solids boundary: a separate process, reached over a text protocol.
//
// WHAT THIS DELIBERATELY IS NOT. There is no Solid entity type here and no
// TopoDS_Shape equivalent. EXTRUDE and REVOLVE send profile geometry across,
// get WIREFRAME CURVES back, and add ordinary LINE, CIRCLE, ARC, ELLIPSE and
// POLYLINE entities to the drawing. Everything downstream -- picking, object
// snap, DXF write, plotting, undo, blocks -- then works with no changes at
// all, because what is in the database is line work it already understands.
//
// The cost is that the result is BAKED. Change the profile and the solid does
// not follow; run the command again. That is the right first limitation to
// accept, because it defers every question this project has not answered --
// what a saved drawing contains, what happens on reopen, how a parametric
// relationship survives a process boundary -- while still proving the boundary,
// the protocol and the wireframe path against real geometry.
//
// The process split is `solids.md`'s, minus the two reasons that were about
// OCCT. What survives is that a long operation must be cancellable and a
// crashing engine must not take the drawing with it.
//
// `iperl.hpp` is the precedent for the mechanism and for the licence argument:
// running a program is not linking it, so nothing the engine depends on --
// Python, or anything it in turn uses -- reaches this binary. The default
// build links nothing new and stays BSD-3.
#pragma once

#include "ncad/command.hpp"
#include "ncad/commands.hpp"
#include "ncad/entity.hpp"

#include <map>
#include <string>
#include <vector>

namespace ncad {

class Database;

// Where the engine lives and whether it can be run.
//
// Looked up once and cached, because a command that cannot find the engine
// should say so immediately rather than paying for a failed fork per attempt.
class SolidEngine {
public:
    static SolidEngine& instance();

    bool available();

    // Empty when available(). Otherwise says what went wrong, in terms a user
    // can act on -- which is the whole reason this is a string and not a bool.
    const std::string& why_unavailable();

    // Sends one request and collects the reply. `reply` holds the lines
    // between OK and DONE; `error` carries the FAIL reason, and the engine's
    // own words are used verbatim because it knows more about the geometry
    // than this side does.
    bool request(const std::string& body, std::vector<std::string>& reply,
                 std::string& error);

    // For tests: point at a specific directory, bypassing discovery.
    void set_root(std::string dir);

private:
    SolidEngine() = default;
    void discover();

    bool discovered_{false};
    std::string root_;
    std::string why_;
};

// --- the two halves of the conversation -------------------------------------

// Selected entities into PROFILE lines. Refuses anything that is not profile
// geometry, by name, rather than dropping it silently -- a profile quietly
// missing a segment produces a solid nobody can explain later.
bool solids_profile_request(const Database& db, const std::vector<Handle>& handles,
                            std::string& out, std::string& error);

// Applies the drawing's current properties to an entity the engine produced,
// WITHOUT disturbing the plane it computed.
//
// Its own function because getting it wrong is invisible: assigning the whole
// EntityProps also assigns `normal`, which `current_props()` leaves at world
// Z -- so the solid stays right and the line work comes back flattened into
// the XY plane. A revolve about a Y axis drew its rings in XY; an extrude off
// a tilted UCS drew its caps there too.
void solids_apply_props(Entity& e, const EntityProps& props);

// Reply lines into entities. Notes come back separately for the command line.
bool solids_wire_entities(const std::vector<std::string>& reply,
                          std::vector<EntityPtr>& out,
                          std::vector<std::string>& notes, std::string& error);

// --- named solids -----------------------------------------------------------

// Session state: the solids this session has built, by name.
//
// A boolean needs two solids, and that is the first thing here that needs
// anything to be REMEMBERED. It is remembered on THIS side rather than in the
// engine, so the engine stays a pure function of its request -- the whole tree
// travels every time, and there is no session over there to go stale,
// disagree, or need invalidating.
//
// It is deliberately NOT in the drawing. Nothing here is written to DXF and
// nothing survives quitting: what the drawing holds is the wireframe, which it
// already understands. That keeps every question about persistence -- what a
// saved file contains, what reopening restores -- closed for now.
//
// Conceptually this belongs in `CommandMemory`, which is exactly "state that
// outlives the command without belonging to the drawing". It lives here
// instead so the solids work stays contained to one pair of files and can be
// reviewed, or reverted, on its own.
class SolidTable {
public:
    static SolidTable& instance();

    // Records a solid and returns the name assigned to it. Names are S1, S2,
    // ... and are shown rather than asked for: R12's idiom everywhere else is
    // a default computed for you and displayed, not a prompt.
    //
    // `body_template` carries @NAME@ where the assigned name goes, so the
    // caller never has to know the name before it exists.
    std::string define(const std::string& body_template,
                       std::vector<std::string> deps);

    // Undoes a define(). A solid the engine REFUSED must not stay in the
    // table: it would keep its name, and every later request that reached it
    // would fail with the same complaint about geometry the user has long
    // since moved on from. Rolls the numbering back too when it was the most
    // recent, so a failed attempt does not leave a hole in the names.
    void forget(const std::string& name);

    bool has(const std::string& name) const;
    std::vector<std::string> names() const;

    // The full request that builds `name`, with every solid it depends on
    // emitted first and exactly once.
    bool request_for(const std::string& name, std::string& out,
                     std::string& error) const;

    // Which entities this solid put in the drawing, so a boolean can consume
    // its operands the way AutoCAD's does.
    void record_drawn(const std::string& name, std::vector<Handle> handles);
    std::size_t erase_drawn(Database& db, const std::string& name);

    void clear();

private:
    struct Record {
        std::string body;
        std::vector<std::string> deps;
        std::vector<Handle> drawn;
    };

    bool collect(const std::string& name, std::vector<std::string>& order,
                 std::vector<std::string>& seen, std::string& error) const;

    std::map<std::string, Record> solids_;
    int next_{0};
};

// --- the commands -----------------------------------------------------------

// EXTRUDE and REVOLVE. One class, because they differ only in what they ask
// for after the selection, and writing them apart is how the profile handling
// gets implemented twice and agrees once.
class SolidSweepCommand final : public Command {
public:
    enum class Mode : std::uint8_t { Extrude, Revolve };

    explicit SolidSweepCommand(Mode mode) : mode_(mode) {}

    const char* name() const override {
        return mode_ == Mode::Extrude ? "EXTRUDE" : "REVOLVE";
    }

    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    enum class State : std::uint8_t {
        Selecting,
        Height,       // EXTRUDE
        AxisStart,    // REVOLVE
        AxisEnd,
        Angle,
    };

    Step run(CommandContext& ctx, const std::string& op);

    Mode mode_;
    State state_{State::Selecting};
    SelectionPrompter select_;
    std::vector<Handle> handles_;
    Vec3 axis_start_{};
    Vec3 axis_end_{};
};

// UNION, SUBTRACT and INTERSECT. One class again, because they differ only in
// the word sent across.
class SolidBooleanCommand final : public Command {
public:
    enum class Op : std::uint8_t { Union, Subtract, Intersect };

    explicit SolidBooleanCommand(Op op) : op_(op) {}

    const char* name() const override;
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    enum class State : std::uint8_t { First, Second };

    Op op_;
    State state_{State::First};
    std::string first_;
};

// SOLIDS: what this session has built, and what each one is.
class SolidListCommand final : public Command {
public:
    const char* name() const override { return "SOLIDS"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;
};

}  // namespace ncad
