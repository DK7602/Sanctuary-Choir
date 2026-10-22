#include "sanctuary/Lyrics.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace sc
{
namespace
{
    // Words that make a text Latin (sung the church way).
    const std::set<std::string>& latinWords()
    {
        static const std::set<std::string> words {
            "kyrie", "eleison", "christe", "gloria", "in", "excelsis", "deo", "et", "terra", "pax", "hominibus",
            "bonae", "voluntatis", "laudamus", "te", "benedicimus", "adoramus", "glorificamus", "gratias", "agimus",
            "tibi", "propter", "magnam", "gloriam", "tuam", "domine", "deus", "rex", "caelestis", "pater", "omnipotens",
            "fili", "unigenite", "iesu", "jesu", "christe", "agnus", "dei", "filius", "patris", "qui", "tollis", "peccata",
            "mundi", "miserere", "nobis", "suscipe", "deprecationem", "nostram", "sedes", "ad", "dexteram", "quoniam",
            "tu", "solus", "sanctus", "dominus", "altissimus", "cum", "spiritu", "sancto", "amen", "credo", "unum",
            "benedictus", "venit", "nomine", "hosanna", "dona", "pacem", "requiem", "aeternam", "aeterna", "lux",
            "perpetua", "luceat", "eis", "ave", "maria", "gratia", "plena", "tecum", "benedicta", "mulieribus",
            "fructus", "ventris", "tui", "sancta", "mater", "ora", "pro", "peccatoribus", "nunc", "hora", "mortis",
            "nostrae", "alleluia", "alleluja", "regina", "caeli", "coeli", "laetare", "salve", "mater", "misericordiae",
            "vita", "dulcedo", "spes", "nostra", "veni", "emmanuel", "gaude", "gaudete", "magnificat", "anima", "mea",
            "jubilate", "iubilate", "exsultate", "exultate", "omnes", "gentes", "laudate", "dominum", "pange", "lingua",
            "adoro", "devote", "panis", "angelicus", "ubi", "caritas", "amor", "est", "ecce", "sacerdos", "magnus",
            "stabat", "dolorosa", "dies", "irae", "illa", "lacrimosa", "libera", "me", "lacrymosa", "osanna", "sanctissima",
            "virgo", "beata", "o", "salutaris", "hostia", "tantum", "ergo", "sacramentum", "non", "nobis", "sed",
            "nomini", "da", "laus", "honor", "virtus", "potestas", "saecula", "saeculorum", "per", "omnia", "secula",
            "spiritus", "corpus", "verum", "natum", "ex", "de", "mihi", "meus", "mea", "cor", "jesus", "cantate",
            "canticum", "novum", "psallite", "puer", "natus", "bethlehem", "adeste", "fideles", "venite", "adoremus",
        };
        return words;
    }

    // Common English words the spelling rules would get wrong (space-separated sounds).
    const std::map<std::string, std::string>& englishWords()
    {
        static const std::map<std::string, std::string> words {
            { "the", "th ah" }, { "a", "ah" }, { "to", "t oo" }, { "do", "d oo" }, { "who", "h oo" }, { "of", "ah v" },
            { "you", "y oo" }, { "your", "y oh r" }, { "our", "ah r" }, { "love", "l ah v" }, { "come", "k ah m" },
            { "one", "w ah n" }, { "some", "s ah m" }, { "heaven", "h eh v eh n" },
            { "christmas", "k r ee s m ah s" }, { "hallelujah", "h ah l eh l oo y ah" }, { "alleluia", "ah l eh l oo y ah" },
            { "lord", "l oh r d" }, { "is", "ee z" }, { "his", "h ee z" }, { "was", "w ah z" }, { "are", "ah r" },
            { "have", "h ah v" }, { "give", "g ee v" }, { "live", "l ee v" }, { "there", "th eh r" }, { "where", "w eh r" },
            { "were", "w eh r" }, { "said", "s eh d" }, { "been", "b ee n" }, { "done", "d ah n" }, { "none", "n ah n" },
            { "son", "s ah n" }, { "above", "ah b ah v" }, { "from", "f r ah m" }, { "what", "w ah t" }, { "wonderful", "w ah n d eh r f oo l" },
            { "angel", "eh n j eh l" }, { "angels", "eh n j eh l z" }, { "father", "f ah th eh r" }, { "mother", "m ah th eh r" },
            { "spirit", "s p ee r ee t" }, { "glory", "g l oh r ee" }, { "holy", "h oh l ee" }, { "noel", "n oh eh l" },
            { "jesus", "j ee z ah s" }, { "emmanuel", "eh m ah n oo eh l" }, { "now", "n ah" }, { "how", "h ah" }, { "amazing", "ah m eh z ee ng" },
            { "silent", "s ah l eh n t" }, { "night", "n ah t" }, { "light", "l ah t" }, { "child", "ch ah l d" },
            { "christ", "k r ah s t" }, { "evermore", "eh v eh r m oh r" },
        };
        return words;
    }

    bool isVowelLetter (char c) { return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y'; }

    /** One sound of a word, with where it is written. */
    struct Seg
    {
        bool vowel = false;
        VocalType v = VocalType::Ah;
        Phone c = Phone::Count;
        int from = 0;           // index in the word where it is written
        bool breakBefore = false;
    };

    void addC (std::vector<Seg>& out, Phone p, int at, bool brk)
    {
        Seg s;
        s.c = p;
        s.from = at;
        s.breakBefore = brk;
        out.push_back (s);
    }

    void addV (std::vector<Seg>& out, VocalType v, int at, bool brk)
    {
        Seg s;
        s.vowel = true;
        s.v = v;
        s.from = at;
        s.breakBefore = brk;
        out.push_back (s);
    }

    /** Sounds from the dictionary form ("l ah v"). */
    std::vector<Seg> fromSpec (const std::string& spec, int length)
    {
        static const std::map<std::string, Phone> cons {
            { "p", Phone::P }, { "b", Phone::B }, { "t", Phone::T }, { "d", Phone::D }, { "k", Phone::K }, { "g", Phone::G },
            { "f", Phone::F }, { "v", Phone::V }, { "s", Phone::S }, { "z", Phone::Z }, { "sh", Phone::Sh }, { "zh", Phone::Zh },
            { "h", Phone::H }, { "th", Phone::Th }, { "ch", Phone::Ch }, { "j", Phone::J }, { "m", Phone::M }, { "n", Phone::N },
            { "ng", Phone::Ng }, { "l", Phone::L }, { "r", Phone::R }, { "y", Phone::Y }, { "w", Phone::W } };
        static const std::map<std::string, VocalType> vows {
            { "ah", VocalType::Ah }, { "oh", VocalType::Oh }, { "oo", VocalType::Oo }, { "ee", VocalType::Ee }, { "eh", VocalType::Eh } };
        std::vector<Seg> out;
        std::string tok;
        std::vector<std::string> toks;
        for (char ch : spec + " ")
        {
            if (ch == ' ') { if (! tok.empty()) toks.push_back (tok); tok.clear(); }
            else tok += ch;
        }
        // Spread the written positions evenly so the syllables' text splits sensibly.
        for (size_t i = 0; i < toks.size(); ++i)
        {
            const int at = (int) ((size_t) length * i / std::max<size_t> (1, toks.size()));
            if (auto v = vows.find (toks[i]); v != vows.end()) addV (out, v->second, at, false);
            else if (auto c = cons.find (toks[i]); c != cons.end()) addC (out, c->second, at, false);
        }
        return out;
    }

    /** Sounds of one word (lower case letters, '-' = a syllable break the user wrote). */
    std::vector<Seg> sounds (const std::string& w, bool latin)
    {
        std::vector<Seg> out;
        const int n = (int) w.size();
        auto at = [&] (int i) { return i >= 0 && i < n ? w[(size_t) i] : '\0'; };
        auto frontVowel = [&] (int i) { const char c = at (i); return c == 'e' || c == 'i' || c == 'y'; };
        // The previous / next letter that is not a written break.
        auto prevLetter = [&] (int i) { for (int k = i - 1; k >= 0; --k) if (w[(size_t) k] != '-') return w[(size_t) k]; return '\0'; };
        auto nextLetter = [&] (int i) { for (int k = i + 1; k < n; ++k) if (w[(size_t) k] != '-') return w[(size_t) k]; return '\0'; };

        // English: a silent final e makes the vowel before it long.
        int silentE = -1;
        if (! latin && n >= 3 && w.back() == 'e' && ! isVowelLetter (w[(size_t) n - 2]) && w[(size_t) n - 2] != '-')
        {
            int vowels = 0;
            for (int i = 0; i < n - 1; ++i) vowels += isVowelLetter (w[(size_t) i]) ? 1 : 0;
            if (vowels > 0) silentE = n - 1;
        }
        int longVowel = -1;   // the vowel the silent e lengthens
        if (silentE > 0)
            for (int i = silentE - 2; i >= 0 && i >= silentE - 3; --i)
                if (isVowelLetter (w[(size_t) i]) && ! isVowelLetter (at (i - 1)))
                {
                    longVowel = i;
                    break;
                }

        bool brk = false;
        for (int i = 0; i < n;)
        {
            const char c = w[(size_t) i];
            if (c == '-') { brk = true; ++i; continue; }
            if (i == silentE) { ++i; continue; }
            const char nx = at (i + 1), pv = prevLetter (i);
            const bool b = brk;
            brk = false;
            // A doubled consonant is sung once, but splits the syllables (al-le).
            if (! isVowelLetter (c) && c == pv && c != 'c' && ! out.empty() && ! out.back().vowel)
            {
                bool vowelAfter = false;
                for (int k = i + 1; k < n && ! vowelAfter; ++k) vowelAfter = isVowelLetter (w[(size_t) k]) && k != silentE;
                if (vowelAfter) addC (out, out.back().c, i, b);   // sung where the syllables meet
                ++i;
                continue;
            }
            if (latin)
            {
                switch (c)
                {
                    case 'a': if (nx == 'e') { addV (out, VocalType::Eh, i, b); i += 2; } else if (nx == 'u') { addV (out, VocalType::Ah, i, b); i += 2; } else { addV (out, VocalType::Ah, i, b); ++i; } continue;
                    case 'o': if (nx == 'e') { addV (out, VocalType::Eh, i, b); i += 2; } else { addV (out, VocalType::Oh, i, b); ++i; } continue;
                    case 'e': addV (out, VocalType::Eh, i, b); ++i; continue;
                    case 'i':
                    case 'y':
                    {
                        const bool glide = isVowelLetter (nextLetter (i)) && (i == 0 || (isVowelLetter (pv) && pv != 'i'));
                        if (glide) addC (out, Phone::Y, i, b); else addV (out, VocalType::Ee, i, b);
                        ++i;
                        continue;
                    }
                    case 'u':
                        if ((pv == 'g' || pv == 'q') && isVowelLetter (nx)) addC (out, Phone::W, i, b);
                        else addV (out, VocalType::Oo, i, b);
                        ++i;
                        continue;
                    case 'j': addC (out, Phone::Y, i, b); ++i; continue;
                    case 'c':
                        if (nx == 'h') { addC (out, Phone::K, i, b); i += 2; }
                        else if (nx == 'c' && frontVowel (i + 2)) { addC (out, Phone::T, i, b); addC (out, Phone::Ch, i + 1, false); i += 2; }
                        else if (frontVowel (i + 1) || (nx == 'a' && at (i + 2) == 'e') || (nx == 'o' && at (i + 2) == 'e')) { addC (out, Phone::Ch, i, b); ++i; }
                        else { addC (out, Phone::K, i, b); ++i; }
                        continue;
                    case 'g':
                        if (frontVowel (i + 1) || (nx == 'a' && at (i + 2) == 'e')) addC (out, Phone::J, i, b);
                        else addC (out, Phone::G, i, b);
                        ++i;
                        continue;
                    case 'h': ++i; continue;   // silent in church Latin
                    case 'p': if (nx == 'h') { addC (out, Phone::F, i, b); i += 2; } else { addC (out, Phone::P, i, b); ++i; } continue;
                    case 'q': addC (out, Phone::K, i, b); ++i; continue;
                    case 's':
                        if (nx == 'c' && frontVowel (i + 2)) { addC (out, Phone::Sh, i, b); i += 2; }
                        else { addC (out, Phone::S, i, b); ++i; }
                        continue;
                    case 't':
                        if (nx == 'h') { addC (out, Phone::T, i, b); i += 2; }
                        else if (nx == 'i' && isVowelLetter (at (i + 2)) && pv != 's' && pv != 't' && pv != 'x') { addC (out, Phone::T, i, b); addC (out, Phone::S, i, false); ++i; }
                        else { addC (out, Phone::T, i, b); ++i; }
                        continue;
                    case 'x':
                        if (nx == 'c' && frontVowel (i + 2)) { addC (out, Phone::K, i, b); addC (out, Phone::Sh, i + 1, false); i += 2; }
                        else { addC (out, Phone::K, i, b); addC (out, Phone::S, i, false); ++i; }
                        continue;
                    case 'z': addC (out, Phone::Z, i, b); ++i; continue;
                    default: break;
                }
            }
            else
            {
                const std::string rest = w.substr ((size_t) i, 4);
                auto starts = [&rest] (const char* s) { return rest.rfind (s, 0) == 0; };
                if (isVowelLetter (c))
                {
                    const bool last = i == n - 1 || (silentE >= 0 && i == silentE - 1);
                    VocalType v = VocalType::Ah;
                    int len = 1;
                    if (starts ("eigh")) { v = VocalType::Eh; len = 4; }
                    else if (starts ("igh")) { v = VocalType::Ah; len = 3; }
                    else if (starts ("ee") || starts ("ea") || (starts ("ey") && i + 2 == n) || (starts ("ie") && i + 2 < n)) { v = VocalType::Ee; len = 2; }
                    else if (starts ("oo") || starts ("ue") || starts ("ew")) { v = VocalType::Oo; len = 2; }
                    else if (starts ("ou")) { v = VocalType::Ah; len = 2; }
                    else if (starts ("ow") || starts ("oa")) { v = VocalType::Oh; len = 2; }
                    else if (starts ("oi") || starts ("oy")) { v = VocalType::Oh; len = 2; }
                    else if (starts ("ai") || starts ("ay") || starts ("ei")) { v = VocalType::Eh; len = 2; }
                    else if (starts ("au") || starts ("aw") || starts ("aa")) { v = VocalType::Ah; len = 2; }
                    else if (c == 'y' && isVowelLetter (nx)) { addC (out, Phone::Y, i, b); ++i; continue; }
                    else
                    {
                        const bool lengthened = i == longVowel;
                        switch (c)
                        {
                            case 'a': v = lengthened ? VocalType::Eh : VocalType::Ah; break;
                            case 'e': v = last && n <= 3 ? VocalType::Ee : VocalType::Eh; break;
                            case 'i': v = lengthened ? VocalType::Ah : VocalType::Ee; break;
                            case 'o': v = VocalType::Oh; break;
                            case 'u': v = lengthened ? VocalType::Oo : VocalType::Ah; break;
                            default:  // y as a vowel: "my" sings Ah, "glory" Ee
                            {
                                int vowels = 0;
                                for (int k = 0; k < n; ++k) vowels += isVowelLetter (w[(size_t) k]) ? 1 : 0;
                                v = vowels == 1 ? VocalType::Ah : VocalType::Ee;
                                break;
                            }
                        }
                    }
                    addV (out, v, i, b);
                    i += len;   // vowel teams (and the "gh" of "igh") are one sound
                    continue;
                }
                if (starts ("ch")) { addC (out, Phone::Ch, i, b); i += 2; continue; }
                if (starts ("sh")) { addC (out, Phone::Sh, i, b); i += 2; continue; }
                if (starts ("th")) { addC (out, Phone::Th, i, b); i += 2; continue; }
                if (starts ("ph")) { addC (out, Phone::F, i, b); i += 2; continue; }
                if (starts ("wh")) { addC (out, Phone::W, i, b); i += 2; continue; }
                if (starts ("ck")) { addC (out, Phone::K, i, b); i += 2; continue; }
                if (starts ("gh")) { i += 2; continue; }
                if (starts ("qu")) { addC (out, Phone::K, i, b); addC (out, Phone::W, i + 1, false); i += 2; continue; }
                if (starts ("ng"))
                {
                    if (frontVowel (i + 2)) { addC (out, Phone::N, i, b); addC (out, Phone::J, i + 1, false); }
                    else addC (out, Phone::Ng, i, b);
                    i += 2;
                    continue;
                }
                switch (c)
                {
                    case 'c': addC (out, frontVowel (i + 1) ? Phone::S : Phone::K, i, b); ++i; continue;
                    case 'g': addC (out, frontVowel (i + 1) && nx != 'i' ? Phone::J : Phone::G, i, b); ++i; continue;
                    case 'j': addC (out, Phone::J, i, b); ++i; continue;
                    case 'x': addC (out, Phone::K, i, b); addC (out, Phone::S, i, false); ++i; continue;
                    case 'h':   // "oh", "ah", "Jah": silent after a vowel
                        if (! (isVowelLetter (pv) && (nx == '\0' || ! isVowelLetter (nx)))) addC (out, Phone::H, i, b);
                        ++i;
                        continue;
                    case 'w': addC (out, Phone::W, i, b); ++i; continue;
                    case 'q': addC (out, Phone::K, i, b); ++i; continue;
                    case 'z': addC (out, Phone::Z, i, b); ++i; continue;
                    case 's': addC (out, Phone::S, i, b); ++i; continue;
                    default: break;
                }
            }
            switch (c)
            {
                case 'b': addC (out, Phone::B, i, b); break;
                case 'd': addC (out, Phone::D, i, b); break;
                case 'f': addC (out, Phone::F, i, b); break;
                case 'k': addC (out, Phone::K, i, b); break;
                case 'l': addC (out, Phone::L, i, b); break;
                case 'm': addC (out, Phone::M, i, b); break;
                case 'n': addC (out, Phone::N, i, b); break;
                case 'p': addC (out, Phone::P, i, b); break;
                case 'r': addC (out, Phone::R, i, b); break;
                case 's': addC (out, Phone::S, i, b); break;
                case 't': addC (out, Phone::T, i, b); break;
                case 'v': addC (out, Phone::V, i, b); break;
                case 'w': addC (out, Phone::W, i, b); break;
                default: break;
            }
            ++i;
        }
        return out;
    }

    bool isStop (Phone p) { return p <= Phone::G; }

    /** Can a syllable start with a then b? */
    bool onsetPair (Phone a, Phone b)
    {
        if (a == b) return false;
        if ((isStop (a) || a == Phone::F) && (b == Phone::L || b == Phone::R))
            return ! ((a == Phone::T || a == Phone::D) && b == Phone::L);
        if (b == Phone::W || b == Phone::Y) return true;
        if (a == Phone::S && (b == Phone::P || b == Phone::T || b == Phone::K || b == Phone::M || b == Phone::N || b == Phone::L)) return true;
        if (a == Phone::T && b == Phone::S) return true;   // Latin "ti": gra-tsi-a
        if ((a == Phone::K || a == Phone::G || a == Phone::P) && b == Phone::N) return false;
        return false;
    }

    /** Lower-case ASCII, with accented Latin letters (and \u00e6, \u0153) spelled out, and
        alongside it the same letters as typed (for showing the syllables). */
    void fold (const std::string& in, std::string& folded, std::string& shown)
    {
        auto put = [&] (const char* f, char orig)
        {
            for (const char* c = f; *c != '\0'; ++c)
            {
                folded += *c;
                shown += orig != '\0' && c == f ? orig : *c;
            }
        };
        for (size_t i = 0; i < in.size(); ++i)
        {
            const unsigned char c = (unsigned char) in[i];
            if (c < 0x80)
            {
                const char lower[2] = { (char) std::tolower (c), '\0' };
                put (lower, (char) c);
                continue;
            }
            if (c == 0xC3 && i + 1 < in.size())
            {
                const unsigned char raw = (unsigned char) in[++i];
                const bool capital = raw < 0xA0;
                const unsigned char d = raw | 0x20;   // capitals fold onto small letters
                const char* f = " ";
                if (d >= 0xA0 && d <= 0xA5) f = "a";
                else if (d == 0xA6) f = "ae";
                else if (d == 0xA7) f = "c";
                else if (d >= 0xA8 && d <= 0xAB) f = "e";
                else if (d >= 0xAC && d <= 0xAF) f = "i";
                else if (d == 0xB1) f = "n";
                else if (d >= 0xB2 && d <= 0xB6) f = "o";
                else if (d >= 0xB9 && d <= 0xBC) f = "u";
                else if (d == 0xBD || d == 0xBF) f = "y";
                put (f, capital && f[0] != ' ' ? (char) std::toupper ((unsigned char) f[0]) : '\0');
                continue;
            }
            if (c == 0xC5 && i + 1 < in.size() && ((unsigned char) in[i + 1] == 0x92 || (unsigned char) in[i + 1] == 0x93))
            {
                put ("oe", (unsigned char) in[i + 1] == 0x92 ? 'O' : '\0');
                ++i;
                continue;
            }
            put (" ", '\0');
        }
    }
}

const char* phoneName (Phone p)
{
    static const char* names[] = { "p", "b", "t", "d", "k", "g", "f", "v", "s", "z", "sh", "zh", "h", "th", "ch", "j",
                                   "m", "n", "ng", "l", "r", "y", "w" };
    static_assert (sizeof (names) / sizeof (names[0]) == (size_t) Phone::Count);
    return p < Phone::Count ? names[(int) p] : "";
}

/** Where each syllable of a word starts (in sounds), and its vowels: a consonant between two
    vowels starts the next syllable (a-ve), of two or more the last one does (Sanc-tus) unless
    the last two can begin a syllable together (Chri-ste, gra-tsi-a). */
static std::vector<int> syllableStarts (const std::vector<Seg>& segs, std::vector<int>& nuclei)
{
    nuclei.clear();
    for (int i = 0; i < (int) segs.size(); ++i)
        if (segs[(size_t) i].vowel) nuclei.push_back (i);
    std::vector<int> starts { 0 };
    for (size_t k = 0; k + 1 < nuclei.size(); ++k)
    {
        const int a = nuclei[k] + 1, b = nuclei[k + 1];   // consonants between: [a, b)
        int split = -1;
        for (int i = a; i <= b; ++i)
            if (segs[(size_t) i].breakBefore) split = i;
        if (split < 0)
        {
            const int count = b - a;
            if (count <= 1) split = a;
            else
            {
                split = b - 1;
                if (onsetPair (segs[(size_t) b - 2].c, segs[(size_t) b - 1].c))
                {
                    split = b - 2;
                    if (count >= 3 && segs[(size_t) b - 3].c == Phone::S && isStop (segs[(size_t) b - 2].c))
                        split = b - 3;
                }
            }
        }
        starts.push_back (split);
    }
    starts.push_back ((int) segs.size());
    return starts;
}

//==============================================================================
Lyrics Lyrics::parse (const std::string& text)
{
    Lyrics lyr;
    // Words folded (for the rules) and as typed (for display); "_" holds.
    std::string folded, typed;
    fold (text, folded, typed);
    std::vector<std::pair<std::string, std::string>> words;   // folded, shown
    {
        std::string cur, shown;
        auto flush = [&] { if (! cur.empty()) words.push_back ({ cur, shown }); cur.clear(); shown.clear(); };
        for (size_t i = 0; i < folded.size(); ++i)
        {
            const char ch = folded[i];
            if (std::isalpha ((unsigned char) ch) || (ch == '-' && ! cur.empty())) { cur += ch; shown += typed[i]; }
            else if (ch == '_') { flush(); words.push_back ({ "_", "_" }); }
            else flush();
        }
        flush();
    }

    int latinCount = 0, counted = 0;
    for (const auto& [w, shown] : words)
    {
        if (w == "_") continue;
        std::string plain;
        for (char ch : w) if (ch != '-') plain += ch;
        ++counted;
        latinCount += latinWords().count (plain) > 0 ? 1 : 0;
    }
    lyr.latin = counted > 0 && latinCount * 2 >= counted;

    VocalType lastVowel = VocalType::Ah;
    for (const auto& [w, shown] : words)
    {
        if (w == "_")
        {
            Syllable s;
            s.text = "_";
            s.hold = true;
            s.vowel = lastVowel;
            if (! lyr.syl.empty())
            {
                // Holding moves the closing consonants onto the last note held.
                s.coda = lyr.syl.back().coda;
                s.numCoda = lyr.syl.back().numCoda;
                lyr.syl.back().numCoda = 0;
            }
            lyr.syl.push_back (s);
            continue;
        }
        std::string plain;
        for (char ch : w) if (ch != '-') plain += ch;
        std::vector<Seg> segs;
        bool fromDictionary = false;
        if (! lyr.latin)
            if (auto it = englishWords().find (plain); it != englishWords().end() && w.find ('-') == std::string::npos)
            {
                segs = fromSpec (it->second, (int) w.size());
                fromDictionary = true;
            }
        if (segs.empty())
            segs = sounds (w, lyr.latin);

        std::vector<int> nuclei;
        const auto starts = syllableStarts (segs, nuclei);
        if (nuclei.empty())
            continue;   // no vowel to sing ("hmm", "sh")
        // Where the written word splits: from the spelling rules when they find the same
        // syllables as the dictionary (Hal-le-lu-jah), else spread evenly.
        std::vector<int> textAt;
        {
            std::vector<int> ruleNuclei;
            const auto ruleSegs = segs.size() > 0 && fromDictionary ? sounds (w, lyr.latin) : segs;
            const auto ruleStarts = syllableStarts (ruleSegs, ruleNuclei);
            const bool useRules = ruleNuclei.size() == nuclei.size();
            for (size_t k = 0; k + 1 < starts.size(); ++k)
                textAt.push_back (k == 0 ? 0 : useRules ? ruleSegs[(size_t) ruleStarts[k]].from : segs[(size_t) starts[k]].from);
            textAt.push_back ((int) shown.size());
        }

        for (size_t k = 0; k + 1 < starts.size(); ++k)
        {
            Syllable s;
            s.wordStart = k == 0;
            const int a = starts[k], b = starts[k + 1];
            const int nucleus = nuclei[k];
            s.vowel = segs[(size_t) nucleus].v;
            for (int i = a; i < nucleus; ++i)
                if (s.numOnset < s.onset.size()) s.onset[s.numOnset++] = segs[(size_t) i].c;
            for (int i = nucleus + 1; i < b; ++i)
                if (s.numCoda < s.coda.size()) s.coda[s.numCoda++] = segs[(size_t) i].c;
            for (int i = textAt[k]; i < textAt[k + 1] && i < (int) shown.size(); ++i)
                if (shown[(size_t) i] != '-') s.text += shown[(size_t) i];
            lastVowel = s.vowel;
            lyr.syl.push_back (s);
        }
    }
    return lyr;
}

std::string Lyrics::hyphenated() const
{
    std::string out;
    for (size_t i = 0; i < syl.size(); ++i)
    {
        if (i > 0) out += syl[i].wordStart || syl[i].hold ? " " : "-";
        out += syl[i].text;
    }
    return out;
}

//==============================================================================
namespace
{
    using Segment = LyricScript::Segment;

    Segment seg (float seconds, float vowel, float lowpass = 0.0f, float noiseHz = 0.0f, float noiseQ = 1.0f, float noise = 0.0f)
    {
        Segment s;
        s.seconds = seconds;
        s.a.vowel = vowel;
        s.a.lowpass = lowpass;
        s.a.noiseHz = noiseHz;
        s.a.noiseQ = noiseQ;
        s.a.noise = noise;
        return s;
    }

    /** How each consonant is made: closure, burst, hiss, hum. Levels are against the voice. */
    template <size_t N>
    void addPhone (std::array<Segment, N>& out, uint8_t& count, Phone p)
    {
        auto add = [&] (const Segment& s) { if (count < N) out[count++] = s; };
        switch (p)
        {
            case Phone::P:  add (seg (0.045f, 0.0f)); add (seg (0.012f, 0.0f, 0, 1000, 0.8f, 0.9f)); add (seg (0.025f, 0.2f, 0, 1600, 0.5f, 0.35f)); break;
            case Phone::B:  add (seg (0.035f, 0.12f, 350)); add (seg (0.008f, 0.3f, 0, 900, 0.8f, 0.35f)); break;
            case Phone::T:  add (seg (0.04f, 0.0f)); add (seg (0.012f, 0.0f, 0, 4500, 1.2f, 1.0f)); add (seg (0.025f, 0.2f, 0, 3500, 0.6f, 0.35f)); break;
            case Phone::D:  add (seg (0.03f, 0.12f, 450)); add (seg (0.008f, 0.3f, 0, 3500, 1.0f, 0.4f)); break;
            case Phone::K:  add (seg (0.045f, 0.0f)); add (seg (0.015f, 0.0f, 0, 2200, 1.5f, 0.9f)); add (seg (0.03f, 0.2f, 0, 2000, 0.6f, 0.35f)); break;
            case Phone::G:  add (seg (0.035f, 0.12f, 400)); add (seg (0.01f, 0.3f, 0, 2000, 1.2f, 0.4f)); break;
            case Phone::F:  add (seg (0.08f, 0.0f, 0, 5000, 0.4f, 0.35f)); break;
            case Phone::V:  add (seg (0.06f, 0.35f, 1500, 4500, 0.5f, 0.15f)); break;
            case Phone::S:  add (seg (0.1f, 0.0f, 0, 7000, 2.0f, 0.8f)); break;
            case Phone::Z:  add (seg (0.07f, 0.35f, 1200, 6500, 2.0f, 0.35f)); break;
            case Phone::Sh: add (seg (0.1f, 0.0f, 0, 3000, 1.5f, 0.7f)); break;
            case Phone::Zh: add (seg (0.07f, 0.35f, 1200, 3000, 1.5f, 0.3f)); break;
            case Phone::H:  add (seg (0.06f, 0.0f, 0, 1400, 0.6f, 0.35f)); break;
            case Phone::Th: add (seg (0.07f, 0.0f, 0, 5500, 0.4f, 0.2f)); break;
            case Phone::Ch: add (seg (0.035f, 0.0f)); add (seg (0.06f, 0.0f, 0, 3000, 1.5f, 0.75f)); break;
            case Phone::J:  add (seg (0.03f, 0.12f, 400)); add (seg (0.05f, 0.3f, 1200, 3000, 1.5f, 0.35f)); break;
            case Phone::M:  add (seg (0.08f, 0.5f, 300)); break;
            case Phone::N:  add (seg (0.07f, 0.5f, 450)); break;
            case Phone::Ng: add (seg (0.07f, 0.5f, 400)); break;
            case Phone::L:  add (seg (0.05f, 0.65f, 1100)); break;
            case Phone::R:  add (seg (0.03f, 0.55f, 1400)); break;
            case Phone::Y:  add (seg (0.04f, 0.8f, 2500)); break;
            case Phone::W:  add (seg (0.05f, 0.65f, 800)); break;
            default: break;
        }
    }

    Articulation mix (const Articulation& a, const Articulation& b, float t)
    {
        auto lp = [] (float f) { return f <= 0.0f ? 20000.0f : f; };
        Articulation r;
        r.vowel = a.vowel + (b.vowel - a.vowel) * t;
        const float f = lp (a.lowpass) + (lp (b.lowpass) - lp (a.lowpass)) * t;
        r.lowpass = f >= 15000.0f ? 0.0f : f;
        // The noise keeps its colour while it fades in or out.
        r.noise = a.noise + (b.noise - a.noise) * t;
        const bool fromA = a.noise > 0.0f && (b.noise <= 0.0f || t < 0.5f);
        r.noiseHz = fromA ? a.noiseHz : b.noiseHz;
        r.noiseQ = fromA ? a.noiseQ : b.noiseQ;
        if (a.noise > 0.0f && b.noise > 0.0f)
        {
            r.noiseHz = a.noiseHz + (b.noiseHz - a.noiseHz) * t;
            r.noiseQ = a.noiseQ + (b.noiseQ - a.noiseQ) * t;
        }
        return r;
    }
}

LyricScript LyricScript::make (const Lyrics& lyrics)
{
    LyricScript script;
    script.text = lyrics.hyphenated();
    const auto& syl = lyrics.syllables();
    for (size_t i = 0; i < syl.size(); ++i)
    {
        const Syllable& s = syl[i];
        Entry e;
        e.vowel = s.vowel;
        e.hold = s.hold;
        if (! s.hold)
        {
            // The end of the syllable before (sung as this note starts), then this one's start.
            // A consonant both close and open (al-le) is sung once.
            const Syllable* prev = i > 0 ? &syl[i - 1] : nullptr;
            int skip = -1;
            if (prev != nullptr && prev->numCoda > 0 && s.numOnset > 0 && prev->coda[prev->numCoda - 1u] == s.onset[0])
                skip = prev->numCoda - 1;
            if (prev != nullptr)
                for (int k = 0; k < prev->numCoda; ++k)
                    if (k != skip)
                        addPhone (e.start, e.numStart, prev->coda[(size_t) k]);
            for (int k = 0; k < s.numOnset; ++k)
                addPhone (e.start, e.numStart, s.onset[(size_t) k]);
        }
        for (int k = 0; k < s.numCoda; ++k)
            addPhone (e.end, e.numEnd, s.coda[(size_t) k]);
        script.entries.push_back (e);
    }
    return script;
}

Articulation LyricScript::at (const Entry& e, float t, bool ending) noexcept
{
    const Segment* segs = ending ? e.end.data() : e.start.data();
    const int count = ending ? e.numEnd : e.numStart;
    constexpr float xf = 0.006f;
    Articulation prev;   // before the first consonant: the vowel still sounding
    float acc = 0.0f;
    for (int k = 0; k < count; ++k)
    {
        const Segment& s = segs[k];
        if (t < acc + s.seconds)
        {
            const float into = t - acc;
            const float w = std::min (xf, s.seconds * 0.5f);
            return into < w ? mix (prev, s.a, into / w) : s.a;
        }
        acc += s.seconds;
        prev = s.a;
    }
    // After the consonants: open into the vowel, or (the note let go on a consonant) fall
    // silent. With nothing to close it, a released vowel just dies away.
    Articulation after;
    if (count == 0)
        return after;
    if (ending)
    {
        after.vowel = 0.0f;
        return mix (prev, after, std::min (1.0f, (t - acc) / 0.02f));
    }
    return mix (prev, after, std::min (1.0f, (t - acc) / 0.025f));
}
} // namespace sc
