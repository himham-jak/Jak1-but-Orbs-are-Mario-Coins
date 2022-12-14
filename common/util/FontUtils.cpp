/*!
 * @file FontUtils.cpp
 *
 * Code for handling text and strings in Jak 1's "large font" format.
 *
 * MAKE SURE THIS FILE IS ENCODED IN UTF-8!!! The various strings here depend on it.
 * Always verify the encoding if string detection suddenly goes awry.
 */

#include "FontUtils.h"

#include <algorithm>
#include <stdexcept>

#include "common/util/Assert.h"

#include "third-party/fmt/core.h"

namespace {

/*!
 * Is this a valid character for a hex number?
 */
bool hex_char(char c) {
  return !((c < '0' || c > '9') && (c < 'a' || c > 'f') && (c < 'A' || c > 'F'));
}

}  // namespace

const std::unordered_map<std::string, GameTextVersion> sTextVerEnumMap = {
    {"jak1-v1", GameTextVersion::JAK1_V1},
    {"jak1-v2", GameTextVersion::JAK1_V2}};

const std::string& get_text_version_name(GameTextVersion version) {
  for (auto& [name, ver] : sTextVerEnumMap) {
    if (ver == version) {
      return name;
    }
  }
  throw std::runtime_error(fmt::format("invalid text version {}", version));
}

GameTextFontBank::GameTextFontBank(GameTextVersion version,
                                   std::vector<EncodeInfo>* encode_info,
                                   std::vector<ReplaceInfo>* replace_info,
                                   std::unordered_set<char>* passthrus)
    : m_version(version),
      m_encode_info(encode_info),
      m_replace_info(replace_info),
      m_passthrus(passthrus) {
  std::sort(
      m_encode_info->begin(), m_encode_info->end(),
      [](const EncodeInfo& a, const EncodeInfo& b) { return a.bytes.size() > b.bytes.size(); });
  std::sort(
      m_replace_info->begin(), m_replace_info->end(),
      [](const ReplaceInfo& a, const ReplaceInfo& b) { return a.from.size() > b.from.size(); });
  (void)m_version;
}

/*!
 * Finds a remap info that best matches the byte sequence (is the longest match).
 */
const EncodeInfo* GameTextFontBank::find_encode_to_utf8(const char* in) const {
  const EncodeInfo* best_info = nullptr;
  for (auto& info : *m_encode_info) {
    if (info.bytes.size() == 0)
      continue;

    bool found = true;
    for (int i = 0; found && i < (int)info.bytes.size(); ++i) {
      if (uint8_t(in[i]) != info.bytes.at(i)) {
        found = false;
      }
    }

    if (found && (!best_info || info.chars.length() > best_info->chars.length())) {
      best_info = &info;
    }
  }
  return best_info;
}

/*!
 * Finds a remap info that best matches the character sequence (is the longest match).
 */
const EncodeInfo* GameTextFontBank::find_encode_to_game(const std::string& in, int off) const {
  const EncodeInfo* best_info = nullptr;
  for (auto& info : *m_encode_info) {
    if (info.chars.length() == 0)
      continue;

    bool found = true;
    for (int i = 0; found && i < (int)info.chars.length() && i + off < (int)in.size(); ++i) {
      if (in.at(i + off) != info.chars.at(i)) {
        found = false;
      }
    }

    if (found && (!best_info || info.chars.length() > best_info->chars.length())) {
      best_info = &info;
    }
  }
  return best_info;
}

/*!
 * Try to replace specific substrings with better variants.
 * These are for hiding confusing text transforms.
 */
