#include "catalog.hpp"

#include "binary_io.hpp"
#include "catalog_codec.hpp"
#include "crc32.hpp"

#include <algorithm>
#include <utility>

namespace lab2 {

const char* to_string(ReadStatus status) {
    switch (status) {
    case ReadStatus::Ok: return "Ok";
    case ReadStatus::NotFound: return "NotFound";
    case ReadStatus::IndexKeyMismatch: return "IndexKeyMismatch";
    case ReadStatus::InvalidOffset: return "InvalidOffset";
    case ReadStatus::TruncatedHeader: return "TruncatedHeader";
    case ReadStatus::BadMagic: return "BadMagic";
    case ReadStatus::UnsupportedVersion: return "UnsupportedVersion";
    case ReadStatus::InvalidLength: return "InvalidLength";
    case ReadStatus::TruncatedPayload: return "TruncatedPayload";
    case ReadStatus::MissingChecksum: return "MissingChecksum";
    case ReadStatus::ChecksumMismatch: return "ChecksumMismatch";
    case ReadStatus::MalformedPayload: return "MalformedPayload";
    }
    return "UnknownReadStatus";
}

const char* to_string(BuildStatus status) {
    switch (status) {
    case BuildStatus::Ok: return "Ok";
    case BuildStatus::ReadError: return "ReadError";
    case BuildStatus::DuplicateKey: return "DuplicateKey";
    }
    return "UnknownBuildStatus";
}

const char* to_string(VerificationIssueType type) {
    switch (type) {
    case VerificationIssueType::UnsortedIndex: return "UnsortedIndex";
    case VerificationIssueType::DuplicateKey: return "DuplicateKey";
    case VerificationIssueType::DuplicateOffset: return "DuplicateOffset";
    case VerificationIssueType::RecordReadError: return "RecordReadError";
    case VerificationIssueType::KeyMismatch: return "KeyMismatch";
    }
    return "UnknownVerificationIssue";
}

ReadResult read_record_at(std::istream& input, std::uint64_t offset) {
    ReadResult result;
    result.offset = offset;
    result.next_offset = offset;

    const auto size = stream_size(input);
    if (!size.has_value() || offset >= *size) {
        result.status = ReadStatus::InvalidOffset;
        result.detail = "El offset solicitado está fuera del archivo.";
        return result;
    }

    if (!seek_absolute(input, offset)) {
        result.status = ReadStatus::InvalidOffset;
        result.detail = "No fue posible posicionar el stream en el offset solicitado.";
        return result;
    }

    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint32_t payload_length = 0;

    if (!read_u32_le(input, magic) ||
        !read_u16_le(input, version) ||
        !read_u32_le(input, payload_length)) {
        result.status = ReadStatus::TruncatedHeader;
        result.detail = "El header está incompleto.";
        return result;
    }

    if (magic != RECORD_MAGIC) {
        result.status = ReadStatus::BadMagic;
        result.detail = "El magic number no corresponde a un registro MUS2.";
        return result;
    }

    if (version != RECORD_VERSION) {
        result.status = ReadStatus::UnsupportedVersion;
        result.detail = "La versión del registro no está soportada.";
        return result;
    }

    if (payload_length == 0 || payload_length > MAX_PAYLOAD_SIZE) {
        result.status = ReadStatus::InvalidLength;
        result.detail = "La longitud del payload es inválida.";
        return result;
    }

    std::vector<std::byte> payload(payload_length);
    if (!read_exact(input, std::span<std::byte>(payload))) {
        result.status = ReadStatus::TruncatedPayload;
        result.detail = "Faltan bytes del payload.";
        return result;
    }

    std::uint32_t stored_crc = 0;
    if (!read_u32_le(input, stored_crc)) {
        result.status = ReadStatus::MissingChecksum;
        result.detail = "Falta el CRC-32 almacenado.";
        return result;
    }

    const std::uint32_t computed_crc = crc32(std::span<const std::byte>(payload));
    if (computed_crc != stored_crc) {
        result.status = ReadStatus::ChecksumMismatch;
        result.detail = "El CRC-32 calculado no coincide con el almacenado.";
        return result;
    }

    PayloadDecodeResult decoded = decode_payload(std::span<const std::byte>(payload));
    if (!decoded.record.has_value()) {
        result.status = ReadStatus::MalformedPayload;
        result.detail = decoded.detail;
        return result;
    }

    result.status = ReadStatus::Ok;
    result.record = std::move(decoded.record);
    result.next_offset = offset + RECORD_HEADER_SIZE + payload_length + RECORD_CHECKSUM_SIZE;
    return result;
}

PrimaryBuildResult build_primary_index(std::istream& input) {
    PrimaryBuildResult result;
    result.status = BuildStatus::Ok;

    const auto size = stream_size(input);
    std::uint64_t offset = 0;

    while (size.has_value() && offset < *size) {
        const ReadResult read = read_record_at(input, offset);
        if (!read.ok()) {
            result.status = BuildStatus::ReadError;
            result.entries.clear();
            result.error_offset = offset;
            result.detail = read.detail;
            return result;
        }

        result.entries.push_back(PrimaryEntry{read.record->label_id, offset});
        offset = read.next_offset;
    }

    std::sort(result.entries.begin(), result.entries.end(),
            [](const PrimaryEntry& a, const PrimaryEntry& b) {
                return a.label_id < b.label_id;
            });

    for (std::size_t i = 1; i < result.entries.size(); ++i) {
        if (result.entries[i].label_id == result.entries[i - 1].label_id) {
            result.status = BuildStatus::DuplicateKey;
            result.error_key = result.entries[i].label_id;
            return result;
        }
    }

    return result;
}

std::optional<std::uint64_t> find_offset(
    std::span<const PrimaryEntry> index,
    std::string_view label_id) {
    std::size_t low = 0;
    std::size_t high = index.size();

    while (low < high) {
        const std::size_t mid = low + (high - low) / 2;
        const std::string_view mid_key = index[mid].label_id;

        if (mid_key == label_id) {
            return index[mid].offset;
        }
        if (mid_key < label_id) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    return std::nullopt;
}

ReadResult find_record(
    std::istream& input,
    std::span<const PrimaryEntry> index,
    std::string_view label_id) {
    const auto offset = find_offset(index, label_id);
    if (!offset.has_value()) {
        return {ReadStatus::NotFound, std::nullopt, 0, 0,
                "La clave no existe en el índice primario."};
    }
    ReadResult result = read_record_at(input, *offset);
    if (result.ok() && result.record->label_id != label_id) {
        result.status = ReadStatus::IndexKeyMismatch;
        result.record.reset();
        result.detail = "La clave del índice no coincide con la clave del registro.";
    }
    return result;
}

ComposerBuildResult build_composer_index(
    std::istream& input,
    std::span<const PrimaryEntry> primary) {
    ComposerBuildResult result;

    std::vector<std::pair<std::string, std::string>> pairs; // (composer, label_id)

    for (const auto& entry : primary) {
        const ReadResult read = read_record_at(input, entry.offset);

        if (!read.ok()) {
            result.skipped.push_back(SkippedRecord{entry.label_id, entry.offset, read.status});
            continue;
        }

        if (read.record->label_id != entry.label_id) {
            result.skipped.push_back(
                SkippedRecord{entry.label_id, entry.offset, ReadStatus::IndexKeyMismatch});
            continue;
        }

        pairs.emplace_back(read.record->composer, read.record->label_id);
    }

    std::sort(pairs.begin(), pairs.end());

    std::size_t i = 0;
    while (i < pairs.size()) {
        ComposerEntry group;
        group.composer = pairs[i].first;

        while (i < pairs.size() && pairs[i].first == group.composer) {
            if (group.label_ids.empty() || group.label_ids.back() != pairs[i].second) {
                group.label_ids.push_back(pairs[i].second);
            }
            ++i;
        }

        result.entries.push_back(std::move(group));
    }

    return result;
}

std::span<const std::string> find_by_composer(
    const ComposerIndex& index,
    std::string_view composer) {
    std::size_t low = 0;
    std::size_t high = index.size();

    while (low < high) {
        const std::size_t mid = low + (high - low) / 2;
        const std::string_view mid_key = index[mid].composer;

        if (mid_key == composer) {
            return index[mid].label_ids;
        }
        if (mid_key < composer) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    return {};
}

VerificationReport verify_primary_index(
    std::istream& input,
    std::span<const PrimaryEntry> index) {
    VerificationReport report;
    report.entries_checked = index.size();

    // 1) Índice desordenado: compara cada clave con la anterior en orden físico.
    for (std::size_t i = 1; i < index.size(); ++i) {
        if (index[i].label_id < index[i - 1].label_id) {
            report.issues.push_back(VerificationIssue{
                VerificationIssueType::UnsortedIndex,
                index[i].label_id,
                index[i].offset,
                ReadStatus::Ok,
                "La clave rompe el orden ascendente respecto a la entrada anterior."});
        }
    }

    // 2) Claves duplicadas, sin importar si el índice está desordenado.
    std::vector<std::size_t> by_key(index.size());
    for (std::size_t i = 0; i < index.size(); ++i) by_key[i] = i;
    std::sort(by_key.begin(), by_key.end(), [&](std::size_t a, std::size_t b) {
        return index[a].label_id < index[b].label_id;
    });
    for (std::size_t i = 1; i < by_key.size(); ++i) {
        if (index[by_key[i]].label_id == index[by_key[i - 1]].label_id) {
            report.issues.push_back(VerificationIssue{
                VerificationIssueType::DuplicateKey,
                index[by_key[i]].label_id,
                index[by_key[i]].offset,
                ReadStatus::Ok,
                "La clave aparece más de una vez en el índice."});
        }
    }

    // 3) Offsets duplicados.
    std::vector<std::size_t> by_offset(index.size());
    for (std::size_t i = 0; i < index.size(); ++i) by_offset[i] = i;
    std::sort(by_offset.begin(), by_offset.end(), [&](std::size_t a, std::size_t b) {
        return index[a].offset < index[b].offset;
    });
    for (std::size_t i = 1; i < by_offset.size(); ++i) {
        if (index[by_offset[i]].offset == index[by_offset[i - 1]].offset) {
            report.issues.push_back(VerificationIssue{
                VerificationIssueType::DuplicateOffset,
                index[by_offset[i]].label_id,
                index[by_offset[i]].offset,
                ReadStatus::Ok,
                "Dos entradas del índice apuntan al mismo offset."});
        }
    }

    // 4) Verificación contra el archivo de datos.
    for (const auto& entry : index) {
        const ReadResult read = read_record_at(input, entry.offset);

        if (!read.ok()) {
            report.issues.push_back(VerificationIssue{
                VerificationIssueType::RecordReadError,
                entry.label_id,
                entry.offset,
                read.status,
                read.detail});
            continue;
        }

        if (read.record->label_id != entry.label_id) {
            report.issues.push_back(VerificationIssue{
                VerificationIssueType::KeyMismatch,
                entry.label_id,
                entry.offset,
                read.status,
                "La clave del índice no coincide con la clave del registro leído."});
            continue;
        }

        ++report.readable_matching_entries;
    }

    return report;
}

std::vector<std::string> intersect_sorted(
    std::span<const std::string> left,
    std::span<const std::string> right) {
    std::vector<std::string> result;

    std::size_t i = 0;
    std::size_t j = 0;

    while (i < left.size() && j < right.size()) {
        if (left[i] < right[j]) {
            ++i;
        } else if (right[j] < left[i]) {
            ++j;
        } else {
            if (result.empty() || result.back() != left[i]) {
                result.push_back(left[i]);
            }
            ++i;
            ++j;
        }
    }

    return result;
}

} // namespace lab2
