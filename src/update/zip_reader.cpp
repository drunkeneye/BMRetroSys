#include "zip_reader.h"

#include "fat_path_policy.h"
#include "sha256.h"

#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_MALLOC
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_DEFLATE_APIS
#define MINIZ_NO_ZLIB_APIS
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "../../third_party/miniz/miniz.h"

#include <string.h>

namespace bmx {
namespace update {
namespace {

static const uint32_t kEndSignature = UINT32_C(0x06054b50);
static const uint32_t kCentralSignature = UINT32_C(0x02014b50);
static const uint32_t kLocalSignature = UINT32_C(0x04034b50);
static const uint32_t kDescriptorSignature = UINT32_C(0x08074b50);
static const uint16_t kUtf8Flag = UINT16_C(1) << 11U;
// Lenient browsing additionally tolerates these general-purpose bits:
// 1-2 (deflate speed hints, informational) and 3 (data descriptor: sizes
// live after the data, central sizes stay authoritative). Bit 0/6
// (encryption), 5 (patch) and reserved bits stay rejected: undecryptable.
static const uint16_t kLenientFlagMask =
    (UINT16_C(1) << 1U) | (UINT16_C(1) << 2U) | (UINT16_C(1) << 3U);
static const size_t kEndRecordSize = 22U;
static const size_t kCentralHeaderSize = 46U;
static const size_t kLocalHeaderSize = 30U;

static_assert(kZipMaximumPathBytes == kFatReleasePathMaximumBytes,
              "ZIP and FAT release path limits must remain identical");

uint16_t ReadU16(const uint8_t *bytes)
{
    return static_cast<uint16_t>(bytes[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(bytes[1]) << 8U);
}

uint32_t ReadU32(const uint8_t *bytes)
{
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8U) |
           (static_cast<uint32_t>(bytes[2]) << 16U) |
           (static_cast<uint32_t>(bytes[3]) << 24U);
}

bool CheckedAdd(uint64_t left, uint64_t right, uint64_t *result)
{
    if (result == 0 || left > UINT64_MAX - right) {
        return false;
    }
    *result = left + right;
    return true;
}

bool RangeWithin(uint64_t offset, uint64_t length, uint64_t boundary)
{
    uint64_t end = 0U;
    return CheckedAdd(offset, length, &end) && end <= boundary;
}

bool HashU64(Sha256 *hash, uint64_t value)
{
    if (hash == 0) {
        return false;
    }
    uint8_t encoded[8];
    for (size_t index = 0U; index < sizeof(encoded); ++index) {
        encoded[index] = static_cast<uint8_t>(value >> (index * 8U));
    }
    return hash->Update(encoded, sizeof(encoded));
}

bool HashSizedBytes(Sha256 *hash, const void *bytes, size_t size)
{
    return HashU64(hash, size) && hash->Update(bytes, size);
}

uint8_t AsciiLower(uint8_t value)
{
    if (value >= static_cast<uint8_t>('A') &&
        value <= static_cast<uint8_t>('Z')) {
        return static_cast<uint8_t>(value +
                                    (static_cast<uint8_t>('a') -
                                     static_cast<uint8_t>('A')));
    }
    return value;
}

bool FatEqual(const char *left, const char *right)
{
    if (left == 0 || right == 0) {
        return false;
    }
    size_t index = 0U;
    for (;; ++index) {
        const uint8_t left_byte =
            AsciiLower(static_cast<uint8_t>(left[index]));
        const uint8_t right_byte =
            AsciiLower(static_cast<uint8_t>(right[index]));
        if (left_byte != right_byte) {
            return false;
        }
        if (left_byte == 0U) {
            return true;
        }
    }
}

bool FatPrefixDirectory(const char *parent, const char *child)
{
    if (parent == 0 || child == 0) {
        return false;
    }
    size_t index = 0U;
    while (parent[index] != '\0') {
        if (child[index] == '\0' ||
            AsciiLower(static_cast<uint8_t>(parent[index])) !=
                AsciiLower(static_cast<uint8_t>(child[index]))) {
            return false;
        }
        ++index;
    }
    return child[index] == '/';
}

bool IsValidUtf8(const uint8_t *bytes, size_t size)
{
    size_t position = 0U;
    while (position < size) {
        const uint8_t first = bytes[position++];
        if (first < 0x80U) {
            continue;
        }
        uint32_t code_point = 0U;
        size_t continuation_count = 0U;
        if (first >= 0xc2U && first <= 0xdfU) {
            code_point = static_cast<uint32_t>(first & 0x1fU);
            continuation_count = 1U;
        } else if (first >= 0xe0U && first <= 0xefU) {
            code_point = static_cast<uint32_t>(first & 0x0fU);
            continuation_count = 2U;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            code_point = static_cast<uint32_t>(first & 0x07U);
            continuation_count = 3U;
        } else {
            return false;
        }
        if (continuation_count > size - position) {
            return false;
        }
        for (size_t index = 0U; index < continuation_count; ++index) {
            const uint8_t next = bytes[position++];
            if ((next & 0xc0U) != 0x80U) {
                return false;
            }
            code_point = (code_point << 6U) |
                         static_cast<uint32_t>(next & 0x3fU);
        }
        if ((continuation_count == 1U && code_point < 0x80U) ||
            (continuation_count == 2U && code_point < 0x800U) ||
            (continuation_count == 3U && code_point < 0x10000U) ||
            (code_point >= 0xd800U && code_point <= 0xdfffU) ||
            code_point > 0x10ffffU) {
            return false;
        }
    }
    return true;
}

ZipStatus ValidateAndCopyPath(const uint8_t *raw,
                              size_t raw_size,
                              uint16_t flags,
                              bool is_directory,
                              size_t maximum_path_bytes,
                              char destination[kZipMaximumPathBytes + 1U])
{
    if (raw == 0 || destination == 0 || raw_size == 0U) {
        return ZipStatus::PathInvalid;
    }
    bool has_non_ascii = false;
    for (size_t index = 0U; index < raw_size; ++index) {
        if (raw[index] == 0U) {
            return ZipStatus::PathInvalid;
        }
        if (raw[index] >= 0x80U) {
            has_non_ascii = true;
        }
    }
    if (has_non_ascii) {
        if ((flags & kUtf8Flag) == 0U || !IsValidUtf8(raw, raw_size)) {
            return ZipStatus::PathEncodingInvalid;
        }
        // BMX release paths are intentionally restricted to the FAT-stable
        // ASCII subset. Without a Unicode normalization/case-folding database
        // on the boot image, accepting arbitrary UTF-8 would make collision
        // checks filesystem-dependent and therefore unsafe.
        return ZipStatus::NonAsciiPathUnsupported;
    }

    const bool trailing_slash = raw[raw_size - 1U] == '/';
    if (trailing_slash != is_directory) {
        return ZipStatus::DirectoryMetadataInvalid;
    }
    const size_t normalized_size = trailing_slash ? raw_size - 1U : raw_size;
    const FatPathValidationStatus path_status = ValidateFatRelativePathBytes(
        raw, normalized_size, maximum_path_bytes);
    if (path_status == FatPathValidationStatus::PathTooLong) {
        return ZipStatus::PathTooLong;
    }
    if (path_status != FatPathValidationStatus::Ok) {
        return ZipStatus::PathInvalid;
    }

    memcpy(destination, raw, normalized_size);
    destination[normalized_size] = '\0';
    return ZipStatus::Ok;
}

bool IsDirectoryMetadata(uint8_t creator_system,
                         uint32_t external_attributes,
                         bool name_is_directory,
                         ZipStatus *status)
{
    const uint16_t unix_mode =
        static_cast<uint16_t>((external_attributes >> 16U) & 0xffffU);
    const uint16_t unix_type = static_cast<uint16_t>(unix_mode & 0170000U);
    if (creator_system == 3U && unix_type == 0120000U) {
        *status = ZipStatus::SpecialFileForbidden;
        return false;
    }
    if (creator_system == 3U && unix_type != 0U &&
        unix_type != 0100000U && unix_type != 0040000U) {
        *status = ZipStatus::SpecialFileForbidden;
        return false;
    }
    const bool by_unix_mode = creator_system == 3U && unix_type == 0040000U;
    const bool by_dos_attribute = (external_attributes & 0x10U) != 0U;
    if ((by_unix_mode || by_dos_attribute) != name_is_directory) {
        *status = ZipStatus::DirectoryMetadataInvalid;
        return false;
    }
    *status = ZipStatus::Ok;
    return true;
}

namespace {

uint32_t Crc32TableEntry(size_t index)
{
    uint32_t value = static_cast<uint32_t>(index);
    for (unsigned bit = 0U; bit < 8U; ++bit) {
        if ((value & UINT32_C(1)) != 0U) {
            value = (value >> 1U) ^ UINT32_C(0xedb88320);
        } else {
            value >>= 1U;
        }
    }
    return value;
}

const uint32_t *Crc32Table()
{
    // Same polynomial as the old bitwise loop (identical digests), built
    // once: ~15x faster on ARM, which turns CRC from the bottleneck into
    // noise. Single-threaded use only, matching the rest of the reader.
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (size_t index = 0U; index < 256U; ++index) {
            table[index] = Crc32TableEntry(index);
        }
        ready = true;
    }
    return table;
}

}  // namespace

uint32_t UpdateCrc32(uint32_t crc, const uint8_t *bytes, size_t size)
{
    const uint32_t *table = Crc32Table();
    uint32_t value = crc;
    for (size_t index = 0U; index < size; ++index) {
        value = table[(value ^ bytes[index]) & UINT32_C(0xff)] ^
                (value >> 8U);
    }
    return value;
}

bool DigestEqual(const uint8_t left[kSha256DigestBytes],
                 const uint8_t right[kSha256DigestBytes])
{
    uint8_t difference = 0U;
    for (size_t index = 0U; index < kSha256DigestBytes; ++index) {
        difference = static_cast<uint8_t>(difference |
                                          (left[index] ^ right[index]));
    }
    return difference == 0U;
}

bool ContainsSignature(SeekableZipSource *source,
                       uint64_t begin,
                       uint64_t end,
                       const uint8_t signature[4],
                       bool *found)
{
    if (source == 0 || signature == 0 || found == 0 || begin > end) {
        return false;
    }
    *found = false;
    uint8_t buffer[4096U + 3U];
    size_t prefix = 0U;
    uint64_t offset = begin;
    while (offset < end) {
        const uint64_t remaining = end - offset;
        const size_t amount = static_cast<size_t>(
            remaining < 4096U ? remaining : UINT64_C(4096));
        if (!source->ReadAt(offset, buffer + prefix, amount)) {
            return false;
        }
        const size_t available = prefix + amount;
        if (available >= 4U) {
            for (size_t index = 0U; index + 4U <= available; ++index) {
                if (memcmp(buffer + index, signature, 4U) == 0) {
                    *found = true;
                    return true;
                }
            }
        }
        prefix = available < 3U ? available : 3U;
        if (prefix != 0U) {
            memmove(buffer, buffer + available - prefix, prefix);
        }
        offset += amount;
    }
    return true;
}

bool FindLastSignature(SeekableZipSource *source,
                       uint64_t begin,
                       uint64_t end,
                       const uint8_t signature[4],
                       bool *found,
                       uint64_t *last_offset)
{
    if (source == 0 || signature == 0 || found == 0 || last_offset == 0 ||
        begin > end) {
        return false;
    }
    *found = false;
    *last_offset = 0U;
    uint8_t buffer[4096U + 3U];
    size_t prefix = 0U;
    uint64_t offset = begin;
    while (offset < end) {
        const uint64_t remaining = end - offset;
        const size_t amount = static_cast<size_t>(
            remaining < 4096U ? remaining : UINT64_C(4096));
        if (!source->ReadAt(offset, buffer + prefix, amount)) {
            return false;
        }
        const size_t available = prefix + amount;
        if (available >= 4U) {
            for (size_t index = 0U; index + 4U <= available; ++index) {
                if (memcmp(buffer + index, signature, 4U) == 0) {
                    *found = true;
                    *last_offset = offset - prefix + index;
                }
            }
        }
        prefix = available < 3U ? available : 3U;
        if (prefix != 0U) {
            memmove(buffer, buffer + available - prefix, prefix);
        }
        offset += amount;
    }
    return true;
}

}  // namespace

ZipLimits::ZipLimits()
    : maximum_archive_bytes(UINT64_C(512) * 1024U * 1024U),
      maximum_entries(kZipMaximumEntries),
      maximum_path_bytes(kZipMaximumPathBytes),
      maximum_file_bytes(UINT64_C(128) * 1024U * 1024U),
      maximum_installed_bytes(UINT64_C(512) * 1024U * 1024U),
      maximum_compression_ratio(200U)
{
}

ZipHashSink::~ZipHashSink()
{
}

ZipExtractSink::~ZipExtractSink()
{
}

ZipReader::ZipReader()
    : source_(0), entries_(0), inventory_(), limits_(), workspace_(0),
      open_(false), lenient_extra_fields_(false), lenient_paths_(false),
      lenient_archive_(false), lenient_ratio_(false),
      validated_inventory_binding_(), validated_contents_(false)
{
    memset(&inventory_, 0, sizeof(inventory_));
    memset(validated_inventory_binding_, 0,
           sizeof(validated_inventory_binding_));
    first_failure_path_[0] = '\0';
    first_failure_flags_ = 0U;
}

void ZipReader::SetLenientExtraFields(bool lenient)
{
    lenient_extra_fields_ = lenient;
}

void ZipReader::SetLenientPaths(bool lenient)
{
    lenient_paths_ = lenient;
}

void ZipReader::SetLenientArchive(bool lenient)
{
    lenient_archive_ = lenient;
}

void ZipReader::SetLenientRatio(bool lenient)
{
    lenient_ratio_ = lenient;
}

const char *ZipReader::FirstFailurePath() const
{
    return first_failure_path_;
}

uint16_t ZipReader::FirstFailureFlags() const
{
    return first_failure_flags_;
}

// Records a sanitized copy of the offending raw name for diagnostics.
static void NoteFailurePath(char *destination, const uint8_t *raw,
                            size_t raw_size)
{
    size_t n = 0;
    for (size_t i = 0; i < raw_size && n < 255U; ++i) {
        uint8_t c = raw[i];
        destination[n++] =
            (c >= 0x20U && c < 0x7fU) ? static_cast<char>(c) : '?';
    }
    destination[n] = '\0';
}

// kCp850Fold[b - 0x80]: legacy DOS filename byte -> FAT-stable ASCII (""
// = not foldable; symbols/box-drawing stay rejected as before).
// DOS-era archivers store non-ASCII names without the UTF-8 flag in the
// local DOS codepage (CP437, or CP850 for e.g. Brazilian packs where 0xC6
// is ã). Folding letters with diacritics to their base letter keeps such
// packs browsable; anything folded still goes through the normal ASCII
// FAT validation plus duplicate/case-collision checks downstream, so a
// bad fold can only fail closed, never produce an unsafe path.
// Generated from CP850 + NFKD-strip with an explicit ae/ss/oe/th/eth table.
static const char *const kCp850Fold[128] = {
    "C", // 0x80 U+00C7 Ç -> C
    "u", // 0x81 U+00FC ü -> u
    "e", // 0x82 U+00E9 é -> e
    "a", // 0x83 U+00E2 â -> a
    "a", // 0x84 U+00E4 ä -> a
    "a", // 0x85 U+00E0 à -> a
    "a", // 0x86 U+00E5 å -> a
    "c", // 0x87 U+00E7 ç -> c
    "e", // 0x88 U+00EA ê -> e
    "e", // 0x89 U+00EB ë -> e
    "e", // 0x8A U+00E8 è -> e
    "i", // 0x8B U+00EF ï -> i
    "i", // 0x8C U+00EE î -> i
    "i", // 0x8D U+00EC ì -> i
    "A", // 0x8E U+00C4 Ä -> A
    "A", // 0x8F U+00C5 Å -> A
    "E", // 0x90 U+00C9 É -> E
    "ae", // 0x91 U+00E6 æ -> ae
    "AE", // 0x92 U+00C6 Æ -> AE
    "o", // 0x93 U+00F4 ô -> o
    "o", // 0x94 U+00F6 ö -> o
    "o", // 0x95 U+00F2 ò -> o
    "u", // 0x96 U+00FB û -> u
    "u", // 0x97 U+00F9 ù -> u
    "y", // 0x98 U+00FF ÿ -> y
    "O", // 0x99 U+00D6 Ö -> O
    "U", // 0x9A U+00DC Ü -> U
    "o", // 0x9B U+00F8 ø -> o
    "", // 0x9C (unmapped)
    "O", // 0x9D U+00D8 Ø -> O
    "x", // 0x9E U+00D7 × -> x
    "f", // 0x9F U+0192 ƒ -> f
    "a", // 0xA0 U+00E1 á -> a
    "i", // 0xA1 U+00ED í -> i
    "o", // 0xA2 U+00F3 ó -> o
    "u", // 0xA3 U+00FA ú -> u
    "n", // 0xA4 U+00F1 ñ -> n
    "N", // 0xA5 U+00D1 Ñ -> N
    "a", // 0xA6 U+00AA ª -> a
    "o", // 0xA7 U+00BA º -> o
    "", // 0xA8 (unmapped)
    "", // 0xA9 (unmapped)
    "", // 0xAA (unmapped)
    "", // 0xAB (unmapped)
    "", // 0xAC (unmapped)
    "", // 0xAD (unmapped)
    "", // 0xAE (unmapped)
    "", // 0xAF (unmapped)
    "", // 0xB0 (unmapped)
    "", // 0xB1 (unmapped)
    "", // 0xB2 (unmapped)
    "", // 0xB3 (unmapped)
    "", // 0xB4 (unmapped)
    "A", // 0xB5 U+00C1 Á -> A
    "A", // 0xB6 U+00C2 Â -> A
    "A", // 0xB7 U+00C0 À -> A
    "", // 0xB8 (unmapped)
    "", // 0xB9 (unmapped)
    "", // 0xBA (unmapped)
    "", // 0xBB (unmapped)
    "", // 0xBC (unmapped)
    "", // 0xBD (unmapped)
    "", // 0xBE (unmapped)
    "", // 0xBF (unmapped)
    "", // 0xC0 (unmapped)
    "", // 0xC1 (unmapped)
    "", // 0xC2 (unmapped)
    "", // 0xC3 (unmapped)
    "", // 0xC4 (unmapped)
    "", // 0xC5 (unmapped)
    "a", // 0xC6 U+00E3 ã -> a
    "A", // 0xC7 U+00C3 Ã -> A
    "", // 0xC8 (unmapped)
    "", // 0xC9 (unmapped)
    "", // 0xCA (unmapped)
    "", // 0xCB (unmapped)
    "", // 0xCC (unmapped)
    "", // 0xCD (unmapped)
    "", // 0xCE (unmapped)
    "", // 0xCF (unmapped)
    "d", // 0xD0 U+00F0 ð -> d
    "D", // 0xD1 U+00D0 Ð -> D
    "E", // 0xD2 U+00CA Ê -> E
    "E", // 0xD3 U+00CB Ë -> E
    "E", // 0xD4 U+00C8 È -> E
    "i", // 0xD5 U+0131 ı -> i
    "I", // 0xD6 U+00CD Í -> I
    "I", // 0xD7 U+00CE Î -> I
    "I", // 0xD8 U+00CF Ï -> I
    "", // 0xD9 (unmapped)
    "", // 0xDA (unmapped)
    "", // 0xDB (unmapped)
    "", // 0xDC (unmapped)
    "", // 0xDD (unmapped)
    "I", // 0xDE U+00CC Ì -> I
    "", // 0xDF (unmapped)
    "O", // 0xE0 U+00D3 Ó -> O
    "ss", // 0xE1 U+00DF ß -> ss
    "O", // 0xE2 U+00D4 Ô -> O
    "O", // 0xE3 U+00D2 Ò -> O
    "o", // 0xE4 U+00F5 õ -> o
    "O", // 0xE5 U+00D5 Õ -> O
    "u", // 0xE6 U+00B5 µ -> u
    "t", // 0xE7 U+00FE þ -> t
    "T", // 0xE8 U+00DE Þ -> T
    "U", // 0xE9 U+00DA Ú -> U
    "U", // 0xEA U+00DB Û -> U
    "U", // 0xEB U+00D9 Ù -> U
    "y", // 0xEC U+00FD ý -> y
    "Y", // 0xED U+00DD Ý -> Y
    "", // 0xEE (unmapped: spacing mark)
    "", // 0xEF (unmapped: spacing mark)
    "", // 0xF0 (unmapped)
    "", // 0xF1 (unmapped)
    "", // 0xF2 (unmapped: spacing mark)
    "", // 0xF3 (unmapped)
    "", // 0xF4 (unmapped)
    "", // 0xF5 (unmapped)
    "", // 0xF6 (unmapped)
    "", // 0xF7 (unmapped: spacing mark)
    "", // 0xF8 (unmapped)
    "", // 0xF9 (unmapped: spacing mark)
    "", // 0xFA (unmapped)
    "1", // 0xFB U+00B9 ¹ -> 1
    "3", // 0xFC U+00B3 ³ -> 3
    "2", // 0xFD U+00B2 ² -> 2
    "", // 0xFE (unmapped)
    "", // 0xFF (unmapped: spacing mark)
};

// Lenient browsing: fold '\\' to '/', drop leading separators, and strip
// spaces (leading) plus spaces/dots (trailing) per component — what
// Windows/FAT do with such names. Output never exceeds input length.
// Windows/FAT do with such names. Output never exceeds input length.
// Returns false when nothing usable remains ('.', '..', all-trimmed).
// Collisions created hereby ('A/B' vs 'A /B') are caught by the normal
// duplicate/case checks downstream. Traversal ('..') is NOT repaired:
// it still fails validation, by design.
static bool NormalizeLenientPath(uint8_t *dst, size_t dstsz,
                                 const uint8_t *raw, size_t rawsz,
                                 bool *is_dir)
{
    size_t w = 0;
    size_t r = 0;
    bool trailing_slash = false;
    if (raw == 0 || dst == 0 || dstsz == 0U || rawsz == 0U) return false;
    if (raw[rawsz - 1U] == '/' || raw[rawsz - 1U] == '\\') {
        trailing_slash = true;
    }
    while (r < rawsz && (raw[r] == '/' || raw[r] == '\\')) r++;
    while (r < rawsz) {
        size_t e = r;
        while (e < rawsz && raw[e] != '/' && raw[e] != '\\') e++;
        size_t b = r;
        while (b < e && raw[b] == ' ') b++;
        size_t t = e;
        while (t > b && (raw[t - 1U] == ' ' || raw[t - 1U] == '.')) t--;
        if (t == b) return false;
        if (w != 0U) {
            if (w + 1U >= dstsz) return false;
            dst[w++] = '/';
        }
        if (w + (t - b) >= dstsz) return false;
        memcpy(dst + w, raw + b, t - b);
        w += t - b;
        r = e;
        while (r < rawsz && (raw[r] == '/' || raw[r] == '\\')) r++;
    }
    if (w == 0U) return false;
    if (trailing_slash) {
        if (w + 1U >= dstsz) return false;
        dst[w++] = '/';
    }
    dst[w] = '\0';
    if (is_dir != 0) *is_dir = trailing_slash;
    return true;
}

// Fold one non-ASCII code point (UTF-8-flagged names) to 1-2 ASCII bytes
// (letters only). Returns bytes written; 0 = not representable (the caller
// keeps the original bytes so validation reports the historical status).
// Generated from NFKD-strip with an explicit ae/ss/oe/th/eth table.
static size_t FoldCodePointToAscii(uint32_t cp, char out[2]) {
    switch (cp) {
    case 0x00AAU: out[0] = 'a'; return 1U;
    case 0x00B5U: out[0] = 'u'; return 1U;
    case 0x00BAU: out[0] = 'o'; return 1U;
    case 0x00C0U: out[0] = 'A'; return 1U;
    case 0x00C1U: out[0] = 'A'; return 1U;
    case 0x00C2U: out[0] = 'A'; return 1U;
    case 0x00C3U: out[0] = 'A'; return 1U;
    case 0x00C4U: out[0] = 'A'; return 1U;
    case 0x00C5U: out[0] = 'A'; return 1U;
    case 0x00C6U: out[0] = 'A'; out[1] = 'E'; return 2U;
    case 0x00C7U: out[0] = 'C'; return 1U;
    case 0x00C8U: out[0] = 'E'; return 1U;
    case 0x00C9U: out[0] = 'E'; return 1U;
    case 0x00CAU: out[0] = 'E'; return 1U;
    case 0x00CBU: out[0] = 'E'; return 1U;
    case 0x00CCU: out[0] = 'I'; return 1U;
    case 0x00CDU: out[0] = 'I'; return 1U;
    case 0x00CEU: out[0] = 'I'; return 1U;
    case 0x00CFU: out[0] = 'I'; return 1U;
    case 0x00D0U: out[0] = 'D'; return 1U;
    case 0x00D1U: out[0] = 'N'; return 1U;
    case 0x00D2U: out[0] = 'O'; return 1U;
    case 0x00D3U: out[0] = 'O'; return 1U;
    case 0x00D4U: out[0] = 'O'; return 1U;
    case 0x00D5U: out[0] = 'O'; return 1U;
    case 0x00D6U: out[0] = 'O'; return 1U;
    case 0x00D7U: out[0] = 'x'; return 1U;
    case 0x00D8U: out[0] = 'O'; return 1U;
    case 0x00D9U: out[0] = 'U'; return 1U;
    case 0x00DAU: out[0] = 'U'; return 1U;
    case 0x00DBU: out[0] = 'U'; return 1U;
    case 0x00DCU: out[0] = 'U'; return 1U;
    case 0x00DDU: out[0] = 'Y'; return 1U;
    case 0x00DEU: out[0] = 'T'; return 1U;
    case 0x00DFU: out[0] = 's'; out[1] = 's'; return 2U;
    case 0x00E0U: out[0] = 'a'; return 1U;
    case 0x00E1U: out[0] = 'a'; return 1U;
    case 0x00E2U: out[0] = 'a'; return 1U;
    case 0x00E3U: out[0] = 'a'; return 1U;
    case 0x00E4U: out[0] = 'a'; return 1U;
    case 0x00E5U: out[0] = 'a'; return 1U;
    case 0x00E6U: out[0] = 'a'; out[1] = 'e'; return 2U;
    case 0x00E7U: out[0] = 'c'; return 1U;
    case 0x00E8U: out[0] = 'e'; return 1U;
    case 0x00E9U: out[0] = 'e'; return 1U;
    case 0x00EAU: out[0] = 'e'; return 1U;
    case 0x00EBU: out[0] = 'e'; return 1U;
    case 0x00ECU: out[0] = 'i'; return 1U;
    case 0x00EDU: out[0] = 'i'; return 1U;
    case 0x00EEU: out[0] = 'i'; return 1U;
    case 0x00EFU: out[0] = 'i'; return 1U;
    case 0x00F0U: out[0] = 'd'; return 1U;
    case 0x00F1U: out[0] = 'n'; return 1U;
    case 0x00F2U: out[0] = 'o'; return 1U;
    case 0x00F3U: out[0] = 'o'; return 1U;
    case 0x00F4U: out[0] = 'o'; return 1U;
    case 0x00F5U: out[0] = 'o'; return 1U;
    case 0x00F6U: out[0] = 'o'; return 1U;
    case 0x00F8U: out[0] = 'o'; return 1U;
    case 0x00F9U: out[0] = 'u'; return 1U;
    case 0x00FAU: out[0] = 'u'; return 1U;
    case 0x00FBU: out[0] = 'u'; return 1U;
    case 0x00FCU: out[0] = 'u'; return 1U;
    case 0x00FDU: out[0] = 'y'; return 1U;
    case 0x00FEU: out[0] = 't'; return 1U;
    case 0x00FFU: out[0] = 'y'; return 1U;
    case 0x0100U: out[0] = 'A'; return 1U;
    case 0x0101U: out[0] = 'a'; return 1U;
    case 0x0102U: out[0] = 'A'; return 1U;
    case 0x0103U: out[0] = 'a'; return 1U;
    case 0x0104U: out[0] = 'A'; return 1U;
    case 0x0105U: out[0] = 'a'; return 1U;
    case 0x0106U: out[0] = 'C'; return 1U;
    case 0x0107U: out[0] = 'c'; return 1U;
    case 0x0108U: out[0] = 'C'; return 1U;
    case 0x0109U: out[0] = 'c'; return 1U;
    case 0x010AU: out[0] = 'C'; return 1U;
    case 0x010BU: out[0] = 'c'; return 1U;
    case 0x010CU: out[0] = 'C'; return 1U;
    case 0x010DU: out[0] = 'c'; return 1U;
    case 0x010EU: out[0] = 'D'; return 1U;
    case 0x010FU: out[0] = 'd'; return 1U;
    case 0x0112U: out[0] = 'E'; return 1U;
    case 0x0113U: out[0] = 'e'; return 1U;
    case 0x0114U: out[0] = 'E'; return 1U;
    case 0x0115U: out[0] = 'e'; return 1U;
    case 0x0116U: out[0] = 'E'; return 1U;
    case 0x0117U: out[0] = 'e'; return 1U;
    case 0x0118U: out[0] = 'E'; return 1U;
    case 0x0119U: out[0] = 'e'; return 1U;
    case 0x011AU: out[0] = 'E'; return 1U;
    case 0x011BU: out[0] = 'e'; return 1U;
    case 0x011CU: out[0] = 'G'; return 1U;
    case 0x011DU: out[0] = 'g'; return 1U;
    case 0x011EU: out[0] = 'G'; return 1U;
    case 0x011FU: out[0] = 'g'; return 1U;
    case 0x0120U: out[0] = 'G'; return 1U;
    case 0x0121U: out[0] = 'g'; return 1U;
    case 0x0122U: out[0] = 'G'; return 1U;
    case 0x0123U: out[0] = 'g'; return 1U;
    case 0x0124U: out[0] = 'H'; return 1U;
    case 0x0125U: out[0] = 'h'; return 1U;
    case 0x0128U: out[0] = 'I'; return 1U;
    case 0x0129U: out[0] = 'i'; return 1U;
    case 0x012AU: out[0] = 'I'; return 1U;
    case 0x012BU: out[0] = 'i'; return 1U;
    case 0x012CU: out[0] = 'I'; return 1U;
    case 0x012DU: out[0] = 'i'; return 1U;
    case 0x012EU: out[0] = 'I'; return 1U;
    case 0x012FU: out[0] = 'i'; return 1U;
    case 0x0130U: out[0] = 'I'; return 1U;
    case 0x0131U: out[0] = 'i'; return 1U;
    case 0x0134U: out[0] = 'J'; return 1U;
    case 0x0135U: out[0] = 'j'; return 1U;
    case 0x0136U: out[0] = 'K'; return 1U;
    case 0x0137U: out[0] = 'k'; return 1U;
    case 0x0139U: out[0] = 'L'; return 1U;
    case 0x013AU: out[0] = 'l'; return 1U;
    case 0x013BU: out[0] = 'L'; return 1U;
    case 0x013CU: out[0] = 'l'; return 1U;
    case 0x013DU: out[0] = 'L'; return 1U;
    case 0x013EU: out[0] = 'l'; return 1U;
    case 0x0141U: out[0] = 'L'; return 1U;
    case 0x0142U: out[0] = 'l'; return 1U;
    case 0x0143U: out[0] = 'N'; return 1U;
    case 0x0144U: out[0] = 'n'; return 1U;
    case 0x0145U: out[0] = 'N'; return 1U;
    case 0x0146U: out[0] = 'n'; return 1U;
    case 0x0147U: out[0] = 'N'; return 1U;
    case 0x0148U: out[0] = 'n'; return 1U;
    case 0x014CU: out[0] = 'O'; return 1U;
    case 0x014DU: out[0] = 'o'; return 1U;
    case 0x014EU: out[0] = 'O'; return 1U;
    case 0x014FU: out[0] = 'o'; return 1U;
    case 0x0150U: out[0] = 'O'; return 1U;
    case 0x0151U: out[0] = 'o'; return 1U;
    case 0x0152U: out[0] = 'O'; out[1] = 'E'; return 2U;
    case 0x0153U: out[0] = 'o'; out[1] = 'e'; return 2U;
    case 0x0154U: out[0] = 'R'; return 1U;
    case 0x0155U: out[0] = 'r'; return 1U;
    case 0x0156U: out[0] = 'R'; return 1U;
    case 0x0157U: out[0] = 'r'; return 1U;
    case 0x0158U: out[0] = 'R'; return 1U;
    case 0x0159U: out[0] = 'r'; return 1U;
    case 0x015AU: out[0] = 'S'; return 1U;
    case 0x015BU: out[0] = 's'; return 1U;
    case 0x015CU: out[0] = 'S'; return 1U;
    case 0x015DU: out[0] = 's'; return 1U;
    case 0x015EU: out[0] = 'S'; return 1U;
    case 0x015FU: out[0] = 's'; return 1U;
    case 0x0160U: out[0] = 'S'; return 1U;
    case 0x0161U: out[0] = 's'; return 1U;
    case 0x0162U: out[0] = 'T'; return 1U;
    case 0x0163U: out[0] = 't'; return 1U;
    case 0x0164U: out[0] = 'T'; return 1U;
    case 0x0165U: out[0] = 't'; return 1U;
    case 0x0168U: out[0] = 'U'; return 1U;
    case 0x0169U: out[0] = 'u'; return 1U;
    case 0x016AU: out[0] = 'U'; return 1U;
    case 0x016BU: out[0] = 'u'; return 1U;
    case 0x016CU: out[0] = 'U'; return 1U;
    case 0x016DU: out[0] = 'u'; return 1U;
    case 0x016EU: out[0] = 'U'; return 1U;
    case 0x016FU: out[0] = 'u'; return 1U;
    case 0x0170U: out[0] = 'U'; return 1U;
    case 0x0171U: out[0] = 'u'; return 1U;
    case 0x0172U: out[0] = 'U'; return 1U;
    case 0x0173U: out[0] = 'u'; return 1U;
    case 0x0174U: out[0] = 'W'; return 1U;
    case 0x0175U: out[0] = 'w'; return 1U;
    case 0x0176U: out[0] = 'Y'; return 1U;
    case 0x0177U: out[0] = 'y'; return 1U;
    case 0x0178U: out[0] = 'Y'; return 1U;
    case 0x0179U: out[0] = 'Z'; return 1U;
    case 0x017AU: out[0] = 'z'; return 1U;
    case 0x017BU: out[0] = 'Z'; return 1U;
    case 0x017CU: out[0] = 'z'; return 1U;
    case 0x017DU: out[0] = 'Z'; return 1U;
    case 0x017EU: out[0] = 'z'; return 1U;
    case 0x017FU: out[0] = 's'; return 1U;
    case 0x0192U: out[0] = 'f'; return 1U;
    case 0x01A0U: out[0] = 'O'; return 1U;
    case 0x01A1U: out[0] = 'o'; return 1U;
    case 0x01AFU: out[0] = 'U'; return 1U;
    case 0x01B0U: out[0] = 'u'; return 1U;
    case 0x01CDU: out[0] = 'A'; return 1U;
    case 0x01CEU: out[0] = 'a'; return 1U;
    case 0x01CFU: out[0] = 'I'; return 1U;
    case 0x01D0U: out[0] = 'i'; return 1U;
    case 0x01D1U: out[0] = 'O'; return 1U;
    case 0x01D2U: out[0] = 'o'; return 1U;
    case 0x01D3U: out[0] = 'U'; return 1U;
    case 0x01D4U: out[0] = 'u'; return 1U;
    case 0x01D5U: out[0] = 'U'; return 1U;
    case 0x01D6U: out[0] = 'u'; return 1U;
    case 0x01D7U: out[0] = 'U'; return 1U;
    case 0x01D8U: out[0] = 'u'; return 1U;
    case 0x01D9U: out[0] = 'U'; return 1U;
    case 0x01DAU: out[0] = 'u'; return 1U;
    case 0x01DBU: out[0] = 'U'; return 1U;
    case 0x01DCU: out[0] = 'u'; return 1U;
    case 0x01DEU: out[0] = 'A'; return 1U;
    case 0x01DFU: out[0] = 'a'; return 1U;
    case 0x01E0U: out[0] = 'A'; return 1U;
    case 0x01E1U: out[0] = 'a'; return 1U;
    case 0x01E6U: out[0] = 'G'; return 1U;
    case 0x01E7U: out[0] = 'g'; return 1U;
    case 0x01E8U: out[0] = 'K'; return 1U;
    case 0x01E9U: out[0] = 'k'; return 1U;
    case 0x01EAU: out[0] = 'O'; return 1U;
    case 0x01EBU: out[0] = 'o'; return 1U;
    case 0x01ECU: out[0] = 'O'; return 1U;
    case 0x01EDU: out[0] = 'o'; return 1U;
    case 0x01F0U: out[0] = 'j'; return 1U;
    case 0x01F4U: out[0] = 'G'; return 1U;
    case 0x01F5U: out[0] = 'g'; return 1U;
    case 0x01F8U: out[0] = 'N'; return 1U;
    case 0x01F9U: out[0] = 'n'; return 1U;
    case 0x01FAU: out[0] = 'A'; return 1U;
    case 0x01FBU: out[0] = 'a'; return 1U;
    case 0x0200U: out[0] = 'A'; return 1U;
    case 0x0201U: out[0] = 'a'; return 1U;
    case 0x0202U: out[0] = 'A'; return 1U;
    case 0x0203U: out[0] = 'a'; return 1U;
    case 0x0204U: out[0] = 'E'; return 1U;
    case 0x0205U: out[0] = 'e'; return 1U;
    case 0x0206U: out[0] = 'E'; return 1U;
    case 0x0207U: out[0] = 'e'; return 1U;
    case 0x0208U: out[0] = 'I'; return 1U;
    case 0x0209U: out[0] = 'i'; return 1U;
    case 0x020AU: out[0] = 'I'; return 1U;
    case 0x020BU: out[0] = 'i'; return 1U;
    case 0x020CU: out[0] = 'O'; return 1U;
    case 0x020DU: out[0] = 'o'; return 1U;
    case 0x020EU: out[0] = 'O'; return 1U;
    case 0x020FU: out[0] = 'o'; return 1U;
    case 0x0210U: out[0] = 'R'; return 1U;
    case 0x0211U: out[0] = 'r'; return 1U;
    case 0x0212U: out[0] = 'R'; return 1U;
    case 0x0213U: out[0] = 'r'; return 1U;
    case 0x0214U: out[0] = 'U'; return 1U;
    case 0x0215U: out[0] = 'u'; return 1U;
    case 0x0216U: out[0] = 'U'; return 1U;
    case 0x0217U: out[0] = 'u'; return 1U;
    case 0x0218U: out[0] = 'S'; return 1U;
    case 0x0219U: out[0] = 's'; return 1U;
    case 0x021AU: out[0] = 'T'; return 1U;
    case 0x021BU: out[0] = 't'; return 1U;
    case 0x021EU: out[0] = 'H'; return 1U;
    case 0x021FU: out[0] = 'h'; return 1U;
    case 0x0226U: out[0] = 'A'; return 1U;
    case 0x0227U: out[0] = 'a'; return 1U;
    case 0x0228U: out[0] = 'E'; return 1U;
    case 0x0229U: out[0] = 'e'; return 1U;
    case 0x022AU: out[0] = 'O'; return 1U;
    case 0x022BU: out[0] = 'o'; return 1U;
    case 0x022CU: out[0] = 'O'; return 1U;
    case 0x022DU: out[0] = 'o'; return 1U;
    case 0x022EU: out[0] = 'O'; return 1U;
    case 0x022FU: out[0] = 'o'; return 1U;
    case 0x0230U: out[0] = 'O'; return 1U;
    case 0x0231U: out[0] = 'o'; return 1U;
    case 0x0232U: out[0] = 'Y'; return 1U;
    case 0x0233U: out[0] = 'y'; return 1U;
    case 0x03BCU: out[0] = 'u'; return 1U;
    default: return 0U;
    }
}

// Lenient browsing: fold a normalized name with legacy (CP850) or UTF-8
// diacritics down to FAT-stable ASCII letters (see kCp850Fold and
// FoldCodePointToAscii). Pure-ASCII input passes through unchanged.
// Returns false when a byte/sequence is not foldable; the caller then
// validates the original bytes so the reported status is unchanged.
// Output is NUL-terminated and never longer than dstsz - 1.
static bool FoldLenientNameToAscii(uint8_t *dst, size_t dstsz,
                                   const uint8_t *raw, size_t rawsz,
                                   bool utf8_flag)
{
    if (dst == 0 || dstsz == 0U || raw == 0 || rawsz == 0U) return false;
    size_t w = 0U;
    if (!utf8_flag) {
        for (size_t r = 0U; r < rawsz; ++r) {
            const uint8_t b = raw[r];
            if (b < 0x80U) {
                if (w + 1U >= dstsz) return false;
                dst[w++] = b;
                continue;
            }
            const char *fold = kCp850Fold[b - 0x80U];
            const size_t n = strlen(fold);
            if (n == 0U || w + n >= dstsz) return false;
            memcpy(dst + w, fold, n);
            w += n;
        }
    } else {
        size_t r = 0U;
        while (r < rawsz) {
            const uint8_t first = raw[r];
            if (first < 0x80U) {
                if (w + 1U >= dstsz) return false;
                dst[w++] = first;
                ++r;
                continue;
            }
            // Strict UTF-8, mirroring IsValidUtf8: C2-DF/E0-EF/F0-F4
            // starters, 80-BF continuations, no overlongs, surrogates or
            // code points past U+10FFFF.
            uint32_t cp = 0U;
            size_t cont = 0U;
            if (first >= 0xc2U && first <= 0xdfU) {
                cp = static_cast<uint32_t>(first & 0x1fU);
                cont = 1U;
            } else if (first >= 0xe0U && first <= 0xefU) {
                cp = static_cast<uint32_t>(first & 0x0fU);
                cont = 2U;
            } else if (first >= 0xf0U && first <= 0xf4U) {
                cp = static_cast<uint32_t>(first & 0x07U);
                cont = 3U;
            } else {
                return false;
            }
            if (cont > rawsz - r - 1U) return false;
            for (size_t i = 0U; i < cont; ++i) {
                const uint8_t next = raw[r + 1U + i];
                if ((next & 0xc0U) != 0x80U) return false;
                cp = (cp << 6U) | static_cast<uint32_t>(next & 0x3fU);
            }
            if ((cont == 1U && cp < 0x80U) ||
                (cont == 2U && cp < 0x800U) ||
                (cont == 3U && cp < 0x10000U) ||
                (cp >= 0xd800U && cp <= 0xdfffU) ||
                cp > 0x10ffffU) {
                return false;
            }
            char out[2];
            const size_t n = FoldCodePointToAscii(cp, out);
            if (n == 0U || w + n >= dstsz) return false;
            memcpy(dst + w, out, n);
            w += n;
            r += 1U + cont;
        }
    }
    if (w >= dstsz) return false;
    dst[w] = '\0';
    return true;
}

void ZipReader::SetWorkspace(ZipWorkspace *workspace)
{
    workspace_ = workspace;
}

const ZipInventory &ZipReader::inventory() const
{
    return inventory_;
}

ZipStatus ZipReader::Open(SeekableZipSource *source,
                           ZipEntry *entries,
                           size_t entry_capacity,
                           const ZipLimits &limits,
                           ZipProgressCallback open_progress,
                           void *open_context)
{
    ClearValidatedContents();
    open_ = false;
    source_ = 0;
    entries_ = 0;
    first_failure_path_[0] = '\0';
    first_failure_flags_ = 0U;
    memset(&inventory_, 0, sizeof(inventory_));
    if (source == 0 || entries == 0 || limits.maximum_entries == 0U ||
        limits.maximum_entries > kZipMaximumEntries ||
        limits.maximum_path_bytes == 0U ||
        limits.maximum_path_bytes > kZipMaximumPathBytes ||
        limits.maximum_archive_bytes < kEndRecordSize ||
        limits.maximum_file_bytes == 0U ||
        limits.maximum_installed_bytes == 0U ||
        limits.maximum_compression_ratio == 0U) {
        return ZipStatus::InvalidArgument;
    }

    uint64_t archive_size = 0U;
    if (!source->GetSize(&archive_size)) {
        return ZipStatus::SourceIo;
    }
    if (archive_size < kEndRecordSize ||
        archive_size > limits.maximum_archive_bytes) {
        return ZipStatus::ArchiveSizeInvalid;
    }
    const uint64_t tail_size = archive_size < UINT64_C(65557)
                                   ? archive_size
                                   : UINT64_C(65557);
    const uint64_t tail_begin = archive_size - tail_size;
    static const uint8_t end_signature[4] = {'P', 'K', 0x05U, 0x06U};
    bool end_found = false;
    uint64_t end_offset = 0U;
    if (!FindLastSignature(source, tail_begin, archive_size, end_signature,
                           &end_found, &end_offset)) {
        return ZipStatus::SourceIo;
    }
    if (!end_found || !RangeWithin(end_offset, kEndRecordSize, archive_size)) {
        return ZipStatus::EndRecordInvalid;
    }
    uint8_t end_record[kEndRecordSize];
    if (!source->ReadAt(end_offset, end_record, sizeof(end_record))) {
        return ZipStatus::SourceIo;
    }
    if (ReadU32(end_record) != kEndSignature) {
        return ZipStatus::EndRecordInvalid;
    }
    const uint16_t disk_number = ReadU16(end_record + 4U);
    const uint16_t central_disk = ReadU16(end_record + 6U);
    const uint16_t entries_on_disk = ReadU16(end_record + 8U);
    const uint16_t entry_count = ReadU16(end_record + 10U);
    const uint32_t central_size = ReadU32(end_record + 12U);
    const uint32_t central_offset = ReadU32(end_record + 16U);
    const uint16_t comment_length = ReadU16(end_record + 20U);
    uint64_t archive_end_from_record = 0U;
    if (!CheckedAdd(end_offset, kEndRecordSize, &archive_end_from_record) ||
        !CheckedAdd(archive_end_from_record, comment_length,
                    &archive_end_from_record) ||
        archive_end_from_record != archive_size) {
        return ZipStatus::EndRecordInvalid;
    }
    // Comment bytes sit past the end record and are bounds-validated
    // above; browsing skips them, updates keep rejecting them.
    if (comment_length != 0U && !lenient_archive_) {
        return ZipStatus::ArchiveCommentForbidden;
    }
    if (disk_number != 0U || central_disk != 0U ||
        entries_on_disk != entry_count) {
        return ZipStatus::MultiDiskForbidden;
    }
    if (entry_count == 0U || entry_count == UINT16_MAX ||
        entry_count > limits.maximum_entries) {
        return ZipStatus::EntryCountInvalid;
    }
    if (central_size == UINT32_MAX || central_offset == UINT32_MAX) {
        return ZipStatus::Zip64Forbidden;
    }
    if (entry_count > entry_capacity) {
        return ZipStatus::StorageTooSmall;
    }
    uint64_t central_end = 0U;
    if (!CheckedAdd(central_offset, central_size, &central_end) ||
        central_end != end_offset) {
        return ZipStatus::CentralDirectoryInvalid;
    }

    static const uint8_t zip64_end_signature[4] = {'P', 'K', 0x06U, 0x06U};
    static const uint8_t zip64_locator_signature[4] = {'P', 'K', 0x06U, 0x07U};
    bool found = false;
    if (!ContainsSignature(source, tail_begin, end_offset,
                           zip64_end_signature, &found)) {
        return ZipStatus::SourceIo;
    }
    if (found) {
        return ZipStatus::Zip64Forbidden;
    }
    if (!ContainsSignature(source, tail_begin, end_offset,
                           zip64_locator_signature, &found)) {
        return ZipStatus::SourceIo;
    }
    if (found) {
        return ZipStatus::Zip64Forbidden;
    }

    uint64_t cursor = central_offset;
    size_t file_count = 0U;
    size_t directory_count = 0U;
    for (size_t index = 0U; index < entry_count; ++index) {
        if (!RangeWithin(cursor, kCentralHeaderSize, central_end)) {
            return ZipStatus::CentralDirectoryInvalid;
        }
        uint8_t header[kCentralHeaderSize];
        if (!source->ReadAt(cursor, header, sizeof(header))) {
            return ZipStatus::SourceIo;
        }
        if (ReadU32(header) != kCentralSignature) {
            return ZipStatus::CentralDirectoryInvalid;
        }
        const uint16_t version_made = ReadU16(header + 4U);
        const uint16_t version_needed = ReadU16(header + 6U);
        const uint16_t flags = ReadU16(header + 8U);
        const uint16_t method = ReadU16(header + 10U);
        const uint32_t crc32 = ReadU32(header + 16U);
        const uint32_t compressed_size = ReadU32(header + 20U);
        const uint32_t size = ReadU32(header + 24U);
        const uint16_t name_length = ReadU16(header + 28U);
        const uint16_t extra_length = ReadU16(header + 30U);
        const uint16_t member_comment_length = ReadU16(header + 32U);
        const uint16_t disk_start = ReadU16(header + 34U);
        const uint32_t external_attributes = ReadU32(header + 38U);
        const uint32_t local_offset = ReadU32(header + 42U);
        if (version_needed > 20U) {
            return ZipStatus::UnsupportedVersion;
        }
        uint16_t allowed = kUtf8Flag;
        if (lenient_paths_) {
            allowed |= kLenientFlagMask;
        }
        if ((flags & static_cast<uint16_t>(~allowed)) != 0U) {
            // The raw name is not read yet at this point (bounds checks
            // come first), so only the offending bits are recorded.
            first_failure_flags_ = flags;
            return ZipStatus::UnsupportedFlags;
        }
        if (method != static_cast<uint16_t>(ZipCompression::Store) &&
            method != static_cast<uint16_t>(ZipCompression::Deflate)) {
            return ZipStatus::UnsupportedCompression;
        }
        if (disk_start != 0U) {
            return ZipStatus::MultiDiskForbidden;
        }
        if (compressed_size == UINT32_MAX || size == UINT32_MAX ||
            local_offset == UINT32_MAX) {
            return ZipStatus::Zip64Forbidden;
        }
        // Extra content is bounds-skipped below; only its presence is
        // policy. Browsing tolerates benign tool metadata (timestamps,
        // Unix attrs); the update flow keeps rejecting.
        if (extra_length != 0U && !lenient_extra_fields_) {
            return ZipStatus::ExtraFieldForbidden;
        }
        if (member_comment_length != 0U) {
            return ZipStatus::MemberCommentForbidden;
        }
        if (name_length == 0U) {
            return ZipStatus::PathInvalid;
        }
        uint64_t record_size = kCentralHeaderSize;
        if (!CheckedAdd(record_size, name_length, &record_size) ||
            !CheckedAdd(record_size, extra_length, &record_size) ||
            !CheckedAdd(record_size, member_comment_length, &record_size) ||
            !RangeWithin(cursor, record_size, central_end)) {
            return ZipStatus::CentralDirectoryInvalid;
        }
        if (name_length > limits.maximum_path_bytes + 1U ||
            name_length > kZipMaximumPathBytes + 1U) {
            return ZipStatus::PathTooLong;
        }
        uint8_t raw_name[kZipMaximumPathBytes + 1U];
        if (!source->ReadAt(cursor + kCentralHeaderSize, raw_name,
                            name_length)) {
            return ZipStatus::SourceIo;
        }
        const bool name_is_directory = raw_name[name_length - 1U] == '/';
        ZipStatus metadata_status = ZipStatus::Ok;
        if (!IsDirectoryMetadata(static_cast<uint8_t>(version_made >> 8U),
                                 external_attributes, name_is_directory,
                                 &metadata_status)) {
            return metadata_status;
        }

        ZipEntry &entry = entries[index];
        memset(&entry, 0, sizeof(entry));
        // Lenient browsing: fold Windows separators/leading slashes into a
        // bounded buffer, then validate the normalized form. FAT-impossible
        // names (.., trailing dots, reserved stems) still fail, by design.
        const uint8_t *check_name = raw_name;
        size_t check_length = name_length;
        bool check_is_dir = name_is_directory;
        uint8_t norm_name[kZipMaximumPathBytes + 1U];
        uint8_t fold_name[kZipMaximumPathBytes + 1U];
        if (lenient_paths_ && name_length <= kZipMaximumPathBytes) {
            if (!NormalizeLenientPath(norm_name, sizeof(norm_name),
                                      raw_name, name_length, &check_is_dir)) {
                NoteFailurePath(first_failure_path_, raw_name,
                                name_length > 240U ? 240U : name_length);
                return ZipStatus::PathInvalid;
            }
            check_name = norm_name;
            check_length = strlen((const char *)norm_name);
            // Fold legacy/CP850 or UTF-8 diacritics to ASCII so real-world
            // ROM packs stay browsable. The folded form goes through the
            // normal FAT validation plus duplicate/case checks below; when
            // a name is not foldable the original bytes are validated, so
            // the reported status is unchanged.
            if (FoldLenientNameToAscii(fold_name, sizeof(fold_name),
                                       norm_name, check_length,
                                       (flags & kUtf8Flag) != 0U)) {
                check_name = fold_name;
                check_length = strlen((const char *)fold_name);
            }
        }
        const ZipStatus path_status = ValidateAndCopyPath(
            check_name, check_length, flags, check_is_dir,
            limits.maximum_path_bytes, entry.path);
        if (path_status != ZipStatus::Ok) {
            NoteFailurePath(first_failure_path_, raw_name,
                            name_length > 240U ? 240U : name_length);
            return path_status;
        }
        for (size_t previous = 0U; previous < index; ++previous) {
            if (strcmp(entry.path, entries[previous].path) == 0) {
                NoteFailurePath(first_failure_path_,
                                (const uint8_t *)entry.path,
                                strlen(entry.path));
                return ZipStatus::DuplicatePath;
            }
            if (FatEqual(entry.path, entries[previous].path)) {
                NoteFailurePath(first_failure_path_,
                                (const uint8_t *)entry.path,
                                strlen(entry.path));
                return ZipStatus::FatCaseCollision;
            }
        }

        entry.is_directory = check_is_dir;
        entry.compression = static_cast<ZipCompression>(method);
        entry.flags = flags;
        entry.crc32 = crc32;
        entry.compressed_size = compressed_size;
        entry.size = size;
        entry.local_header_offset = local_offset;
        entry.external_attributes = external_attributes;
        entry.creator_system = static_cast<uint8_t>(version_made >> 8U);

        if (entry.is_directory) {
            if (entry.compression != ZipCompression::Store ||
                entry.compressed_size != 0U || entry.size != 0U ||
                entry.crc32 != 0U) {
                return ZipStatus::DirectoryMetadataInvalid;
            }
            ++directory_count;
        } else {
            if (entry.size > limits.maximum_file_bytes ||
                (entry.size != 0U && entry.compressed_size == 0U)) {
                return ZipStatus::MemberSizeInvalid;
            }
            const uint64_t ratio_denominator =
                entry.compressed_size == 0U ? 1U : entry.compressed_size;
            if (!lenient_ratio_ &&
                (entry.size / ratio_denominator >
                     limits.maximum_compression_ratio ||
                 (entry.size / ratio_denominator ==
                      limits.maximum_compression_ratio &&
                  entry.size % ratio_denominator != 0U))) {
                return ZipStatus::CompressionRatioInvalid;
            }
            ++file_count;
        }
        cursor += record_size;
        if (open_progress != 0 && (index & 255U) == 0U &&
            !open_progress(index, entry_count, open_context)) {
            return ZipStatus::Cancelled;
        }
    }
    if (cursor != central_end) {
        return ZipStatus::CentralDirectoryInvalid;
    }

    for (size_t index = 0U; index < entry_count; ++index) {
        if (entries[index].is_directory) {
            continue;
        }
        for (size_t child = 0U; child < entry_count; ++child) {
            if (index != child &&
                FatPrefixDirectory(entries[index].path, entries[child].path)) {
                return ZipStatus::FileUsedAsDirectory;
            }
        }
    }

    uint64_t expected_local_offset = 0U;
    uint64_t installed_size = 0U;
    for (size_t index = 0U; index < entry_count; ++index) {
        ZipEntry &entry = entries[index];
        if (open_progress != 0 && (index & 255U) == 0U &&
            !open_progress(entry_count + index, entry_count * 2U,
                           open_context)) {
            return ZipStatus::Cancelled;
        }
        if (entry.local_header_offset != expected_local_offset ||
            !RangeWithin(entry.local_header_offset, kLocalHeaderSize,
                         central_offset)) {
            return ZipStatus::OffsetInvalid;
        }
        uint8_t local[kLocalHeaderSize];
        if (!source->ReadAt(entry.local_header_offset, local, sizeof(local))) {
            return ZipStatus::SourceIo;
        }
        if (ReadU32(local) != kLocalSignature) {
            return ZipStatus::LocalHeaderMismatch;
        }
        const uint16_t local_version = ReadU16(local + 4U);
        const uint16_t local_flags = ReadU16(local + 6U);
        const uint16_t local_method = ReadU16(local + 8U);
        const uint32_t local_crc = ReadU32(local + 14U);
        const uint32_t local_compressed_size = ReadU32(local + 18U);
        const uint32_t local_size = ReadU32(local + 22U);
        const uint16_t local_name_length = ReadU16(local + 26U);
        const uint16_t local_extra_length = ReadU16(local + 28U);
        if (local_version > 20U) {
            return ZipStatus::UnsupportedVersion;
        }
        // With a data descriptor (bit 3) the local CRC/sizes are zero or
        // postponed; central values (already validated) stay authoritative
        // and extraction verifies CRC over the output anyway.
        bool sizes_postponed =
            lenient_paths_ && ((local_flags & (UINT16_C(1) << 3U)) != 0U);
        if (local_flags != entry.flags ||
            local_method != static_cast<uint16_t>(entry.compression) ||
            (!sizes_postponed &&
             (local_crc != entry.crc32 ||
              local_compressed_size != entry.compressed_size ||
              local_size != entry.size))) {
            return ZipStatus::LocalHeaderMismatch;
        }
        if (local_extra_length != 0U && !lenient_extra_fields_) {
            return ZipStatus::ExtraFieldForbidden;
        }
        uint8_t local_name[kZipMaximumPathBytes + 1U];
        if (local_name_length > kZipMaximumPathBytes + 1U) {
            return ZipStatus::LocalHeaderMismatch;
        }
        if (!source->ReadAt(entry.local_header_offset + kLocalHeaderSize,
                            local_name, local_name_length)) {
            return ZipStatus::SourceIo;
        }
        // Lenient browsing: compare the normalized form so archives whose
        // local headers use raw Windows separators still match. Byte
        // offsets keep using raw lengths (positions, unaffected).
        const uint8_t *cmp_name = local_name;
        size_t cmp_length = local_name_length;
        uint8_t norm_local[kZipMaximumPathBytes + 1U];
        uint8_t fold_local[kZipMaximumPathBytes + 1U];
        if (lenient_paths_ && local_name_length <= kZipMaximumPathBytes) {
            bool dummy = false;
            if (!NormalizeLenientPath(norm_local, sizeof(norm_local),
                                      local_name, local_name_length,
                                      &dummy)) {
                return ZipStatus::LocalHeaderMismatch;
            }
            cmp_name = norm_local;
            cmp_length = strlen((const char *)norm_local);
            // Same diacritic fold as the central directory above, so local
            // headers compare against the folded entry path.
            if (FoldLenientNameToAscii(fold_local, sizeof(fold_local),
                                       norm_local, cmp_length,
                                       (entry.flags & kUtf8Flag) != 0U)) {
                cmp_name = fold_local;
                cmp_length = strlen((const char *)fold_local);
            }
        }
        const size_t expected_name_length =
            strlen(entry.path) + (entry.is_directory ? 1U : 0U);
        if (cmp_length != expected_name_length) {
            return ZipStatus::LocalHeaderMismatch;
        }
        uint64_t data_offset = 0U;
        if (!CheckedAdd(entry.local_header_offset, kLocalHeaderSize,
                        &data_offset) ||
            !CheckedAdd(data_offset, local_name_length, &data_offset) ||
            !CheckedAdd(data_offset, local_extra_length, &data_offset) ||
            !RangeWithin(data_offset, entry.compressed_size, central_offset)) {
            return ZipStatus::OffsetInvalid;
        }
        if (memcmp(cmp_name, entry.path, strlen(entry.path)) != 0 ||
            (entry.is_directory &&
             (cmp_length == 0U || cmp_name[cmp_length - 1U] != '/'))) {
            return ZipStatus::LocalHeaderMismatch;
        }
        entry.data_offset = data_offset;
        if (!CheckedAdd(data_offset, entry.compressed_size,
                        &expected_local_offset)) {
            return ZipStatus::OffsetInvalid;
        }
        // Data descriptor (general flag bit 3, browsing only): 12 payload
        // bytes (CRC/sizes), optionally preceded by the PK0708 signature.
        // Skipped so the offset chain stays exact; payload must agree with
        // the validated central values. Absent when the next record is
        // already a local header (gratuitously set flag).
        if (!entry.is_directory && lenient_paths_ &&
            ((entry.flags & (UINT16_C(1) << 3U)) != 0U)) {
            uint8_t sig[4];
            if (!RangeWithin(expected_local_offset, sizeof(sig),
                             central_offset) ||
                !source->ReadAt(expected_local_offset, sig, sizeof(sig))) {
                return ZipStatus::SourceIo;
            }
            if (ReadU32(sig) != kLocalSignature) {
                uint8_t dd[16];
                size_t dd_size = 12U;
                const uint8_t *pl = dd;
                if (RangeWithin(expected_local_offset, sizeof(dd),
                                central_offset)) {
                    if (!source->ReadAt(expected_local_offset, dd,
                                        sizeof(dd))) {
                        return ZipStatus::SourceIo;
                    }
                    if (ReadU32(dd) == kDescriptorSignature) {
                        dd_size = 16U;
                        pl = dd + 4U;
                    }
                } else {
                    if (!RangeWithin(expected_local_offset, 12U,
                                     central_offset) ||
                        !source->ReadAt(expected_local_offset, dd, 12U)) {
                        return ZipStatus::OffsetInvalid;
                    }
                }
                if (ReadU32(pl) != entry.crc32 ||
                    ReadU32(pl + 4U) !=
                        (uint32_t)entry.compressed_size ||
                    ReadU32(pl + 8U) != (uint32_t)entry.size) {
                    return ZipStatus::OffsetInvalid;
                }
                if (!CheckedAdd(expected_local_offset, dd_size,
                                &expected_local_offset)) {
                    return ZipStatus::OffsetInvalid;
                }
            }
        }
        if (!entry.is_directory) {
            if (!CheckedAdd(installed_size, entry.size, &installed_size) ||
                installed_size > limits.maximum_installed_bytes) {
                return ZipStatus::InstalledSizeInvalid;
            }
        }
    }
    if (expected_local_offset != central_offset) {
        return ZipStatus::OffsetInvalid;
    }

    source_ = source;
    entries_ = entries;
    limits_ = limits;
    inventory_.entries = entries;
    inventory_.entry_count = entry_count;
    inventory_.file_count = file_count;
    inventory_.directory_count = directory_count;
    inventory_.archive_size = archive_size;
    inventory_.installed_size = installed_size;
    inventory_.central_directory_offset = central_offset;
    inventory_.central_directory_size = central_size;
    open_ = true;
    return ZipStatus::Ok;
}

const ZipExpectedFile *ZipReader::FindExpectedFile(
    const ZipExpectedInventory &expected,
    const char *path) const
{
    if (path == 0) {
        return 0;
    }
    for (size_t index = 0U; index < expected.file_count; ++index) {
        if (expected.files[index].path != 0 &&
            strcmp(expected.files[index].path, path) == 0) {
            return &expected.files[index];
        }
    }
    return 0;
}

bool ZipReader::ComputeExpectedInventoryBinding(
    const ZipExpectedInventory &expected,
    uint8_t digest[kSha256DigestBytes]) const
{
    if (!open_ || digest == 0) {
        return false;
    }

    // This is a canonical, padding-free encoding in validated ZIP order. All
    // counts and lengths are fixed-width little-endian uint64 values; member
    // policy fields use explicit bytes. The domain separator makes the digest
    // unusable as another BMX object.
    static const char domain[] = "bmx-zip-validated-inventory-v1";
    Sha256 hash;
    if (!HashSizedBytes(&hash, domain, sizeof(domain) - 1U) ||
        !HashU64(&hash, expected.file_count) ||
        !HashU64(&hash, expected.directory_count)) {
        return false;
    }

    for (size_t index = 0U; index < inventory_.entry_count; ++index) {
        const ZipEntry &entry = entries_[index];
        const uint8_t kind = entry.is_directory ? 0x44U : 0x46U;
        const size_t path_size = strlen(entry.path);
        if (!hash.Update(&kind, sizeof(kind)) ||
            !HashSizedBytes(&hash, entry.path, path_size)) {
            return false;
        }
        if (entry.is_directory) {
            continue;
        }

        const ZipExpectedFile *wanted =
            FindExpectedFile(expected, entry.path);
        if (wanted == 0 ||
            !HashU64(&hash, wanted->size)) {
            return false;
        }
        const uint8_t compression =
            static_cast<uint8_t>(wanted->compression);
        const uint8_t has_sha256 = wanted->sha256 == 0 ? 0U : 1U;
        if (!hash.Update(&compression, sizeof(compression)) ||
            !hash.Update(&has_sha256, sizeof(has_sha256)) ||
            (has_sha256 != 0U &&
             !hash.Update(wanted->sha256, kSha256DigestBytes))) {
            return false;
        }
    }
    return hash.Final(digest);
}

bool ZipReader::ValidatedContentsMatch(
    const ZipExpectedInventory &expected) const
{
    if (!validated_contents_) {
        return false;
    }
    uint8_t current[kSha256DigestBytes];
    if (!ComputeExpectedInventoryBinding(expected, current)) {
        return false;
    }
    return ConstantTimeDigestEqual(current, validated_inventory_binding_);
}

void ZipReader::ClearValidatedContents()
{
    memset(validated_inventory_binding_, 0,
           sizeof(validated_inventory_binding_));
    validated_contents_ = false;
}

ZipStatus ZipReader::VerifyExpectedInventory(
    const ZipExpectedInventory &expected,
    bool *requires_hash) const
{
    return VerifyExpectedInventory(expected, requires_hash, 0, 0);
}

ZipStatus ZipReader::VerifyExpectedInventory(
    const ZipExpectedInventory &expected, bool *requires_hash,
    ZipProgressCallback progress, void *progress_context) const
{
    if (!open_) {
        return ZipStatus::NotOpen;
    }
    if (requires_hash == 0 ||
        (expected.file_count != 0U && expected.files == 0) ||
        (expected.directory_count != 0U && expected.directories == 0)) {
        return ZipStatus::ExpectedInventoryInvalid;
    }
    *requires_hash = false;
    if (expected.file_count != inventory_.file_count ||
        expected.directory_count != inventory_.directory_count) {
        return ZipStatus::InventoryMismatch;
    }
    for (size_t index = 0U; index < expected.file_count; ++index) {
        const ZipExpectedFile &wanted = expected.files[index];
        if (wanted.path == 0 || wanted.path[0] == '\0') {
            return ZipStatus::ExpectedInventoryInvalid;
        }
        for (size_t previous = 0U; previous < index; ++previous) {
            if (strcmp(wanted.path, expected.files[previous].path) == 0) {
                return ZipStatus::ExpectedInventoryInvalid;
            }
        }
        const ZipEntry *actual = 0;
        for (size_t entry_index = 0U;
             entry_index < inventory_.entry_count; ++entry_index) {
            const ZipEntry &entry = entries_[entry_index];
            if (!entry.is_directory && strcmp(entry.path, wanted.path) == 0) {
                actual = &entry;
                break;
            }
        }
        if (actual == 0 || actual->size != wanted.size ||
            actual->compression != wanted.compression) {
            return ZipStatus::InventoryMismatch;
        }
        if (wanted.sha256 != 0) {
            *requires_hash = true;
        }
        if (progress != 0 &&
            !progress(index + 1U, expected.file_count, progress_context)) {
            return ZipStatus::Cancelled;
        }
    }
    for (size_t index = 0U; index < expected.directory_count; ++index) {
        const char *wanted = expected.directories[index];
        if (wanted == 0 || wanted[0] == '\0') {
            return ZipStatus::ExpectedInventoryInvalid;
        }
        for (size_t previous = 0U; previous < index; ++previous) {
            if (strcmp(wanted, expected.directories[previous]) == 0) {
                return ZipStatus::ExpectedInventoryInvalid;
            }
        }
        bool found = false;
        for (size_t entry_index = 0U;
             entry_index < inventory_.entry_count; ++entry_index) {
            if (entries_[entry_index].is_directory &&
                strcmp(entries_[entry_index].path, wanted) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            return ZipStatus::InventoryMismatch;
        }
    }
    return ZipStatus::Ok;
}

ZipStatus ZipReader::StreamFile(const ZipEntry &entry,
                                 const ZipExpectedFile *expected,
                                 ZipHashSink *hash_sink,
                                 ZipExtractSink *extract_sink,
                                 ZipProgressCallback progress,
                                 void *progress_context)
{
    if (!open_ || entry.is_directory || expected == 0) {
        return ZipStatus::InvalidArgument;
    }
    if (workspace_ == 0) {
        return ZipStatus::WorkspaceRequired;
    }
    uint64_t current_size = 0U;
    if (!source_->GetSize(&current_size)) {
        return ZipStatus::SourceIo;
    }
    if (current_size != inventory_.archive_size) {
        return ZipStatus::OffsetInvalid;
    }

    bool hash_started = false;
    bool extraction_started = false;
    if (hash_sink != 0) {
        if (!hash_sink->BeginFile(entry.path, entry.size)) {
            return ZipStatus::HashSinkError;
        }
        hash_started = true;
    } else if (expected->sha256 != 0) {
        return ZipStatus::HashSinkRequired;
    }
    if (extract_sink != 0) {
        if (!extract_sink->BeginEntry(entry)) {
            if (hash_started) {
                hash_sink->AbortFile();
            }
            return ZipStatus::SinkError;
        }
        extraction_started = true;
    }

    uint32_t crc = UINT32_C(0xffffffff);
    uint64_t produced = 0U;
    ZipStatus result = ZipStatus::Ok;
    uint8_t *const input = workspace_->input;

    if (entry.compression == ZipCompression::Store) {
        uint64_t remaining = entry.compressed_size;
        uint64_t offset = entry.data_offset;
        while (remaining != 0U) {
            const size_t amount = static_cast<size_t>(
                remaining < kZipInputBufferBytes
                    ? remaining
                    : kZipInputBufferBytes);
            if (!source_->ReadAt(offset, input, amount)) {
                result = ZipStatus::SourceIo;
                break;
            }
            if (produced > entry.size || amount > entry.size - produced) {
                result = ZipStatus::UncompressedSizeMismatch;
                break;
            }
            crc = UpdateCrc32(crc, input, amount);
            if (hash_started && !hash_sink->Update(ByteView(input, amount))) {
                result = ZipStatus::HashSinkError;
                break;
            }
            if (extraction_started &&
                !extract_sink->Write(ByteView(input, amount))) {
                result = ZipStatus::SinkError;
                break;
            }
            produced += amount;
            offset += amount;
            remaining -= amount;
            if (progress != 0 &&
                !progress(produced, entry.size, progress_context)) {
                result = ZipStatus::Cancelled;
                break;
            }
        }
    } else {
        static_assert(sizeof(tinfl_decompressor) <=
                          sizeof(workspace_->inflate_state),
                      "ZipWorkspace inflate state is too small for miniz");
        static_assert(alignof(uint64_t) >= alignof(tinfl_decompressor),
                      "ZipWorkspace inflate state is under-aligned for miniz");
        static_assert(TINFL_LZ_DICT_SIZE == kZipInflateDictionaryBytes,
                      "ZipWorkspace dictionary does not match miniz");
        tinfl_decompressor *const decompressor =
            reinterpret_cast<tinfl_decompressor *>(
                workspace_->inflate_state);
        memset(decompressor, 0, sizeof(*decompressor));
        tinfl_init(decompressor);
        uint8_t *const dictionary = workspace_->dictionary;
        memset(dictionary, 0, kZipInflateDictionaryBytes);
        size_t dictionary_offset = 0U;
        uint64_t remaining = entry.compressed_size;
        uint64_t source_offset = entry.data_offset;
        size_t input_size = 0U;
        size_t input_offset = 0U;
        bool done = false;
        while (!done && result == ZipStatus::Ok) {
            if (input_offset == input_size && remaining != 0U) {
                input_size = static_cast<size_t>(
                    remaining < kZipInputBufferBytes
                        ? remaining
                        : kZipInputBufferBytes);
                if (!source_->ReadAt(source_offset, input, input_size)) {
                    result = ZipStatus::SourceIo;
                    break;
                }
                source_offset += input_size;
                remaining -= input_size;
                input_offset = 0U;
            }

            size_t consumed = input_size - input_offset;
            size_t output_size = TINFL_LZ_DICT_SIZE - dictionary_offset;
            const mz_uint32 inflate_flags = remaining != 0U
                                                ? static_cast<mz_uint32>(
                                                      TINFL_FLAG_HAS_MORE_INPUT)
                                                : 0U;
            const tinfl_status status = tinfl_decompress(
                decompressor, input + input_offset, &consumed,
                dictionary, dictionary + dictionary_offset, &output_size,
                inflate_flags);
            input_offset += consumed;
            if (produced > entry.size || output_size > entry.size - produced) {
                result = ZipStatus::UncompressedSizeMismatch;
                break;
            }
            if (output_size != 0U) {
                const uint8_t *output = dictionary + dictionary_offset;
                crc = UpdateCrc32(crc, output, output_size);
                if (hash_started &&
                    !hash_sink->Update(ByteView(output, output_size))) {
                    result = ZipStatus::HashSinkError;
                    break;
                }
                if (extraction_started &&
                    !extract_sink->Write(ByteView(output, output_size))) {
                    result = ZipStatus::SinkError;
                    break;
                }
            produced += output_size;
            if (progress != 0 &&
                !progress(produced, entry.size, progress_context)) {
                result = ZipStatus::Cancelled;
                break;
            }
            dictionary_offset =
                    (dictionary_offset + output_size) &
                    (TINFL_LZ_DICT_SIZE - 1U);
            }

            if (status < TINFL_STATUS_DONE) {
                result = ZipStatus::DeflateInvalid;
                break;
            }
            if (status == TINFL_STATUS_DONE) {
                if (remaining != 0U || input_offset != input_size) {
                    result = ZipStatus::DeflateInvalid;
                } else {
                    done = true;
                }
                break;
            }
            if (consumed == 0U && output_size == 0U) {
                if (input_offset == input_size && remaining != 0U) {
                    continue;
                }
                result = ZipStatus::DeflateInvalid;
                break;
            }
            if (status == TINFL_STATUS_NEEDS_MORE_INPUT &&
                input_offset == input_size && remaining == 0U) {
                result = ZipStatus::DeflateInvalid;
                break;
            }
        }
        if (!done && result == ZipStatus::Ok) {
            result = ZipStatus::DeflateInvalid;
        }
    }

    if (result == ZipStatus::Ok && produced != entry.size) {
        result = ZipStatus::UncompressedSizeMismatch;
    }
    crc ^= UINT32_C(0xffffffff);
    if (result == ZipStatus::Ok && crc != entry.crc32) {
        result = ZipStatus::CrcMismatch;
    }

    uint8_t digest[kSha256DigestBytes];
    memset(digest, 0, sizeof(digest));
    if (result == ZipStatus::Ok && hash_started) {
        if (!hash_sink->FinishFile(digest)) {
            result = ZipStatus::HashSinkError;
        } else {
            hash_started = false;
            if (expected->sha256 != 0 &&
                !DigestEqual(digest, expected->sha256)) {
                result = ZipStatus::HashMismatch;
            }
        }
    }
    if (result == ZipStatus::Ok && extraction_started) {
        if (!extract_sink->CommitEntry(entry)) {
            result = ZipStatus::SinkError;
        } else {
            extraction_started = false;
        }
    }
    if (hash_started) {
        hash_sink->AbortFile();
    }
    if (extraction_started) {
        extract_sink->AbortEntry(entry);
    }
    return result;
}

ZipStatus ZipReader::ValidateContents(const ZipExpectedInventory &expected,
                                      ZipHashSink *hash_sink)
{
    ClearValidatedContents();
    bool requires_hash = false;
    const ZipStatus inventory_status =
        VerifyExpectedInventory(expected, &requires_hash);
    if (inventory_status != ZipStatus::Ok) {
        return inventory_status;
    }
    (void)requires_hash;
    if (requires_hash && hash_sink == 0) {
        return ZipStatus::HashSinkRequired;
    }
    uint8_t binding_before[kSha256DigestBytes];
    if (!ComputeExpectedInventoryBinding(expected, binding_before)) {
        return ZipStatus::ExpectedInventoryInvalid;
    }
    for (size_t index = 0U; index < inventory_.entry_count; ++index) {
        const ZipEntry &entry = entries_[index];
        if (entry.is_directory) {
            continue;
        }
        const ZipExpectedFile *wanted = FindExpectedFile(expected, entry.path);
        const ZipStatus status = StreamFile(entry, wanted, hash_sink, 0);
        if (status != ZipStatus::Ok) {
            return status;
        }
    }
    uint8_t binding_after[kSha256DigestBytes];
    if (!ComputeExpectedInventoryBinding(expected, binding_after) ||
        !ConstantTimeDigestEqual(binding_before, binding_after)) {
        return ZipStatus::ExpectedInventoryInvalid;
    }
    memcpy(validated_inventory_binding_, binding_after,
           sizeof(validated_inventory_binding_));
    validated_contents_ = true;
    return ZipStatus::Ok;
}

ZipStatus ZipReader::BindAuthenticatedInventory(
    const ZipExpectedInventory &expected)
{
    bool requires_hash = false;
    const ZipStatus inventory_status =
        VerifyExpectedInventory(expected, &requires_hash);
    if (inventory_status != ZipStatus::Ok) {
        ClearValidatedContents();
        return inventory_status;
    }
    uint8_t binding[kSha256DigestBytes];
    if (!ComputeExpectedInventoryBinding(expected, binding)) {
        ClearValidatedContents();
        return ZipStatus::ExpectedInventoryInvalid;
    }
    memcpy(validated_inventory_binding_, binding,
           sizeof(validated_inventory_binding_));
    validated_contents_ = true;
    return ZipStatus::Ok;
}

ZipStatus ZipReader::ExtractOne(const char *path,
                                const ZipExpectedInventory &expected,
                                ZipExtractSink *sink,
                                ZipHashSink *hash_sink)
{
    if (path == 0 || sink == 0) {
        return ZipStatus::InvalidArgument;
    }
    bool requires_hash = false;
    const ZipStatus inventory_status =
        VerifyExpectedInventory(expected, &requires_hash);
    if (inventory_status != ZipStatus::Ok) {
        ClearValidatedContents();
        return inventory_status;
    }
    (void)requires_hash;
    const bool reuse_validated_contents =
        ValidatedContentsMatch(expected);
    if (!reuse_validated_contents) {
        ClearValidatedContents();
    }
    const ZipEntry *entry = 0;
    for (size_t index = 0U; index < inventory_.entry_count; ++index) {
        if (!entries_[index].is_directory &&
            strcmp(entries_[index].path, path) == 0) {
            entry = &entries_[index];
            break;
        }
    }
    if (entry == 0) {
        ClearValidatedContents();
        return ZipStatus::EntryNotFound;
    }
    const ZipExpectedFile *wanted = FindExpectedFile(expected, path);
    if (wanted == 0) {
        ClearValidatedContents();
        return ZipStatus::InventoryMismatch;
    }
    if (wanted->sha256 != 0 && hash_sink == 0) {
        ClearValidatedContents();
        return ZipStatus::HashSinkRequired;
    }
    if (!reuse_validated_contents) {
        const ZipStatus status = StreamFile(*entry, wanted, hash_sink, 0);
        if (status != ZipStatus::Ok) {
            ClearValidatedContents();
            return status;
        }
    }
    const ZipStatus status = StreamFile(*entry, wanted, hash_sink, sink);
    if (status != ZipStatus::Ok) {
        ClearValidatedContents();
    }
    return status;
}

ZipStatus ZipReader::ExtractOneSinglePass(
    const char *path, const ZipExpectedInventory &expected,
    ZipExtractSink *sink, ZipProgressCallback verify_progress,
    void *verify_context, ZipProgressCallback stream_progress,
    void *stream_context)
{
    if (path == 0 || sink == 0) {
        return ZipStatus::InvalidArgument;
    }
    bool requires_hash = false;
    const ZipStatus inventory_status = VerifyExpectedInventory(
        expected, &requires_hash, verify_progress, verify_context);
    if (inventory_status != ZipStatus::Ok) {
        ClearValidatedContents();
        return inventory_status;
    }
    const ZipEntry *entry = 0;
    for (size_t index = 0U; index < inventory_.entry_count; ++index) {
        if (!entries_[index].is_directory &&
            strcmp(entries_[index].path, path) == 0) {
            entry = &entries_[index];
            break;
        }
    }
    if (entry == 0) {
        ClearValidatedContents();
        return ZipStatus::EntryNotFound;
    }
    const ZipExpectedFile *wanted = FindExpectedFile(expected, path);
    if (wanted == 0) {
        ClearValidatedContents();
        return ZipStatus::InventoryMismatch;
    }
    // No validation pre-pass: with sha256 == NULL (browse callers) the only
    // checks are CRC-32 and sizes, which StreamFile verifies before
    // CommitEntry runs. AbortEntry runs on any failure, so the caller only
    // keeps the output when this returns Ok.
    const ZipStatus status =
        StreamFile(*entry, wanted, 0, sink, stream_progress, stream_context);
    if (status != ZipStatus::Ok) {
        ClearValidatedContents();
    }
    return status;
}

ZipStatus ZipReader::ExtractAll(const ZipExpectedInventory &expected,
                                ZipExtractSink *sink,
                                ZipHashSink *hash_sink)
{
    if (sink == 0) {
        ClearValidatedContents();
        return ZipStatus::InvalidArgument;
    }
    ZipStatus status = ValidateContents(expected, hash_sink);
    if (status != ZipStatus::Ok) {
        return status;
    }

    // Explicit directories are presented before files, independent of ZIP
    // order, so a filesystem sink can create them safely.
    for (size_t index = 0U; index < inventory_.entry_count; ++index) {
        const ZipEntry &entry = entries_[index];
        if (!entry.is_directory) {
            continue;
        }
        if (!sink->BeginEntry(entry)) {
            ClearValidatedContents();
            return ZipStatus::SinkError;
        }
        if (!sink->CommitEntry(entry)) {
            sink->AbortEntry(entry);
            ClearValidatedContents();
            return ZipStatus::SinkError;
        }
    }
    for (size_t index = 0U; index < inventory_.entry_count; ++index) {
        const ZipEntry &entry = entries_[index];
        if (entry.is_directory) {
            continue;
        }
        const ZipExpectedFile *wanted = FindExpectedFile(expected, entry.path);
        status = StreamFile(entry, wanted, hash_sink, sink);
        if (status != ZipStatus::Ok) {
            ClearValidatedContents();
            return status;
        }
    }
    return ZipStatus::Ok;
}

const char *ZipStatusString(ZipStatus status)
{
    switch (status) {
    case ZipStatus::Ok: return "ok";
    case ZipStatus::InvalidArgument: return "invalid argument";
    case ZipStatus::NotOpen: return "reader is not open";
    case ZipStatus::SourceIo: return "ZIP source I/O failed";
    case ZipStatus::ArchiveSizeInvalid: return "archive size is invalid";
    case ZipStatus::EndRecordInvalid: return "ZIP32 end record is invalid";
    case ZipStatus::ArchiveCommentForbidden: return "archive comments are forbidden";
    case ZipStatus::MultiDiskForbidden: return "multi-disk ZIP is forbidden";
    case ZipStatus::Zip64Forbidden: return "Zip64 is forbidden";
    case ZipStatus::EntryCountInvalid: return "entry count is invalid";
    case ZipStatus::StorageTooSmall: return "entry storage is too small";
    case ZipStatus::CentralDirectoryInvalid: return "central directory is invalid";
    case ZipStatus::UnsupportedVersion: return "ZIP version is unsupported";
    case ZipStatus::UnsupportedFlags: return "ZIP flags are unsupported";
    case ZipStatus::UnsupportedCompression: return "compression method is unsupported";
    case ZipStatus::ExtraFieldForbidden: return "ZIP extra fields are forbidden";
    case ZipStatus::MemberCommentForbidden: return "member comments are forbidden";
    case ZipStatus::SpecialFileForbidden: return "special files are forbidden";
    case ZipStatus::DirectoryMetadataInvalid: return "directory metadata is invalid";
    case ZipStatus::PathInvalid: return "ZIP path is unsafe";
    case ZipStatus::PathTooLong: return "ZIP path is too long";
    case ZipStatus::PathEncodingInvalid: return "ZIP path encoding is invalid";
    case ZipStatus::NonAsciiPathUnsupported: return "non-ASCII ZIP paths are unsupported";
    case ZipStatus::DuplicatePath: return "duplicate ZIP path";
    case ZipStatus::FatCaseCollision: return "FAT case-fold path collision";
    case ZipStatus::FileUsedAsDirectory: return "file is used as a directory";
    case ZipStatus::MemberSizeInvalid: return "member size is invalid";
    case ZipStatus::CompressionRatioInvalid: return "compression ratio exceeds limit";
    case ZipStatus::OffsetInvalid: return "ZIP offsets are invalid";
    case ZipStatus::LocalHeaderMismatch: return "local and central headers differ";
    case ZipStatus::InstalledSizeInvalid: return "installed size exceeds limit";
    case ZipStatus::ExpectedInventoryInvalid: return "expected inventory is invalid";
    case ZipStatus::InventoryMismatch: return "manifest inventory differs from ZIP";
    case ZipStatus::EntryNotFound: return "ZIP entry was not found";
    case ZipStatus::HashSinkRequired: return "SHA-256 sink is required";
    case ZipStatus::HashSinkError: return "SHA-256 sink failed";
    case ZipStatus::HashMismatch: return "SHA-256 mismatch";
    case ZipStatus::SinkError: return "extraction sink failed";
    case ZipStatus::WorkspaceRequired: return "ZIP workspace is required";
    case ZipStatus::CrcMismatch: return "CRC-32 mismatch";
    case ZipStatus::UncompressedSizeMismatch: return "uncompressed size mismatch";
    case ZipStatus::DeflateInvalid: return "raw Deflate stream is invalid";
    case ZipStatus::Cancelled: return "extraction cancelled";
    default: return "unknown ZIP status";
    }
}

}  // namespace update
}  // namespace bmx