std::string GameTextFontBank::replace_to_utf8(std::string& str) const {
  for (auto& info : *m_replace_info) {
    auto pos = str.find(info.from);
    while (pos != std::string::npos) {
      str.replace(pos, info.from.size(), info.to);
      pos = str.find(info.from, pos + info.to.size());
    }
  }
  return str;
}
std::string GameTextFontBank::replace_to_game(std::string& str) const {
  for (auto& info : *m_replace_info) {
    auto pos = str.find(info.to);
    while (pos != std::string::npos) {
      str.replace(pos, info.to.size(), info.from);
      pos = str.find(info.to, pos + info.from.size());
    }
  }
  return str;
}

std::string GameTextFontBank::encode_utf8_to_game(std::string& str) const {
  std::string new_str;

  for (int i = 0; i < (int)str.length();) {
    auto remap = find_encode_to_game(str, i);
    if (!remap) {
      new_str.push_back(str.at(i));
      i += 1;
    } else {
      for (auto b : remap->bytes) {
        new_str.push_back(b);
      }
      i += remap->chars.length();
    }
  }

  str = new_str;
  return str;
}

/*!
 * Turn a normal readable string into a string readable in the Jak 1 font encoding.
 */
std::string GameTextFontBank::convert_utf8_to_game(std::string str) const {
  replace_to_game(str);
  encode_utf8_to_game(str);
  return str;
}

/*!
 * Turn a normal readable string into a string readable in the in-game font encoding and converts
 * \cXX escape sequences
 */
std::string GameTextFontBank::convert_utf8_to_game_with_escape(const std::string& str) const {
  std::string newstr;

  for (size_t i = 0; i < str.size(); ++i) {
    auto c = str.at(i);
    if (c == '"') {
      newstr.push_back('"');
      i += 1;
    } else if (c == '\\') {
      if (i + 1 >= str.size()) {
        throw std::runtime_error("incomplete string escape code");
      }
      auto p = str.at(i + 1);
      if (p == 'c') {
        if (i + 3 >= str.size()) {
          throw std::runtime_error("incomplete string escape code");
        }
        auto first = str.at(i + 2);
        auto second = str.at(i + 3);
        if (!hex_char(first) || !hex_char(second)) {
          throw std::runtime_error("invalid character escape hex number");
        }
        char hex_num[3] = {first, second, '\0'};
        std::size_t end = 0;
        auto value = std::stoul(hex_num, &end, 16);
        if (end != 2) {
          throw std::runtime_error("invalid character escape");
        }
        ASSERT(value < 256);
        newstr.push_back(char(value));
        i += 3;
      } else if (p == '"') {
        newstr.push_back(p);
        i += 1;
      } else {
        throw std::runtime_error("unknown string escape code");
      }
    } else {
      newstr.push_back(c);
    }
  }

  replace_to_game(newstr);
  encode_utf8_to_game(newstr);
  return newstr;
}

/*!
 * Convert a string from the game-text font encoding to something normal.
 * Unprintable characters become escape sequences, including tab and newline.
 */
std::string GameTextFontBank::convert_game_to_utf8(const char* in) const {
  std::string result;
  while (*in) {
    auto remap = find_encode_to_utf8(in);
    if (remap != nullptr) {
      result.append(remap->chars);
      in += remap->bytes.size() - 1;
    } else if (((*in >= '0' && *in <= '9') || (*in >= 'A' && *in <= 'Z') ||
                m_passthrus->find(*in) != m_passthrus->end()) &&
               *in != '\\') {
      result.push_back(*in);
    } else if (*in == '\n') {
      result += "\\n";
    } else if (*in == '\t') {
      result += "\\t";
    } else if (*in == '\\') {
      result += "\\\\";
    } else if (*in == '"') {
      result += "\\\"";
    } else {
      result += fmt::format("\\c{:02x}", uint8_t(*in));
    }
    in++;
  }
  return replace_to_utf8(result);
}

static std::vector<EncodeInfo> s_encode_info_null = {};
static std::vector<ReplaceInfo> s_replace_info_null = {};

/*!
 * ===========================
 * GAME TEXT FONT BANK - JAK 1
 * ===========================
 * This font is used in:
 * - Jak & Daxter: The Precursor Legacy (Black Label)
 */

