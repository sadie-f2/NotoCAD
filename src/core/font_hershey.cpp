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

#include "noto/font.hpp"

#include <cstddef>

namespace noto {
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
}

Glyph StrokeFont::glyph(unsigned char c) const {
    Glyph g;
    if (glyphs_.empty()) return g;

    const std::size_t idx = static_cast<std::size_t>(c) - kFontFirstChar;
    if (c < kFontFirstChar || idx >= glyphs_.size()) {
        // Unmappable: no ink, but keep the space's advance so that the rest of
        // the line does not shift left. A gap is an honest way to say "this
        // byte is not in the font"; dropping it silently is not.
        g.advance = glyphs_[0].advance;
        return g;
    }

    const Entry& e = glyphs_[idx];
    g.points = points_.data();
    g.stroke_begin = stroke_begin_.data() + e.first_stroke;
    g.stroke_count = e.stroke_count;
    g.advance = e.advance;
    return g;
}

double StrokeFont::width(const std::string& text) const {
    double w = 0.0;
    for (const char ch : text) w += glyph(static_cast<unsigned char>(ch)).advance;
    return w;
}

const StrokeFont& StrokeFont::romans() {
    static const StrokeFont font(kRowmansJhf);
    return font;
}

}  // namespace noto
