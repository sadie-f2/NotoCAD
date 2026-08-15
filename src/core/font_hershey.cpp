// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The bundled Hershey Roman Simplex, embedded and parsed on first use.
//
// ATTRIBUTION. Distributing this data requires the following acknowledgements
// to travel with it; see third_party/hershey/README.md and the verbatim notice
// in third_party/hershey/HERSHEY-NOTICE.txt.
//
//   - The Hershey Fonts were originally created by Dr. A. V. Hershey while
//     working at the U. S. National Bureau of Standards.
//   - The format of the font data in this distribution was originally created
//     by James Hurt, Cognition, Inc., 900 Technology Park Drive, Billerica,
//     MA 01821.
//
// The data is embedded rather than loaded from a file so that the core has no
// runtime data path: nothing has to be installed beside the executable, the
// headless tests need no fixtures, and a drawing cannot fail to render because
// a font went missing.

#include "ncad/font.hpp"

#include <cmath>
#include <cstddef>

namespace ncad {
namespace {

// The .jhf coordinate system, MEASURED from the data rather than assumed:
// y increases DOWNWARD, the baseline sits at raw y = 9, and the cap top at
// raw y = -12. Cap height is therefore 21 units.
//
// That divisor is what makes `Text::height()` mean what R12 says it means --
// cap height, not the em box. Getting it wrong would make every drawing's text
// the wrong size in a way that looks plausible.
constexpr double kBaselineRaw = 9.0;
constexpr double kUnitsPerCapHeight = 21.0;

// Every coordinate is a single character biased by 'R'.
constexpr int kCoordBias = 'R';

// Five characters of glyph number, three of vertex count, then the pairs. The
// shortest possible line is a glyph with only its side bearings -- the space.
constexpr std::size_t kCountField = 5;
constexpr std::size_t kBearingField = 8;
constexpr std::size_t kMinLine = 10;

// Hershey Roman Simplex (rowmans), 96 glyphs covering ASCII 32 upward, one per
// line. Byte-for-byte as vendored; see third_party/hershey/rowmans.jhf.
const char kRowmansJhf[] = R"JHF(
  699  1JZ
  714  9MWRFRT RRYQZR[SZRY
  717  6JZNFNM RVFVM
  733 12H]SBLb RYBRb RLOZO RKUYU
  719 27H\PBP_ RTBT_ RYIWGTFPFMGKIKKLMMNOOUQWRXSYUYXWZT[P[MZKX
 2271 32F^[FI[ RNFPHPJOLMMKMIKIIJGLFNFPGSHVHYG[F RWTUUTWTYV[X[ZZ[X[VYTWT
  734 35E_\O\N[MZMYNXPVUTXRZP[L[JZIYHWHUISJRQNRMSKSIRGPFNGMIMKNNPQUXWZY[[[\Z\Y
  731  8MWRHQGRFSGSIRKQL
  721 11KYVBTDRGPKOPOTPYR]T`Vb
  722 11KYNBPDRGTKUPUTTYR]P`Nb
 2219  9JZRFRR RMIWO RWIMO
  725  6E_RIR[ RIR[R
  711  9MWSZR[QZRYSZS\R^Q_
  724  3E_IR[R
  710  6MWRYQZR[SZRY
  720  3G][BIb
  700 18H\QFNGLJKOKRLWNZQ[S[VZXWYRYOXJVGSFQF
  701  5H\NJPISFS[
  702 15H\LKLJMHNGPFTFVGWHXJXLWNUQK[Y[
  703 16H\MFXFRNUNWOXPYSYUXXVZS[P[MZLYKW
  704  7H\UFKTZT RUFU[
  705 18H\WFMFLOMNPMSMVNXPYSYUXXVZS[P[MZLYKW
  706 24H\XIWGTFRFOGMJLOLTMXOZR[S[VZXXYUYTXQVOSNRNOOMQLT
  707  6H\YFO[ RKFYF
  708 30H\PFMGLILKMMONSOVPXRYTYWXYWZT[P[MZLYKWKTLRNPQOUNWMXKXIWGTFPF
  709 24H\XMWPURRSQSNRLPKMKLLINGQFRFUGWIXMXRWWUZR[P[MZLX
  712 12MWRMQNROSNRM RRYQZR[SZRY
  713 15MWRMQNROSNRM RSZR[QZRYSZS\R^Q_
 2241  4F^ZIJRZ[
  726  6E_IO[O RIU[U
 2242  4F^JIZRJ[
  715 21I[LKLJMHNGPFTFVGWHXJXLWNVORQRT RRYQZR[SZRY
 2273 56E`WNVLTKQKOLNMMPMSNUPVSVUUVS RQKOMNPNSOUPV RWKVSVUXVZV\T]Q]O\L[JYHWGTFQFNGLHJJILHOHRIUJWLYNZQ[T[WZYYZX RXKWSWUXV
  501  9I[RFJ[ RRFZ[ RMTWT
  502 24G\KFK[ RKFTFWGXHYJYLXNWOTP RKPTPWQXRYTYWXYWZT[K[
  503 19H]ZKYIWGUFQFOGMILKKNKSLVMXOZQ[U[WZYXZV
  504 16G\KFK[ RKFRFUGWIXKYNYSXVWXUZR[K[
  505 12H[LFL[ RLFYF RLPTP RL[Y[
  506  9HZLFL[ RLFYF RLPTP
  507 23H]ZKYIWGUFQFOGMILKKNKSLVMXOZQ[U[WZYXZVZS RUSZS
  508  9G]KFK[ RYFY[ RKPYP
  509  3NVRFR[
  510 11JZVFVVUYTZR[P[NZMYLVLT
  511  9G\KFK[ RYFKT RPOY[
  512  6HYLFL[ RL[X[
  513 12F^JFJ[ RJFR[ RZFR[ RZFZ[
  514  9G]KFK[ RKFY[ RYFY[
  515 22G]PFNGLIKKJNJSKVLXNZP[T[VZXXYVZSZNYKXIVGTFPF
  516 14G\KFK[ RKFTFWGXHYJYMXOWPTQKQ
  517 25G]PFNGLIKKJNJSKVLXNZP[T[VZXXYVZSZNYKXIVGTFPF RSWY]
  518 17G\KFK[ RKFTFWGXHYJYLXNWOTPKP RRPY[
  519 21H\YIWGTFPFMGKIKKLMMNOOUQWRXSYUYXWZT[P[MZKX
  520  6JZRFR[ RKFYF
  521 11G]KFKULXNZQ[S[VZXXYUYF
  522  6I[JFR[ RZFR[
  523 12F^HFM[ RRFM[ RRFW[ R\FW[
  524  6H\KFY[ RYFK[
  525  7I[JFRPR[ RZFRP
  526  9H\YFK[ RKFYF RK[Y[
 2223 12KYOBOb RPBPb ROBVB RObVb
  804  3KYKFY^
 2224 12KYTBTb RUBUb RNBUB RNbUb
 2262 11JZPLRITL RMORJWO RRJR[
  999  3JZJ]Z]
  730  8MWSFRGQIQKRLSKRJ
  601 18I\XMX[ RXPVNTMQMONMPLSLUMXOZQ[T[VZXX
  602 18H[LFL[ RLPNNPMSMUNWPXSXUWXUZS[P[NZLX
  603 15I[XPVNTMQMONMPLSLUMXOZQ[T[VZXX
  604 18I\XFX[ RXPVNTMQMONMPLSLUMXOZQ[T[VZXX
  605 18I[LSXSXQWOVNTMQMONMPLSLUMXOZQ[T[VZXX
  606  9MYWFUFSGRJR[ ROMVM
  607 23I\XMX]W`VaTbQbOa RXPVNTMQMONMPLSLUMXOZQ[T[VZXX
  608 11I\MFM[ RMQPNRMUMWNXQX[
  609  9NVQFRGSFREQF RRMR[
  610 12MWRFSGTFSERF RSMS^RaPbNb
  611  9IZMFM[ RWMMW RQSX[
  612  3NVRFR[
  613 19CaGMG[ RGQJNLMOMQNRQR[ RRQUNWMZM\N]Q][
  614 11I\MMM[ RMQPNRMUMWNXQX[
  615 18I\QMONMPLSLUMXOZQ[T[VZXXYUYSXPVNTMQM
  616 18H[LMLb RLPNNPMSMUNWPXSXUWXUZS[P[NZLX
  617 18I\XMXb RXPVNTMQMONMPLSLUMXOZQ[T[VZXX
  618  9KXOMO[ ROSPPRNTMWM
  619 18J[XPWNTMQMNNMPNRPSUTWUXWXXWZT[Q[NZMX
  620  9MYRFRWSZU[W[ ROMVM
  621 11I\MMMWNZP[S[UZXW RXMX[
  622  6JZLMR[ RXMR[
  623 12G]JMN[ RRMN[ RRMV[ RZMV[
  624  6J[MMX[ RXMM[
  625 10JZLMR[ RXMR[P_NaLbKb
  626  9J[XMM[ RMMXM RM[X[
 2225 40KYTBRCQDPFPHQJRKSMSOQQ RRCQEQGRISJTLTNSPORSTTVTXSZR[Q]Q_Ra RQSSUSWRYQZP\P^Q`RaTb
  723  3NVRBRb
 2226 40KYPBRCSDTFTHSJRKQMQOSQ RRCSESGRIQJPLPNQPURQTPVPXQZR[S]S_Ra RSSQUQWRYSZT\T^S`RaPb
 2246 24F^IUISJPLONOPPTSVTXTZS[Q RISJQLPNPPQTTVUXUZT[Q[O
  718 14KYQFOGNINKOMQNSNUMVKVIUGSFQF
)JHF";

}  // namespace

StrokeFont::StrokeFont(const char* jhf) {
    double deepest = 0.0;

    for (const char* p = jhf; *p != '\0';) {
        const char* line = p;
        std::size_t len = 0;
        while (line[len] != '\0' && line[len] != '\n') ++len;
        p = line + len + (line[len] == '\n' ? 1 : 0);

        // The leading newline of the raw literal, and any trailing blank.
        if (len < kMinLine) continue;

        int count = 0;
        for (std::size_t i = kCountField; i < kBearingField; ++i) {
            if (line[i] >= '0' && line[i] <= '9') count = count * 10 + (line[i] - '0');
        }
        if (count < 1) continue;

        // The first pair is not a point: it is the left and right side bearing,
        // which is where character advance comes from. Shifting x by the left
        // bearing puts the pen origin at x = 0.
        const int lb = line[kBearingField] - kCoordBias;
        const int rb = line[kBearingField + 1] - kCoordBias;

        Entry e;
        e.first_stroke = static_cast<std::uint32_t>(stroke_begin_.size());
        e.advance = static_cast<double>(rb - lb) / kUnitsPerCapHeight;

        std::size_t i = kBearingField + 2;
        bool open = false;
        for (int v = 1; v < count && i + 1 < len; ++v, i += 2) {
            const char cx = line[i];
            const char cy = line[i + 1];

            // " R" is a pen-up: end the current stroke and start a new one at
            // the next point rather than joining across the gap.
            if (cx == ' ' && cy == 'R') {
                open = false;
                continue;
            }

            if (!open) {
                // A stroke's begin is the previous stroke's end, because points
                // are stored contiguously -- which is what lets one array serve
                // as both.
                stroke_begin_.push_back(static_cast<std::uint32_t>(points_.size()));
                ++e.stroke_count;
                open = true;
            }

            const double x = static_cast<double>(cx - kCoordBias - lb) / kUnitsPerCapHeight;
            const double y = (kBaselineRaw - static_cast<double>(cy - kCoordBias)) / kUnitsPerCapHeight;
            points_.push_back(Vec3{x, y, 0.0});
            if (-y > deepest) deepest = -y;
        }

        // Closes the last stroke, so the glyph owns stroke_count + 1 offsets.
        stroke_begin_.push_back(static_cast<std::uint32_t>(points_.size()));
        glyphs_.push_back(e);
    }

    descender_ = deepest;

    // After the descender is measured, deliberately: the symbols are drawn to
    // sit on or above the baseline, and letting them into that measurement
    // would move every Bottom-justified string in every drawing.
    build_symbols();
}

void StrokeFont::build_symbols() {
    // Strokes are appended to the same pools the parse filled, so a symbol's
    // Glyph is an ordinary one -- same point array, same offsets, same rules.
    auto begin_stroke = [this](Entry& e) {
        stroke_begin_.push_back(static_cast<std::uint32_t>(points_.size()));
        ++e.stroke_count;
    };
    auto put = [this](double x, double y) { points_.push_back(Vec3{x, y, 0.0}); };

    // A circle as a closed run of segments. Sixteen is enough that a degree
    // sign at annotation size shows no facets, and these are drawn once.
    auto circle = [&](Entry& e, double cx, double cy, double r) {
        constexpr int kSegments = 16;
        begin_stroke(e);
        for (int i = 0; i <= kSegments; ++i) {
            const double t = 2.0 * 3.14159265358979323846 * static_cast<double>(i) /
                             static_cast<double>(kSegments);
            put(cx + r * std::cos(t), cy + r * std::sin(t));
        }
    };
    auto segment = [&](Entry& e, double x0, double y0, double x1, double y1) {
        begin_stroke(e);
        put(x0, y0);
        put(x1, y1);
    };

    // Each entry records where its strokes start before any are added, and the
    // pool is closed after, exactly as the parse loop does.
    auto open = [this](Entry& e) {
        e = Entry{};
        e.first_stroke = static_cast<std::uint32_t>(stroke_begin_.size());
    };
    auto close = [this]() {
        stroke_begin_.push_back(static_cast<std::uint32_t>(points_.size()));
    };

    // Degree: a small ring riding at cap height, narrow because it follows a
    // number and must not read as a zero.
    Entry& deg = symbols_[kSymbolDegree - kSymbolDegree];
    open(deg);
    deg.advance = 0.50;
    circle(deg, 0.25, 0.80, 0.15);
    close();

    // Diameter: a ring the size of a digit with a slash running clear of it on
    // both sides, which is what distinguishes it from a zero at a glance.
    Entry& dia = symbols_[kSymbolDiameter - kSymbolDegree];
    open(dia);
    dia.advance = 0.95;
    circle(dia, 0.48, 0.50, 0.36);
    segment(dia, 0.08, 0.10, 0.88, 0.90);
    close();

    // Plus-minus: the plus sits high and the bar below it, so the pair reads as
    // one symbol rather than as a plus followed by a hyphen.
    Entry& pm = symbols_[kSymbolPlusMinus - kSymbolDegree];
    open(pm);
    pm.advance = 1.00;
    segment(pm, 0.50, 0.34, 0.50, 0.86);
    segment(pm, 0.24, 0.60, 0.76, 0.60);
    segment(pm, 0.24, 0.10, 0.76, 0.10);
    close();
}

Glyph StrokeFont::glyph(std::uint16_t code) const {
    Glyph g;
    if (glyphs_.empty()) return g;

    const Entry* e = nullptr;
    if (code >= kSymbolDegree && code <= kSymbolPlusMinus) {
        e = &symbols_[code - kSymbolDegree];
    } else if (code >= kFontFirstChar) {
        const std::size_t idx = static_cast<std::size_t>(code) - kFontFirstChar;
        if (idx < glyphs_.size()) e = &glyphs_[idx];
    }

    if (e == nullptr) {
        // Unmappable: no ink, but keep the space's advance so that the rest of
        // the line does not shift left. A gap is an honest way to say "this
        // byte is not in the font"; dropping it silently is not.
        g.advance = glyphs_[0].advance;
        return g;
    }

    g.points = points_.data();
    g.stroke_begin = stroke_begin_.data() + e->first_stroke;
    g.stroke_count = e->stroke_count;
    g.advance = e->advance;
    return g;
}

double StrokeFont::width(const std::string& text) const {
    std::vector<TextCell> cells;
    decode_text(text, cells);

    double w = 0.0;
    for (const TextCell& c : cells) w += glyph(c.code).advance;
    return w;
}

void decode_text(const std::string& text, std::vector<TextCell>& out) {
    out.clear();
    out.reserve(text.size());

    bool over = false;
    bool under = false;

    for (std::size_t i = 0; i < text.size(); ++i) {
        // A code is `%%` and one more character, so a trailing `%%` at the very
        // end of the string is just two per-cent signs.
        if (text[i] == '%' && i + 2 < text.size() && text[i + 1] == '%') {
            const char raw = text[i + 2];
            const char c = (raw >= 'A' && raw <= 'Z') ? static_cast<char>(raw - 'A' + 'a') : raw;

            std::uint16_t code = 0;
            bool emit = true;
            bool consumed = true;

            switch (c) {
                case 'd': code = kSymbolDegree; break;
                case 'c': code = kSymbolDiameter; break;
                case 'p': code = kSymbolPlusMinus; break;
                case '%': code = '%'; break;

                // Toggles change the state of what FOLLOWS and draw nothing of
                // their own, which is why they produce no cell.
                case 'o':
                    over = !over;
                    emit = false;
                    break;
                case 'u':
                    under = !under;
                    emit = false;
                    break;

                default:
                    if (c >= '0' && c <= '9') {
                        // Up to three digits, greedily. R12 documents exactly
                        // three; taking fewer as well means `%%9` is a
                        // character rather than silently swallowing the text
                        // after it looking for digits that never come.
                        unsigned value = 0;
                        std::size_t j = i + 2;
                        std::size_t digits = 0;
                        while (digits < 3 && j < text.size() && text[j] >= '0' && text[j] <= '9') {
                            value = value * 10 + static_cast<unsigned>(text[j] - '0');
                            ++j;
                            ++digits;
                        }
                        // `%%nnn` names a byte, so anything past 255 names
                        // nothing. Code 0 is below the table and draws the same
                        // honest gap any unmappable byte does.
                        code = value <= 255 ? static_cast<std::uint16_t>(value) : 0;
                        out.push_back(TextCell{code, over, under});
                        i = j - 1;
                        continue;
                    }
                    // Not a code we know. Leave it literal rather than guess at
                    // how much of the string it was supposed to eat.
                    consumed = false;
                    break;
            }

            if (consumed) {
                if (emit) out.push_back(TextCell{code, over, under});
                i += 2;
                continue;
            }
        }

        out.push_back(TextCell{static_cast<std::uint16_t>(static_cast<unsigned char>(text[i])),
                               over, under});
    }
}

const StrokeFont& StrokeFont::romans() {
    static const StrokeFont font(kRowmansJhf);
    return font;
}

}  // namespace ncad