static std::unordered_set<char> s_passthrus = {'~', ' ', ',', '.', '-', '+', '(', ')',
                                               '!', ':', '?', '=', '%', '*', '/', '#',
                                               ';', '<', '>', '@', '[', '_'};

static std::vector<EncodeInfo> s_encode_info_jak1 = {
    // random
    {"??", {0x10}},      // caron
    {"`", {0x11}},      // grave accent
    {"'", {0x12}},      // apostrophe
    {"^", {0x13}},      // circumflex
    {"<TIL>", {0x14}},  // tilde
    {"??", {0x15}},      // umlaut
    {"??", {0x16}},      // numero/overring
    {"??", {0x17}},      // inverted exclamation mark
    {"??", {0x18}},      // inverted question mark

    {"???", {0x1a}},  // umi
    {"??", {0x1b}},   // aesc
    {"???", {0x1c}},  // kai
    {"??", {0x1d}},   // c-cedilla
    {"???", {0x1e}},  // gaku
    {"??", {0x1f}},   // eszett

    {"???", {0x24}},  // wa

    {"???", {0x26}},  // wo
    {"???", {0x27}},  // -n

    {"???", {0x5c}},  // iwa
    {"???", {0x5d}},  // kyuu
    {"???", {0x5e}},  // sora
    //{"???", {0x5f}},  // horu

    {"???", {0x60}},  // -wa
    {"???", {0x61}},  // utsu
    {"???", {0x62}},  // kashikoi
    {"???", {0x63}},  // mizuumi
    {"???", {0x64}},  // kuchi
    {"???", {0x65}},  // iku
    {"???", {0x66}},  // ai
    {"???", {0x67}},  // shi
    {"???", {0x68}},  // tera
    {"???", {0x69}},  // yama
    {"???", {0x6a}},  // mono
    {"???", {0x6b}},  // tokoro
    {"???", {0x6c}},  // kaku
    {"???", {0x6d}},  // shou
    {"???", {0x6e}},  // numa
    {"???", {0x6f}},  // ue
    {"???", {0x70}},  // shiro
    {"???", {0x71}},  // ba
    {"???", {0x72}},  // shutsu
    {"???", {0x73}},  // yami
    {"???", {0x74}},  // nokosu
    {"???", {0x75}},  // ki
    {"???", {0x76}},  // ya
    {"???", {0x77}},  // shita
    {"???", {0x78}},  // ie
    {"???", {0x79}},  // hi
    {"???", {0x7a}},  // hana
    {"???", {0x7b}},  // re
    {"??", {0x7c}},   // oe
    {"???", {0x7d}},  // ro

    {"???", {0x7f}},  // ao

    {"???", {0x90}},  // nakaguro
    {"???", {0x91}},  // dakuten
    {"???", {0x92}},  // handakuten
    {"???", {0x93}},  // chouompu
    {"???", {0x94}},  // nijuukagikakko left
    {"???", {0x95}},  // nijuukagikakko right
    // hiragana
    {"???", {0x96}},  // -a
    {"???", {0x97}},  // a
    {"???", {0x98}},  // -i
    {"???", {0x99}},  // i
    {"???", {0x9a}},  // -u
    {"???", {0x9b}},  // u
    {"???", {0x9c}},  // -e
    {"???", {0x9d}},  // e
    {"???", {0x9e}},  // -o
    {"???", {0x9f}},  // o
    {"???", {0xa0}},  // ka
    {"???", {0xa1}},  // ki
    {"???", {0xa2}},  // ku
    {"???", {0xa3}},  // ke
    {"???", {0xa4}},  // ko
    {"???", {0xa5}},  // sa
    {"???", {0xa6}},  // shi
    {"???", {0xa7}},  // su
    {"???", {0xa8}},  // se
    {"???", {0xa9}},  // so
    {"???", {0xaa}},  // ta
    {"???", {0xab}},  // chi
    {"???", {0xac}},  // sokuon
    {"???", {0xad}},  // tsu
    {"???", {0xae}},  // te
    {"???", {0xaf}},  // to
    {"???", {0xb0}},  // na
    {"???", {0xb1}},  // ni
    {"???", {0xb2}},  // nu
    {"???", {0xb3}},  // ne
    {"???", {0xb4}},  // no
    {"???", {0xb5}},  // ha
    {"???", {0xb6}},  // hi
    {"???", {0xb7}},  // hu
    {"???", {0xb8}},  // he
    {"???", {0xb9}},  // ho
    {"???", {0xba}},  // ma
    {"???", {0xbb}},  // mi
    {"???", {0xbc}},  // mu
    {"???", {0xbd}},  // me
    {"???", {0xbe}},  // mo
    {"???", {0xbf}},  // youon ya
    {"???", {0xc0}},  // ya
    {"???", {0xc1}},  // youon yu
    {"???", {0xc2}},  // yu
    {"???", {0xc3}},  // youon yo
    {"???", {0xc4}},  // yo
    {"???", {0xc5}},  // ra
    {"???", {0xc6}},  // ri
    {"???", {0xc7}},  // ru
    {"???", {0xc8}},  // re
    {"???", {0xc9}},  // ro
    {"???", {0xca}},  // -wa
    {"???", {0xcb}},  // wa
    {"???", {0xcc}},  // wo
    {"???", {0xcd}},  // -n
    // katakana
    {"???", {0xce}},  // -a
    {"???", {0xcf}},  // a
    {"???", {0xd0}},  // -i
    {"???", {0xd1}},  // i
    {"???", {0xd2}},  // -u
    {"???", {0xd3}},  // u
    {"???", {0xd4}},  // -e
    {"???", {0xd5}},  // e
    {"???", {0xd6}},  // -o
    {"???", {0xd7}},  // o
    {"???", {0xd8}},  // ka
    {"???", {0xd9}},  // ki
    {"???", {0xda}},  // ku
    {"???", {0xdb}},  // ke
    {"???", {0xdc}},  // ko
    {"???", {0xdd}},  // sa
    {"???", {0xde}},  // shi
    {"???", {0xdf}},  // su
    {"???", {0xe0}},  // se
    {"???", {0xe1}},  // so
    {"???", {0xe2}},  // ta
    {"???", {0xe3}},  // chi
    {"???", {0xe4}},  // sokuon
    {"???", {0xe5}},  // tsu
    {"???", {0xe6}},  // te
    {"???", {0xe7}},  // to
    {"???", {0xe8}},  // na
    {"???", {0xe9}},  // ni
    {"???", {0xea}},  // nu
    {"???", {0xeb}},  // ne
    {"???", {0xec}},  // no
    {"???", {0xed}},  // ha
    {"???", {0xee}},  // hi
    {"???", {0xef}},  // hu
    {"???", {0xf0}},  // he
    {"???", {0xf1}},  // ho
    {"???", {0xf2}},  // ma
    {"???", {0xf3}},  // mi
    {"???", {0xf4}},  // mu
    {"???", {0xf5}},  // me
    {"???", {0xf6}},  // mo
    {"???", {0xf7}},  // youon ya
    {"???", {0xf8}},  // ya
    {"???", {0xf9}},  // youon yu
    {"???", {0xfa}},  // yu
    {"???", {0xfb}},  // youon yo
    {"???", {0xfc}},  // yo
    {"???", {0xfd}},  // ra
    {"???", {0xfe}},  // ri
    {"???", {0xff}},  // ru
    // kanji 2
    {"???", {1, 0x01}},  // takara

    {"???", {1, 0x10}},  // ishi
    {"???", {1, 0x11}},  // aka
    {"???", {1, 0x12}},  // ato
    {"???", {1, 0x13}},  // kawa
    {"???", {1, 0x14}},  // ikusa
    {"???", {1, 0x15}},  // mura
    {"???", {1, 0x16}},  // tai
    {"???", {1, 0x17}},  // utena
    {"???", {1, 0x18}},  // osa
    {"???", {1, 0x19}},  // tori
    {"???", {1, 0x1a}},  // tei
    {"???", {1, 0x1b}},  // hora
    {"???", {1, 0x1c}},  // michi
    {"???", {1, 0x1d}},  // hatsu
    {"???", {1, 0x1e}},  // tobu
    {"???", {1, 0x1f}},  // fuku

    {"???", {1, 0xa0}},  // ike
    {"???", {1, 0xa1}},  // naka
    {"???", {1, 0xa2}},  // tou
    {"???", {1, 0xa3}},  // shima
    {"???", {1, 0xa4}},  // bu
    {"???", {1, 0xa5}},  // hou
    {"???", {1, 0xa6}},  // san
    {"???", {1, 0xa7}},  // kaerimiru
    {"???", {1, 0xa8}},  // chikara
    {"???", {1, 0xa9}},  // midori
    {"???", {1, 0xaa}},  // kishi
    {"???", {1, 0xab}},  // zou
    {"???", {1, 0xac}},  // tani
    {"???", {1, 0xad}},  // kokoro
    {"???", {1, 0xae}},  // mori
    {"???", {1, 0xaf}},  // mizu
    {"???", {1, 0xb0}},  // fune
    {"???", {1, 0xb1}},   // trademark
};

