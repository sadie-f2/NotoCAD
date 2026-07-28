// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The first commands. Three, chosen because they are different shapes of state
// machine rather than because they are the three most useful:
//
//   CIRCLE  a fixed sequence of prompts, then done
//   LINE    an unbounded loop with keywords and an undo history
//   ERASE   repeated selection terminated by Enter
//
// Between them they cover what the engine has to support. A fourth command that
// does not fit one of these shapes is a sign the abstraction is wrong.
#pragma once

#include "noto/command.hpp"
#include "noto/entities.hpp"
#include "noto/render.hpp"

#include <string_view>

namespace noto {

// LINE: first point, then next points until Enter. Close joins back to the
// start; Undo removes the last segment.
class LineCommand final : public Command {
public:
    const char* name() const override { return "LINE"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    Prompt next_prompt() const;

    Vec3 first_{};
    Vec3 previous_{};
    bool have_first_{false};
    std::vector<Vec3> vertices_;     // every point accepted so far
    std::vector<Handle> segments_;   // the entity for each segment, for Undo
};

// CIRCLE: centre, then radius. Diameter switches which the second answer means.
class CircleCommand final : public Command {
public:
    const char* name() const override { return "CIRCLE"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    enum class State : std::uint8_t { Centre, Radius };

    State state_{State::Centre};
    Vec3 centre_{};
    bool diameter_{false};
};

// PLINE: one polyline, grown a segment at a time.
//
// The longest state machine here, and all of it is R12's two prompts: a line
// mode offering Arc/Close/Halfwidth/Length/Undo/Width, and an arc sub-mode
// offering Angle/CEnter/CLose/Direction/Line/Radius/Second pt on top of the
// same width and undo options. Each arc option is a different way of saying
// the same thing -- an included angle -- so they all converge on one bulge.
//
// Unlike LINE, which adds one entity per segment, this builds a single entity
// and replaces it as it grows. That is what makes Undo a vertex pop rather than
// an entity erase, and it is why the polyline is added to the database as soon
// as it has two vertices: committed work has to survive Escape, exactly as it
// does in LINE, and an entity that only appears at the end would not.
//
// `Length` continues in the current direction -- along the last line segment,
// or tangent to the last arc, which is the case that makes it worth having.
class PlineCommand final : public Command {
public:
    const char* name() const override { return "PLINE"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    enum class State : std::uint8_t {
        First,
        Line,
        Arc,
        WidthStart,
        WidthEnd,
        HalfStart,
        HalfEnd,
        Length,
        ArcAngle,
        ArcAngleEnd,
        ArcCentre,
        ArcCentreEnd,
        ArcRadius,
        ArcRadiusEnd,
        ArcSecond,
        ArcSecondEnd,
        ArcDirection,
        ArcDirectionEnd,
    };

    Prompt line_prompt() const;
    Prompt arc_prompt() const;
    Prompt width_prompt(bool half, bool ending) const;

    // Ask the question the current mode asks. Every option that finishes its
    // job returns through here rather than rebuilding the prompt itself.
    Step resume();

    // Adds or replaces the entity so that what has been drawn so far is real.
    void flush(CommandContext& ctx);

    Step add_vertex(CommandContext& ctx, const Vec3& p, double included, bool is_arc);
    Step close_it(CommandContext& ctx, bool with_arc);
    Step undo_vertex(CommandContext& ctx);

    const Vec3& current() const { return vertices_.back().pos; }

    State state_{State::First};
    bool arc_mode_{false};

    std::vector<PolyVertex> vertices_;
    Handle handle_{kNullHandle};

    // The current widths, applied to each new segment as it is added.
    double start_width_{0.0};
    double end_width_{0.0};

    // The direction the next arc leaves the current point in. Set from the last
    // segment, and overridden by the arc sub-mode's Direction option.
    Vec3 tangent_{1, 0, 0};
    bool have_tangent_{false};

    // Answers collected by the two-part arc options.
    double pending_angle_{0.0};
    double pending_radius_{0.0};
    Vec3 pending_centre_{};
    Vec3 pending_second_{};
};

// POINT: one location, and nothing else.
//
// R12 draws it per PDMODE and PDSIZE, neither of which exists here, so it
// draws as a fixed cross. That is a rendering gap rather than a command one --
// see SF_todo.md.
class PointCommand final : public Command {
public:
    const char* name() const override { return "POINT"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;
};

// SOLID and 3DFACE: four corners, then four more, for as long as you keep
// giving them.
//
// One class for both, because the prompt sequence is identical and the entity
// class already is -- they differ in what the corners MEAN, which is settled by
// the type enum and shows up only at DXF write time, where SOLID converts to
// its own plane and 3DFACE does not.
//
// The continuation is the part worth getting right: after the fourth point R12
// asks for a third and fourth again, and the previous third and fourth become
// the new first and second. That is what makes it a strip rather than a series
// of unrelated quadrilaterals, and it is why the corner order is the famous
// across-the-shape one rather than a loop.
class SolidCommand final : public Command {
public:
    explicit SolidCommand(bool face3d = false) : face3d_(face3d) {}

