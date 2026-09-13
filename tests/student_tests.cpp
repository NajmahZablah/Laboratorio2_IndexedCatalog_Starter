#include "doctest/doctest.h"

#include "catalog.hpp"
#include "catalog_codec.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace {

struct Fixture {
    std::string bytes;
    std::vector<std::uint64_t> offsets;
};

Fixture make_fixture(const std::vector<lab2::Record>& records) {
    std::ostringstream output(std::ios::binary | std::ios::out);
    Fixture fixture;
    for (const auto& record : records) {
        std::uint64_t offset = 0;
        std::string error;
        REQUIRE(lab2::write_record(output, record, &offset, error));
        fixture.offsets.push_back(offset);
    }
    fixture.bytes = output.str();
    return fixture;
}

std::stringstream open_fixture(const std::string& bytes) {
    return std::stringstream(bytes, std::ios::binary | std::ios::in | std::ios::out);
}

} // namespace

TEST_CASE("S01 - build_primary_index sobre un archivo vacío produce un índice vacío") {
    auto input = open_fixture("");

    const lab2::PrimaryBuildResult result = lab2::build_primary_index(input);

    CHECK(result.status == lab2::BuildStatus::Ok);
    CHECK(result.entries.empty());
}

TEST_CASE("S02 - read_record_at rechaza un offset desalineado (mitad de otro registro)") {
    const Fixture fixture = make_fixture({
        {"AAA1111", "COMPOSER A", "TITLE A"},
        {"BBB2222", "COMPOSER B", "TITLE B"}
    });
    auto input = open_fixture(fixture.bytes);

    // Apunta a un byte dentro del header del primer registro, no a su inicio real.
    const std::uint64_t misaligned_offset = fixture.offsets[0] + 5;
    const lab2::ReadResult result = lab2::read_record_at(input, misaligned_offset);

    // Invariante de seguridad: nunca hay Record sin status == Ok.
    CHECK(result.status != lab2::ReadStatus::Ok);
    CHECK_FALSE(result.record.has_value());
}

TEST_CASE("S03 - read_record_at rechaza una versión no soportada") {
    const Fixture fixture = make_fixture({{"ZZZ9999", "COMPOSER Z", "TITLE Z"}});
    std::string bytes = fixture.bytes;

    // El campo version ocupa los bytes [4, 6) del header, little-endian.
    bytes[4] = static_cast<char>(0x02);
    bytes[5] = static_cast<char>(0x00);

    auto input = open_fixture(bytes);
    const lab2::ReadResult result = lab2::read_record_at(input, 0);

    CHECK(result.status == lab2::ReadStatus::UnsupportedVersion);
}

TEST_CASE("S04 - find_by_composer retorna un span vacío cuando el compositor no existe") {
    const lab2::ComposerIndex index{
        {"BACH", {"BWV1001"}},
        {"HANDEL", {"HWV56"}}
    };

    const auto found = lab2::find_by_composer(index, "MOZART");

    CHECK(found.empty());
}

TEST_CASE("S05 - intersect_sorted calcula la intersección sin duplicados") {
    const std::vector<std::string> left{"ANG3795", "COL31809", "DG18807", "WAR23699"};
    const std::vector<std::string> right{"ANG3795", "DG18807", "DG18807", "RCA2626"};

    const auto result = lab2::intersect_sorted(left, right);

    CHECK(result == std::vector<std::string>{"ANG3795", "DG18807"});
}