static std::vector<ReplaceInfo> s_replace_info_jak1 = {
    // other
    {"A~Y~-21H~-5V??~Z", "??"},
    {"N~Y~-6H??~Z~+10H", "N??"},

    // tildes
    {"N~Y~-22H~-4V<TIL>~Z", "??"},
    {"A~Y~-21H~-5V<TIL>~Z", "??"},  // custom
    {"O~Y~-22H~-4V<TIL>~Z", "??"},  // custom

    // acute accents
    {"A~Y~-21H~-5V'~Z", "??"},
    {"E~Y~-22H~-5V'~Z", "??"},
    {"I~Y~-19H~-5V'~Z", "??"},
    {"O~Y~-22H~-4V'~Z", "??"},
    {"U~Y~-24H~-3V'~Z", "??"},

    // circumflex
    {"A~Y~-20H~-4V^~Z", "??"},  // custom
    {"E~Y~-20H~-5V^~Z", "??"},
    {"I~Y~-19H~-5V^~Z", "??"},
    {"O~Y~-20H~-4V^~Z", "??"},  // custom
    {"U~Y~-24H~-3V^~Z", "??"},

    // grave accents
    {"A~Y~-21H~-5V`~Z", "??"},
    {"E~Y~-22H~-5V`~Z", "??"},
    {"I~Y~-19H~-5V`~Z", "??"},
    {"O~Y~-22H~-4V`~Z", "??"},  // custom
    {"U~Y~-24H~-3V`~Z", "??"},

    // umlaut
    {"A~Y~-21H~-5V??~Z", "??"},
    {"E~Y~-20H~-5V??~Z", "??"},
    {"I~Y~-19H~-5V??~Z", "??"},  // custom
    {"O~Y~-22H~-4V??~Z", "??"},
    {"O~Y~-22H~-3V??~Z", "??"},  // dumb
    {"U~Y~-22H~-3V??~Z", "??"},

    // dakuten katakana
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    // handakuten katakana
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    // dakuten hiragana
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    // handakuten hiragana
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    {"~Y???~Z???", "???"},
    // japanese punctuation
    {",~+8H", "???"},
    {"~+8H ", "???"},

    // (hack) special case kanji
    {"~~", "???"},

    // playstation buttons
    {"~Y~22L<~Z~Y~27L*~Z~Y~1L>~Z~Y~23L[~Z~+26H", "<PAD_X>"},
    {"~Y~22L<~Z~Y~26L;~Z~Y~1L>~Z~Y~23L[~Z~+26H", "<PAD_TRIANGLE>"},
    {"~Y~22L<~Z~Y~25L@~Z~Y~1L>~Z~Y~23L[~Z~+26H", "<PAD_CIRCLE>"},
    {"~Y~22L<~Z~Y~24L#~Z~Y~1L>~Z~Y~23L[~Z~+26H", "<PAD_SQUARE>"},  // custom
};