    const char* name() const override { return face3d_ ? "3DFACE" : "SOLID"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    enum class State : std::uint8_t { First, Second, Third, Fourth };

    Prompt ask(const char* message, bool allow_empty) const;
    void emit(CommandContext& ctx);

    bool face3d_;
    State state_{State::First};
    Vec3 corner_[4]{};
    // How many quadrilaterals have been committed, which is what decides
    // whether Enter is "done" or "nothing drawn".
    std::size_t emitted_{0};
};

// TEXT.
//
// The entity has existed since phase 7's entity work; this is the command that
// makes one. R12's sequence is Justify/Style/<start point>, then height,
// rotation and the string itself.
//
// Style is absent: there is no STYLE table yet, so offering the option would be
// offering a question with one answer. Justify is present in full, which is why
// Text grew groups 72, 73 and 11 -- without somewhere to store the answer the
// option would be theatre.
//
// Align and Fit are the two modes that take a second point instead of a
// rotation, and both need the string before they can be resolved: Align derives
// the height from how much text has to span the distance, and Fit derives the
// width factor instead. So the string is collected first and the geometry is
// settled at the end, which is also the order R12 asks in.
class TextCommand final : public Command {
public:
    const char* name() const override { return "TEXT"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    enum class State : std::uint8_t {
        Start,
        JustifyOption,
        SecondPoint,  // Align and Fit
        Height,
        Rotation,
        Value,
    };

    Step ask_height();
    Step ask_rotation();
    Step ask_value();
    Step build(CommandContext& ctx, const std::string& value);

    State state_{State::Start};
    Vec3 start_{};
    Vec3 second_{};
    double height_{1.0};
    double rotation_{0.0};
    TextHAlign h_align_{TextHAlign::Left};
    TextVAlign v_align_{TextVAlign::Baseline};
};

// "Select objects:" -- the whole of it, in one place.
//
// A small state machine rather than a helper function, because Window and
// Crossing are not single answers: each asks for two corners, so the selection
// prompt has sub-prompts of its own. Every editing command delegates to this,
// which is what stops MOVE and ERASE quietly disagreeing about what All means
// or which corner order a crossing box wants.
//
// It needs a DrawContext to flatten entities against for the region tests. The
// tolerance only affects how finely curves are diced before being tested, so a
// default is fine for text-driven use; the viewport passes its own.
class SelectionPrompter {
public:
    // What to ask right now, given what has been collected so far.
    Prompt prompt(const CommandContext& ctx) const;

    enum class Result {
        Selecting,  // still collecting; ask prompt() again
        Finished,   // Enter: the selection is complete
        Rejected,   // not something a selection prompt accepts
    };

    Result feed(CommandContext& ctx, const InputValue& value);

    // Set by the viewport, which knows its own zoom and orientation. A window
    // is a screen-aligned box, so the axes it is built on are the view's, not
    // the world's -- these default to world XY, which is right for text-driven
    // selection and for plan view.
    void set_draw_context(const DrawContext& ctx) { draw_ = ctx; }
    void set_view_axes(const Vec3& ax, const Vec3& ay) {
        view_ax_ = ax;
        view_ay_ = ay;
    }

    // Non-empty when the last answer deserves an echo, R12-style "4 found".
    const std::string& note() const { return note_; }

private:
    enum class State : std::uint8_t { Selecting, FirstCorner, SecondCorner };