GameTextFontBank g_font_bank_jak1(GameTextVersion::JAK1_V1,
                                  &s_encode_info_jak1,
                                  &s_replace_info_jak1,
                                  &s_passthrus);

/*!
 * ================================
 * GAME TEXT FONT BANK - JAK 1 (v2)
 * ================================
 * This font is used in:
 * - Jak & Daxter: The Precursor Legacy (PAL)
 * - ?????????????????????????????????????????????????????????
 * - Jak & Daxter: The Precursor Legacy (NTSC-U v2)
 *
 * It is the same as v1, but _ has been fixed and no longer overlaps ???
 */

static std::vector<EncodeInfo> s_encode_info_jak1_v2 = {
    // random
    {"_", {0x03}},      // large space
    {"??", {0x10}},      // caron
    {"`", {0x11}},      // grave accent
    {"'", {0x12}},      // apostrophe
    {"^", {0x13}},      // circumflex
    {"<TIL>", {0x14}},  // tilde
    {"??", {0x15}},      // umlaut
    {"??", {0x16}},      // numero/overring
    {"??", {0x17}},      // inverted exclamation mark
    {"??", {0x18}},      // inverted question mark

    {"???", {0x1a}},  // umi
    {"??", {0x1b}},   // aesc
    {"???", {0x1c}},  // kai
    {"??", {0x1d}},   // c-cedilla
    {"???", {0x1e}},  // gaku
    {"??", {0x1f}},   // eszett

    {"???", {0x24}},  // wa

    {"???", {0x26}},  // wo
    {"???", {0x27}},  // -n

    {"???", {0x5c}},  // iwa
    {"???", {0x5d}},  // kyuu
    {"???", {0x5e}},  // sora
    {"???", {0x5f}},  // horu

    {"???", {0x60}},  // -wa
    {"???", {0x61}},  // utsu
    {"???", {0x62}},  // kashikoi
    {"???", {0x63}},  // mizuumi
    {"???", {0x64}},  // kuchi
    {"???", {0x65}},  // iku
    {"???", {0x66}},  // ai
    {"???", {0x67}},  // shi
    {"???", {0x68}},  // tera
    {"???", {0x69}},  // yama
    {"???", {0x6a}},  // mono
    {"???", {0x6b}},  // tokoro
    {"???", {0x6c}},  // kaku
    {"???", {0x6d}},  // shou
    {"???", {0x6e}},  // numa
    {"???", {0x6f}},  // ue
    {"???", {0x70}},  // shiro
    {"???", {0x71}},  // ba
    {"???", {0x72}},  // shutsu
    {"???", {0x73}},  // yami
    {"???", {0x74}},  // nokosu
    {"???", {0x75}},  // ki
    {"???", {0x76}},  // ya
    {"???", {0x77}},  // shita
    {"???", {0x78}},  // ie
    {"???", {0x79}},  // hi
    {"???", {0x7a}},  // hana
    {"???", {0x7b}},  // re
    {"??", {0x7c}},   // oe
    {"???", {0x7d}},  // ro

    {"???", {0x7f}},  // ao

    {"???", {0x90}},  // nakaguro
    {"???", {0x91}},  // dakuten
    {"???", {0x92}},  // handakuten
    {"???", {0x93}},  // chouompu
    {"???", {0x94}},  // nijuukagikakko left
    {"???", {0x95}},  // nijuukagikakko right
    // hiragana
    {"???", {0x96}},  // -a
    {"???", {0x97}},  // a
    {"???", {0x98}},  // -i
    {"???", {0x99}},  // i
    {"???", {0x9a}},  // -u
    {"???", {0x9b}},  // u
    {"???", {0x9c}},  // -e
    {"???", {0x9d}},  // e
    {"???", {0x9e}},  // -o
    {"???", {0x9f}},  // o
    {"???", {0xa0}},  // ka
    {"???", {0xa1}},  // ki
    {"???", {0xa2}},  // ku
    {"???", {0xa3}},  // ke
    {"???", {0xa4}},  // ko
    {"???", {0xa5}},  // sa
    {"???", {0xa6}},  // shi
    {"???", {0xa7}},  // su
    {"???", {0xa8}},  // se
    {"???", {0xa9}},  // so
    {"???", {0xaa}},  // ta
    {"???", {0xab}},  // chi
    {"???", {0xac}},  // sokuon
    {"???", {0xad}},  // tsu
    {"???", {0xae}},  // te
    {"???", {0xaf}},  // to
    {"???", {0xb0}},  // na
    {"???", {0xb1}},  // ni
    {"???", {0xb2}},  // nu
    {"???", {0xb3}},  // ne
    {"???", {0xb4}},  // no
    {"???", {0xb5}},  // ha
    {"???", {0xb6}},  // hi
    {"???", {0xb7}},  // hu
    {"???", {0xb8}},  // he
    {"???", {0xb9}},  // ho
    {"???", {0xba}},  // ma
    {"???", {0xbb}},  // mi
    {"???", {0xbc}},  // mu
    {"???", {0xbd}},  // me
    {"???", {0xbe}},  // mo
    {"???", {0xbf}},  // youon ya
    {"???", {0xc0}},  // ya
    {"???", {0xc1}},  // youon yu
    {"???", {0xc2}},  // yu
    {"???", {0xc3}},  // youon yo
    {"???", {0xc4}},  // yo
    {"???", {0xc5}},  // ra
    {"???", {0xc6}},  // ri
    {"???", {0xc7}},  // ru
    {"???", {0xc8}},  // re
    {"???", {0xc9}},  // ro
    {"???", {0xca}},  // -wa
    {"???", {0xcb}},  // wa
    {"???", {0xcc}},  // wo
    {"???", {0xcd}},  // -n
    // katakana
    {"???", {0xce}},  // -a
    {"???", {0xcf}},  // a
    {"???", {0xd0}},  // -i
    {"???", {0xd1}},  // i
    {"???", {0xd2}},  // -u
    {"???", {0xd3}},  // u
    {"???", {0xd4}},  // -e
    {"???", {0xd5}},  // e
    {"???", {0xd6}},  // -o
    {"???", {0xd7}},  // o
    {"???", {0xd8}},  // ka
    {"???", {0xd9}},  // ki
    {"???", {0xda}},  // ku
    {"???", {0xdb}},  // ke
    {"???", {0xdc}},  // ko
    {"???", {0xdd}},  // sa
    {"???", {0xde}},  // shi
    {"???", {0xdf}},  // su
    {"???", {0xe0}},  // se
    {"???", {0xe1}},  // so
    {"???", {0xe2}},  // ta
    {"???", {0xe3}},  // chi
    {"???", {0xe4}},  // sokuon
    {"???", {0xe5}},  // tsu
    {"???", {0xe6}},  // te
    {"???", {0xe7}},  // to
    {"???", {0xe8}},  // na
    {"???", {0xe9}},  // ni
    {"???", {0xea}},  // nu
    {"???", {0xeb}},  // ne
    {"???", {0xec}},  // no
    {"???", {0xed}},  // ha
    {"???", {0xee}},  // hi
    {"???", {0xef}},  // hu
    {"???", {0xf0}},  // he
    {"???", {0xf1}},  // ho
    {"???", {0xf2}},  // ma
    {"???", {0xf3}},  // mi
    {"???", {0xf4}},  // mu
    {"???", {0xf5}},  // me
    {"???", {0xf6}},  // mo
    {"???", {0xf7}},  // youon ya
    {"???", {0xf8}},  // ya
    {"???", {0xf9}},  // youon yu
    {"???", {0xfa}},  // yu
    {"???", {0xfb}},  // youon yo
    {"???", {0xfc}},  // yo
    {"???", {0xfd}},  // ra
    {"???", {0xfe}},  // ri
    {"???", {0xff}},  // ru
    // kanji 2
    {"???", {1, 0x01}},  // takara

    {"???", {1, 0x10}},  // ishi
    {"???", {1, 0x11}},  // aka
    {"???", {1, 0x12}},  // ato
    {"???", {1, 0x13}},  // kawa
    {"???", {1, 0x14}},  // ikusa
    {"???", {1, 0x15}},  // mura
    {"???", {1, 0x16}},  // tai
    {"???", {1, 0x17}},  // utena
    {"???", {1, 0x18}},  // osa
    {"???", {1, 0x19}},  // tori
    {"???", {1, 0x1a}},  // tei
    {"???", {1, 0x1b}},  // hora
    {"???", {1, 0x1c}},  // michi
    {"???", {1, 0x1d}},  // hatsu
    {"???", {1, 0x1e}},  // tobu
    {"???", {1, 0x1f}},  // fuku

    {"???", {1, 0xa0}},  // ike
    {"???", {1, 0xa1}},  // naka
    {"???", {1, 0xa2}},  // tou
    {"???", {1, 0xa3}},  // shima
    {"???", {1, 0xa4}},  // bu
    {"???", {1, 0xa5}},  // hou
    {"???", {1, 0xa6}},  // san
    {"???", {1, 0xa7}},  // kaerimiru
    {"???", {1, 0xa8}},  // chikara
    {"???", {1, 0xa9}},  // midori
    {"???", {1, 0xaa}},  // kishi
    {"???", {1, 0xab}},  // zou
    {"???", {1, 0xac}},  // tani
    {"???", {1, 0xad}},  // kokoro
    {"???", {1, 0xae}},  // mori
    {"???", {1, 0xaf}},  // mizu
    {"???", {1, 0xb0}},  // fune
    {"???", {1, 0xb1}},   // trademark
};