    void apply_region(CommandContext& ctx, const Vec3& a, const Vec3& b);

    State state_{State::Selecting};
    bool removing_{false};
    bool crossing_{false};
    Vec3 first_{};
    Vec3 view_ax_{1, 0, 0};
    Vec3 view_ay_{0, 1, 0};
    DrawContext draw_{};
    std::string note_;
};

// ERASE: select entities until Enter, then delete them all.
class EraseCommand final : public Command {
public:
    const char* name() const override { return "ERASE"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    SelectionPrompter select_;
};

// PEDIT: edit an existing polyline.
//
// R12's option list is Close/Join/Width/Edit vertex/Fit curve/Spline
// curve/Decurve/Undo/eXit. Three of those are deliberately absent, and for two
// different reasons:
//
//   Edit vertex is a nested prompt loop with ten options of its own -- a state
//   machine inside a state machine -- and the useful half of it is dragging
//   vertices, which wants the interactive grip work that is still outstanding.
//
//   Fit curve, Spline curve and Decurve are not command plumbing at all: they
//   need a curve representation on the polyline itself. R12 spells them as a
//   flag plus SPLINETYPE and SPLINESEGS, and Decurve has to restore the control
//   vertices, so the polyline must keep them alongside the fitted ones. That is
//   a storage decision of the same weight as the width one, and it belongs with
//   the spline note in SF_todo.md rather than being settled in passing here.
//
// What is here is the half that edits the polyline as it stands: Close/Open,
// Join, Width, Undo and eXit.
//
// Undo is PEDIT's own, not the drawing's: it steps back through this command's
// edits while the command is still running, which is why it keeps a stack of
// snapshots rather than leaning on UndoJournal. The whole PEDIT session is one
// entry in the drawing's undo, exactly as R12 has it.
class PeditCommand final : public Command {
public:
    const char* name() const override { return "PEDIT"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    enum class State : std::uint8_t { Select, Option, WidthValue, JoinSelect };

    Step ask_option(CommandContext& ctx);
    Polyline* target(CommandContext& ctx) const;

    // Snapshots the polyline so PEDIT's own Undo can step back.
    void push_snapshot(CommandContext& ctx);
    Step do_undo(CommandContext& ctx);

    Step do_join(CommandContext& ctx);

    State state_{State::Select};
    Handle handle_{kNullHandle};
    SelectionPrompter select_;
    std::vector<EntityPtr> snapshots_;
    std::string note_;
};

// MOVE and COPY: select, then a base point, then a displacement.
//
// One class for both, because they differ in exactly one line -- whether the
// entity is transformed in place or a transformed clone is added. Writing them
// twice would be writing the selection and displacement handling twice, and the
// second copy is where they would drift apart.
//
// The second prompt takes R12's "<displacement>" shortcut: Enter at the second
// point means the first point WAS the displacement vector, measured from the
// origin. It is a real R12 idiom and costs one branch.
//
// COPY also offers R12's Multiple: keep placing copies from the same base point
// until Enter. Modern AutoCAD made that the default and added a Mode option to
// get single-copy back; R12 puts it one keystroke away instead, and R12 is what
// this targets. Making it the default is a one-line change in start().
class MoveCommand : public Command {
public:
    explicit MoveCommand(bool copy = false) : copy_(copy) {}

    const char* name() const override { return copy_ ? "COPY" : "MOVE"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    enum class State : std::uint8_t { Selecting, Base, Displacement };

    Step apply(CommandContext& ctx, const Vec3& delta);

    bool copy_;
    State state_{State::Selecting};
    SelectionPrompter select_;
    Vec3 base_{};
    bool multiple_{false};
    std::size_t placed_{0};
};

// ROTATE, SCALE and MIRROR: select, then a base point, then the transform.
//
// All three act in the current construction plane, not in the view. That is not
// a simplification: in R12 you can only draw in the current plane, so a rotation
// needs no axis (it is the plane's normal through the base point) and a mirror
// needs no plane (it is the line you gave, extruded along that normal). Points
// and point pairs are the whole input, which is why these fit one shape.
//
// Without UCS the current plane is world XY. When UCS arrives this reads the
// plane from it and nothing else here changes.
class TransformCommand : public Command {
public:
    enum class Kind : std::uint8_t { Rotate, Scale, Mirror };