GameTextFontBank g_font_bank_jak1_v2(GameTextVersion::JAK1_V2,
                                     &s_encode_info_jak1_v2,
                                     &s_replace_info_jak1,
                                     &s_passthrus);

GameTextFontBank g_font_bank_jak2(GameTextVersion::JAK2,
                                  &s_encode_info_null,
                                  &s_replace_info_null,
                                  &s_passthrus);

/*!
 * ========================
 * GAME TEXT FONT BANK LIST
 * ========================
 * The list of available font banks and a couple of helper functions.
 */

std::map<GameTextVersion, GameTextFontBank*> g_font_banks = {
    {GameTextVersion::JAK1_V1, &g_font_bank_jak1},
    {GameTextVersion::JAK1_V2, &g_font_bank_jak1_v2},
    {GameTextVersion::JAK2, &g_font_bank_jak2}};

const GameTextFontBank* get_font_bank(GameTextVersion version) {
  return g_font_banks.at(version);
}

const GameTextFontBank* get_font_bank(const std::string& name) {
  if (auto it = sTextVerEnumMap.find(name); it == sTextVerEnumMap.end()) {
    throw std::runtime_error(fmt::format("unknown text version {}", name));
  } else {
    return get_font_bank(it->second);
  }
}

bool font_bank_exists(GameTextVersion version) {
  return g_font_banks.find(version) != g_font_banks.cend();
}