    explicit TransformCommand(Kind kind) : kind_(kind) {}

    const char* name() const override;
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    // Mirror asks for a second point of the axis rather than a magnitude, and
    // then whether to keep the original -- so it has one state the others skip.
    enum class State : std::uint8_t { Selecting, Base, Amount, MirrorSecond, MirrorDelete };

    Step apply(CommandContext& ctx, const Mat4& m, bool erase_originals);
    Prompt amount_prompt() const;

    Kind kind_;
    State state_{State::Selecting};
    SelectionPrompter select_;
    Vec3 base_{};
    Vec3 mirror_second_{};
};

// ROTATE3D: rotate about an arbitrary axis in space.
//
// The command ROTATE is a special case of -- ROTATE takes the construction
// plane's normal through a base point, this takes any axis at all. Everything
// underneath already existed: Mat4::rotation has taken an arbitrary axis since
// the first commit, and entities have carried an extrusion since the ECS work,
// so geometry deliberately put into an arbitrary plane serialises correctly.
//
// R12's axis options are Entity/Last/View/Xaxis/Yaxis/Zaxis/<2points>. Last is
// absent here: it has to outlive the command that set it, and CommandContext
// has nowhere for it to live yet. Recorded in SF_todo.md rather than bodged.
class Rotate3dCommand final : public Command {
public:
    const char* name() const override { return "ROTATE3D"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    enum class State : std::uint8_t {
        Selecting,
        AxisOption,
        SecondPoint,
        PointOnAxis,
        AxisEntity,
        Angle,
    };

    Step ask_angle();
    Step apply(CommandContext& ctx, double radians);

    State state_{State::Selecting};
    SelectionPrompter select_;

    // Which world direction a named axis option means, once a point is given.
    Vec3 named_axis_{};
    Vec3 origin_{};
    Vec3 direction_{};
};

// ARRAY: rectangular or polar, following R12's prompt sequence.
//
// The longest command here by some way, and it is all prompt sequencing --
// every item is one transform of the selection, so the geometry is the same
// translate-or-rotate the other editing commands use.
class ArrayCommand final : public Command {
public:
    const char* name() const override { return "ARRAY"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    enum class State : std::uint8_t {
        Selecting,
        Type,
        Rows,
        Columns,
        RowSpacing,
        ColumnSpacing,
        Centre,
        Count,
        Fill,
        RotateItems,
    };

    Step ask_rows();
    Step ask_columns();
    Step ask_spacing(bool rows);
    Step build_rectangular(CommandContext& ctx);
    Step build_polar(CommandContext& ctx);

    // One copy of the whole selection, transformed. The originals are left
    // alone; the item at index zero of an array is the original itself.
    std::size_t place(CommandContext& ctx, const Mat4& m);

    State state_{State::Selecting};
    SelectionPrompter select_;

    std::int32_t rows_{1};
    std::int32_t columns_{1};
    double row_spacing_{0.0};
    double column_spacing_{0.0};

    Vec3 centre_{};
    std::int32_t count_{0};
    double fill_{0.0};
};

// STRETCH: move the defining points that fell inside a crossing window, and
// leave the rest where they are.
//
// The command that needs more from a selection than a list of entities.
// Everything selected is being stretched; only some of each entity's points
// move, and which ones is answered by the crossing region -- which is why
// SelectionSet carries one.
//
// It only does anything interesting when the selection was made by crossing.
// With a plain Window, or with objects picked one at a time, every defining
// point of every selected entity is inside the selection, so "move the points
// that are inside" moves all of them and STRETCH becomes MOVE. R12 behaves that
// way, silently, and it is the classic reason the command looks broken. Here it
// still behaves that way -- but deliberately, and it says so in the message.
class StretchCommand final : public Command {
public:
    const char* name() const override { return "STRETCH"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    enum class State : std::uint8_t { Selecting, Base, Displacement };

    Step apply(CommandContext& ctx, const Vec3& delta);

    State state_{State::Selecting};
    SelectionPrompter select_;
    Vec3 base_{};
};

// BLOCK: turn selected entities into a named definition.
//
// The entities are REMOVED from the drawing, which is R12's behaviour and
// surprises people the first time: BLOCK is not "make a block from a copy of
// this", it is "this geometry is now a definition". OOPS brings it back in R12;
// here UNDO does, since the whole command is one group.
//
// Coordinates are stored relative to the base point, so the definition sits
// around its own origin and an insertion is a plain translation.
class BlockCommand final : public Command {
public:
    const char* name() const override { return "BLOCK"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    enum class State : std::uint8_t { Name, Base, Selecting };

    State state_{State::Name};
    std::string block_name_;
    Vec3 base_{};
    SelectionPrompter select_;
};

// INSERT and MINSERT: place a block.
//
// One class for both, because MINSERT is the same prompt sequence with two more
// questions at the end -- which is exactly how R12 spells it, and why the array
// lives on the INSERT entity rather than becoming separate copies.
//
// R12's corner scale option and its separate X/Y/Z scales are here; the
// "Preset" variants (PScale, PXscale...) are not, since they exist to support
// dragging the block during placement and nothing drags yet.
class InsertCommand final : public Command {
public:
    explicit InsertCommand(bool multiple = false) : multiple_(multiple) {}

    const char* name() const override { return multiple_ ? "MINSERT" : "INSERT"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    enum class State : std::uint8_t {
        Name,
        Point,
        Scale,
        YScale,
        Rotation,
        Rows,
        Columns,
        RowSpacing,
        ColumnSpacing,
    };

    Step ask_rotation();
    Step place(CommandContext& ctx);

    bool multiple_;
    State state_{State::Name};
    BlockId block_{kInvalidBlock};
    Vec3 point_{};
    Vec3 scale_{1, 1, 1};
    double rotation_{0.0};
    std::int16_t rows_{1};
    std::int16_t columns_{1};
    double row_spacing_{0.0};
    double column_spacing_{0.0};
};

// EXPLODE: replace a block reference with copies of what it draws.
//
// One level, as R12 does: exploding a block containing a block yields the inner
// references, not their contents. That is worth keeping rather than "fixing" --
// it is how you take a nested assembly apart deliberately instead of losing all
// its structure at once.
//
// A block inserted with unequal X and Y scales explodes anyway, approximating:
// a circle inside becomes a circle scaled by the X factor, because R12 has no
// ELLIPSE entity to become instead. That is the same approximation
// transform_frame() already documents for SCALE, and it is applied here rather
// than refusing, because refusing would leave no way to take the block apart.
class ExplodeCommand final : public Command {
public:
    const char* name() const override { return "EXPLODE"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    SelectionPrompter select_;
};

// WBLOCK: write a block, or the whole drawing, to its own DXF file.
//
// R12 writes a DWG; only DXF exists here, so this writes DXF for the same
// reason DXFIN is the command that opens a drawing.
//
// The `*` answer -- write the entire drawing -- is supported. The "write a
// selection set" form is not yet: it needs a temporary database to assemble
// into, which is a small thing but not the same thing.
class WblockCommand final : public Command {
public:
    const char* name() const override { return "WBLOCK"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    enum class State : std::uint8_t { File, Block };

    State state_{State::File};
    std::string path_;
};

// BASE: the insertion base point of the drawing itself, for when this drawing
// is inserted into another. R12 keeps it in INSBASE.
class BaseCommand final : public Command {
public:
    const char* name() const override { return "BASE"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;
};

// BREAK: remove the piece of a curve between two points.
//
// The first of phase 10's commands, and the simplest consumer of the
// intersection kernel's parameterisation -- which is why it is first. It needs
// no intersections at all; what it needs is the parameter machinery those
// intersections are reported in, so building it proves that half before TRIM
// depends on both.
//
// R12's prompt sequence is the odd part and is faithfully odd here: selecting
// the object also supplies the FIRST break point, because you point at the
// object by pointing somewhere on it. `F` then exists to say "that pick was
// only to choose the object, ask me again". Answering the second prompt with
// `@` breaks at a single point, splitting the curve without removing anything.
//
// A CIRCLE becomes an ARC, and a closed POLYLINE becomes an open one. Neither
// is a simplification: a loop with a piece missing is not a loop.
class BreakCommand final : public Command {
public:
    const char* name() const override { return "BREAK"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    enum class State : std::uint8_t { Select, Second, FirstAgain, SecondAfterFirst };

    Prompt second_prompt() const;
    Step apply(CommandContext& ctx, const Vec3& first, const Vec3& second, bool single);

    State state_{State::Select};
    Handle target_{kNullHandle};
    Vec3 first_{};
};

// TRIM and EXTEND: the two commands that need both halves of phase 10's
// foundation -- where curves meet, and what is left when one is cut or grown.
//
// One class for both, because they are the same command with the sign reversed.
// Each selects a set of edges, then picks objects one at a time until Enter, and
// each pick is answered by intersecting the picked object against every edge and
// acting on the parameters that come back. What differs is which side of the
// intersections is acted on: TRIM removes the stretch the pick falls in, EXTEND
// grows the end the pick is nearest to. Writing them apart would be writing the
// edge selection, the pick loop and the undo twice.
//
// THE PICK POINT IS THE ARGUMENT. Both commands are meaningless without it --
// "trim this line" has no answer until you say which piece, and a line crossing
// three edges has four pieces. That is what InputValue::has_point exists for,
// and a typed handle is refused here rather than guessed at.
//
// R12's Undo option steps back one object at a time within the command, the
// same shape PEDIT's own Undo has and for the same reason: the whole command is
// one entry in the drawing's undo.
//
// Intersections are true 3-space, as everywhere else here. R12 has no PROJMODE
// -- projecting the trim onto the current view arrived with R13 -- so two curves
// that merely cross on screen do not trim each other, and that is correct
// rather than a limitation.
class TrimCommand final : public Command {
public:
    explicit TrimCommand(bool extend = false) : extend_(extend) {}

    const char* name() const override { return extend_ ? "EXTEND" : "TRIM"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    enum class State : std::uint8_t { SelectingEdges, Picking };

    Prompt pick_prompt() const;
    Step act_on(CommandContext& ctx, Handle target, const Vec3& at);

    // Every parameter on `target` where a selected edge crosses it.
    void cut_parameters(CommandContext& ctx, const Entity& target,
                        std::vector<double>& out) const;

    bool extend_;
    State state_{State::SelectingEdges};
    SelectionPrompter select_;

    // The edges, kept as handles because the selection set is reused by the
    // pick loop and must not be disturbed by it.
    std::vector<Handle> edges_;

    // Snapshots for the command's own Undo, newest last. Each is what the
    // drawing held before one object was acted on.
    struct Applied {
        Handle target{kNullHandle};
        EntityPtr before;
        std::vector<Handle> added;
    };
    std::vector<Applied> history_;
    std::size_t changed_{0};
};

// UCS: define the frame that typed coordinates are read in.
//
// R12's options are Origin, ZAxis, 3point, Entity, View, X, Y, Z, Prev,
// Restore, Save, Del, ? and World. All of them resolve to the same thing -- an
// origin and two axes, in WORLD terms -- which is why the command is long but
// not deep: every option is a different way of arriving at one frame, and the
// frame is stored the same way whichever route it came by.
//
// Storing in world rather than relative to the previous UCS is what DXF does
// and is what keeps this honest: there is never a chain of frames to walk, and
// no way for one to drift out of step with another.
//
// X, Y and Z rotate the CURRENT system about its own axis, which is what makes
// them useful -- "now tilt this by 30 degrees" rather than "define a frame".
// Origin likewise moves the current one without reorienting it.
class UcsCommand final : public Command {
public:
    const char* name() const override { return "UCS"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    enum class State : std::uint8_t {
        Option,
        OriginPoint,
        ZAxisOrigin,
        ZAxisPoint,
        ThreeOrigin,
        ThreeXPoint,
        ThreeYPoint,
        EntityPick,
        RotateAngle,
        SaveName,
        RestoreName,
        DeleteName,
    };

    Step ask_option(CommandContext& ctx);
    Step adopt(CommandContext& ctx, Ucs u, const std::string& name = {});

    State state_{State::Option};
    // Which of X/Y/Z the pending rotation is about.
    Vec3 rotate_axis_{};
    Vec3 origin_{};
    Vec3 xpoint_{};
};

// UCSICON: whether the coordinate-system icon is shown, and where.
//
// The icon itself is a viewport concern and is not drawn yet. The variable is
// still worth setting, because it is drawing state that DXF carries and a
// setting silently dropped on save is worse than one not yet honoured.
class UcsIconCommand final : public Command {
public:
    const char* name() const override { return "UCSICON"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;
};

// LAYER: R12's table editor, as one prompt that loops until Enter.
//
// ?/Make/Set/New/ON/OFF/Color/Ltype/Freeze/Thaw. Most options take a layer name
// afterwards, and Color and Ltype take a value first -- so this is a small state
// machine rather than a switch, and it keeps asking until you leave.
class LayerCommand final : public Command {
public:
    const char* name() const override { return "LAYER"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    enum class State : std::uint8_t {
        Option,
        NameForMake,
        NameForSet,
        NameForNew,
        NameForOn,
        NameForOff,
        NameForFreeze,
        NameForThaw,
        ColorValue,
        NameForColor,
        LtypeValue,
        NameForLtype,
    };

    Step ask_option(CommandContext& ctx);
    Step ask_name(State next_state, const char* message);
    Step apply_to_names(CommandContext& ctx, const std::string& names);

    State state_{State::Option};
    std::int16_t pending_color_{7};
    std::string pending_ltype_;
    std::string report_;
};

// LTYPE: ?/Create/Load/Set.
//
// R12's Load reads definitions out of acad.lin. There is no such file here, so
// Load draws from a small built-in table of the standard R12 patterns instead --
// honest about what it is, and a .lin parser can replace it without the command
// changing. Create defines one inline from a pattern the user types.
class LtypeCommand final : public Command {
public:
    const char* name() const override { return "LTYPE"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    enum class State : std::uint8_t { Option, LoadName, SetName, CreateName, CreatePattern };

    Step ask_option(CommandContext& ctx);

    State state_{State::Option};
    std::string pending_name_;
    std::string report_;
};

// The standard R12 linetype definitions, as `acad.lin` would supply them.
// Returns false for a name that is not one of them.
bool builtin_linetype(std::string_view name, std::string& description,
                      std::vector<double>& pattern);

// COLOR, LTSCALE and LIMITS: one prompt each, straight onto a system variable.
//
// Thin by design. The state they set is already journalled and already reachable
// from getvar, so these commands are the R12 way of typing what (setvar ...)
// could also say.
class ColorCommand final : public Command {
public:
    const char* name() const override { return "COLOR"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;
};

class LtScaleCommand final : public Command {
public:
    const char* name() const override { return "LTSCALE"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;
};

class LimitsCommand final : public Command {
public:
    const char* name() const override { return "LIMITS"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    enum class State : std::uint8_t { Lower, Upper };
    State state_{State::Lower};
    Vec3 lower_{};
};

// PLAN: look straight down at the construction plane.
//
// R12 asks <Current UCS>/Ucs/World. Until UCS exists all three answers name the
// same plane, so all three are accepted and mean world -- the prompt is right
// from the start, and phase 12 fills in the difference rather than adding a
// question that was not there before.
class PlanCommand final : public Command {
public:
    const char* name() const override { return "PLAN"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;
};

// ZOOM and PAN.
//
// Both are transparent: R12 lets you type 'ZOOM at any prompt to look somewhere
// else without abandoning what you were doing. They qualify because they touch
// no drawing state -- which is the test for transparency, and why the registry
// decides it rather than whoever typed the apostrophe.
class ZoomCommand final : public Command {
public:
    const char* name() const override { return "ZOOM"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    enum class State : std::uint8_t { Option, WindowFirst, WindowSecond };

    State state_{State::Option};
    Vec3 first_{};
};

class PanCommand final : public Command {
public:
    const char* name() const override { return "PAN"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    bool have_first_{false};
    Vec3 first_{};
};

// Whether a command may be run inside another with R12's apostrophe form. True
// only for commands that change no drawing state: the view commands, and the
// inquiry ones that merely report.
bool command_is_transparent(std::string_view name);

// The inquiry commands: DIST, ID, AREA and LIST.
//
// They ask questions rather than change anything, which makes them the only
// commands here that write no undo entry -- and the quickest way to check that
// the geometry kernel agrees with what is on screen.
//
// The view commands they normally sit beside -- ZOOM, PAN, PLAN, VPOINT -- are
// not here: they need a Viewport, and CommandContext deliberately holds none.
// See SF_todo.md.
class DistCommand final : public Command {
public:
    const char* name() const override { return "DIST"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    bool have_first_{false};
    Vec3 first_{};
};

class IdCommand final : public Command {
public:
    const char* name() const override { return "ID"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;
};

// AREA: a sequence of points, or a single entity.
//
// R12 also has Add and Subtract modes for accumulating; those are not here.
class AreaCommand final : public Command {
public:
    const char* name() const override { return "AREA"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    enum class State : std::uint8_t { First, Next, Entity };

    Prompt point_prompt() const;

    State state_{State::First};
    std::vector<Vec3> points_;
};

// LIST: dump what the database holds for the selected entities.
class ListCommand final : public Command {
public:
    const char* name() const override { return "LIST"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    SelectionPrompter select_;
};

// DXFIN: read a DXF file, replacing the drawing.
//
// R12 distinguishes OPEN (a DWG) from DXFIN (a DXF). Only DXF exists here, so
// this is the one that reads a drawing, and OPEN is its alias rather than a
// separate command that would have nothing else to do.
class DxfInCommand final : public Command {
public:
    const char* name() const override { return "DXFIN"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;
};

// DXFOUT: prompt for a file name and write the drawing. In R12 this is a
// command, and it only lived as a LISP function because the command layer did
// not exist yet. The (dxfout ...) function stays -- scripts want it -- but this
// is the form a person types.
class DxfOutCommand final : public Command {
public:
    const char* name() const override { return "DXFOUT"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;
};

// Looks a command up by its FULL name, case-insensitively. Returns nullptr if
// unknown, which callers report rather than treating as a crash.
//
// Deliberately exact. Abbreviation resolution must not happen here: a string
// argument to (command ...) is a command name only if it matches exactly, and
// resolving prefixes would make (command "LINE" p1 p2 "C") start CIRCLE instead
// of closing the polyline.
// UNDO and REDO. Both complete in one step with no prompt: R12's UNDO takes a
// count and options, but plain "undo one thing" is what it does by default and
// is the whole of what exists here.
//
// Neither is itself undoable, and neither opens a group -- an undo step that
// undoes an undo is how a history turns into a maze. REDO is the inverse.
class UndoCommand final : public Command {
public:
    const char* name() const override { return "UNDO"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;
};

class RedoCommand final : public Command {
public:
    const char* name() const override { return "REDO"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;
};

CommandPtr make_command(std::string_view name);

// The registered command names, for help text and completion.
const std::vector<std::string>& command_names();

// An acad.pgp-style abbreviation. R12 shipped a table of these rather than
// deriving them, because the useful short forms are not always prefixes --
// COPY is CP once COPY and CIRCLE both want to be C.
struct CommandAlias {
    std::string alias;
    std::string name;
};

const std::vector<CommandAlias>& command_aliases();

// What a typed command name resolved to.
struct CommandMatch {
    std::string name;                     // canonical name, empty if unresolved
    bool ambiguous{false};                // the prefix matched more than one
    std::vector<std::string> candidates;  // all matches, when ambiguous

    bool ok() const { return !name.empty(); }
};

// Resolves what the user typed, in order: exact name, exact alias, then prefix.
//
// An ambiguous prefix still resolves rather than refusing -- pressing Enter is
// expected to commit to something. The winner is the shortest candidate, ties
// broken alphabetically. Shorter names are the more fundamental commands, so LI
// is LINE rather than LINETYPE and AR is ARC rather than ARRAY, which is the
// right answer for the common case.
//
// Deliberately not AutoCAD's modern behaviour, which ranks by how often you have
// used each command and so shifts underneath you. A command line worth building
// muscle memory against has to resolve the same way next month. Where the rule
// picks wrong, the alias table is the override.
//
// For interactive input only -- see make_command above.
CommandMatch resolve_command_name(std::string_view typed);

// The same resolution against an explicit table, so the rules can be tested
// without waiting for two real commands to share a prefix.
CommandMatch resolve_in(std::string_view typed, const std::vector<std::string>& names,
                        const std::vector<CommandAlias>& aliases);

}  // namespace noto
