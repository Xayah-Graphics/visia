module;
#include <Windows.h>
#include <miniz.h>
#include <nlohmann/json.hpp>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>

module visia.document;
import visia.canvas;
import std;

namespace visia {
    namespace {
        void require_supported_ratio(const int width, const int height) {
            const int common = std::gcd(width, height);
            const int horizontal = width / common, vertical = height / common;
            constexpr std::array ratios{std::pair{1, 1}, std::pair{4, 3}, std::pair{3, 2}, std::pair{8, 5}, std::pair{16, 9}, std::pair{7, 3}};
            for (const auto [x, y] : ratios) if ((horizontal == x && vertical == y) || (horizontal == y && vertical == x)) return;
            throw std::runtime_error{"Unsupported image aspect ratio: use 1:1, 4:3, 3:2, 16:10, 16:9, or 21:9 (landscape or portrait)"};
        }

        struct ArchiveReader {
            std::ifstream file;
            mz_zip_archive zip{};
            bool active{};

            explicit ArchiveReader(const std::filesystem::path& path) : file{path, std::ios::binary | std::ios::ate} {
                if (!file) throw std::runtime_error{"Cannot open Visia document"};
                const auto length = static_cast<mz_uint64>(file.tellg());
                zip.m_pIO_opaque = &file;
                zip.m_pRead = [](void* opaque, const mz_uint64 offset, void* data, const size_t bytes) -> size_t {
                    auto& input = *static_cast<std::ifstream*>(opaque);
                    input.clear();
                    input.seekg(static_cast<std::streamoff>(offset));
                    input.read(static_cast<char*>(data), static_cast<std::streamsize>(bytes));
                    return static_cast<size_t>(input.gcount());
                };
                if (!mz_zip_reader_init(&zip, length, 0)) throw std::runtime_error{"Cannot read Visia ZIP document"};
                active = true;
            }

            ~ArchiveReader() {
                if (active) mz_zip_reader_end(&zip);
            }

            [[nodiscard]] std::vector<std::uint8_t> extract(const std::string& name) {
                const int index = mz_zip_reader_locate_file(&zip, name.c_str(), nullptr, 0);
                if (index < 0) throw std::runtime_error{"Missing Visia document entry: " + name};
                mz_zip_archive_file_stat info{};
                if (!mz_zip_reader_file_stat(&zip, static_cast<mz_uint>(index), &info)) throw std::runtime_error{"Cannot read Visia entry metadata"};
                std::vector<std::uint8_t> bytes(static_cast<size_t>(info.m_uncomp_size));
                if (!mz_zip_reader_extract_to_mem(&zip, static_cast<mz_uint>(index), bytes.data(), bytes.size(), 0)) throw std::runtime_error{"Cannot extract Visia document entry: " + name};
                return bytes;
            }
        };

        struct ArchiveWriter {
            std::ofstream file;
            mz_zip_archive zip{};
            bool active{};

            explicit ArchiveWriter(const std::filesystem::path& path) : file{path, std::ios::binary | std::ios::trunc} {
                if (!file) throw std::runtime_error{"Cannot create Visia document"};
                zip.m_pIO_opaque = &file;
                zip.m_pWrite = [](void* opaque, const mz_uint64 offset, const void* data, const size_t bytes) -> size_t {
                    auto& output = *static_cast<std::ofstream*>(opaque);
                    output.seekp(static_cast<std::streamoff>(offset));
                    output.write(static_cast<const char*>(data), static_cast<std::streamsize>(bytes));
                    return output ? bytes : 0;
                };
                if (!mz_zip_writer_init(&zip, 0)) throw std::runtime_error{"Cannot start Visia ZIP document"};
                active = true;
            }

            ~ArchiveWriter() {
                if (active) mz_zip_writer_end(&zip);
            }

            void add(const std::string& name, const void* data, const size_t bytes) {
                if (!mz_zip_writer_add_mem(&zip, name.c_str(), data, bytes, 0)) throw std::runtime_error{"Cannot write Visia document entry: " + name};
            }

            void finish() {
                if (!mz_zip_writer_finalize_archive(&zip)) throw std::runtime_error{"Cannot finish Visia ZIP document"};
                mz_zip_writer_end(&zip);
                active = false;
                file.flush();
                if (!file) throw std::runtime_error{"Cannot flush Visia document"};
                file.close();
                if (!file) throw std::runtime_error{"Cannot close Visia document"};
            }
        };
    }

    Picture read_png(const std::filesystem::path& path) {
        std::ifstream file{path, std::ios::binary | std::ios::ate};
        file.exceptions(std::ios::badbit | std::ios::failbit);
        std::vector<std::uint8_t> bytes(static_cast<size_t>(file.tellg()));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        int width{}, height{}, channels{};
        if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels) || stbi_is_16_bit_from_memory(bytes.data(), static_cast<int>(bytes.size()))) throw std::runtime_error{"Expected a static 8-bit PNG"};
        require_supported_ratio(width, height);
        return Picture{0, std::move(bytes), width, height};
    }

    Canvas open_document(const std::filesystem::path& path) {
        ArchiveReader archive{path};
        const auto data = archive.extract("manifest.json");
        const auto manifest = nlohmann::json::parse(data.begin(), data.end());
        if (manifest.at("version").get<int>() != 1) throw std::runtime_error{"Unsupported Visia document version"};
        Canvas canvas;
        canvas.path = path;
        for (const auto& item : manifest.at("pictures")) {
            Picture picture;
            picture.id = item.at("id").get<std::uint64_t>();
            picture.width = item.at("width").get<int>();
            picture.height = item.at("height").get<int>();
            require_supported_ratio(picture.width, picture.height);
            picture.x = item.at("x").get<double>();
            picture.y = item.at("y").get<double>();
            picture.scale = item.at("scale").get<double>();
            const int common = std::gcd(picture.width, picture.height);
            const double steps = std::round(picture.scale * common / Canvas::grid);
            if (steps < 1 || picture.scale != steps * Canvas::grid / common) throw std::runtime_error{"Image size is not aligned to the Visia grid"};
            picture.png = archive.extract(std::format("images/{}.png", picture.id));
            canvas.next_id = std::max(canvas.next_id, picture.id + 1);
            canvas.pictures.push_back(std::move(picture));
        }
        if (!canvas.pictures.empty()) {
            canvas.center_x = manifest.at("camera").at("x").get<double>();
            canvas.center_y = manifest.at("camera").at("y").get<double>();
            canvas.zoom = manifest.at("camera").at("zoom").get<double>();
            canvas.normalize();
        }
        return canvas;
    }

    void save_document(Canvas& canvas, const std::filesystem::path& path) {
        auto temporary = path;
        temporary += L".tmp";
        ArchiveWriter archive{temporary};
        const bool empty = canvas.pictures.empty();
        nlohmann::json manifest{{"version", 1}, {"camera", {{"x", empty ? 0.0 : canvas.center_x}, {"y", empty ? 0.0 : canvas.center_y}, {"zoom", empty ? 1.0 : canvas.zoom}}}, {"pictures", nlohmann::json::array()}};
        for (const auto& picture : canvas.pictures) {
            const auto name = std::format("images/{}.png", picture.id);
            archive.add(name, picture.png.data(), picture.png.size());
            manifest["pictures"].push_back({{"id", picture.id}, {"width", picture.width}, {"height", picture.height}, {"x", picture.x}, {"y", picture.y}, {"scale", picture.scale}});
        }
        const auto description = manifest.dump(2);
        archive.add("manifest.json", description.data(), description.size());
        archive.finish();
        if (std::filesystem::exists(path)) {
            if (!ReplaceFileW(path.c_str(), temporary.c_str(), nullptr, 0, nullptr, nullptr)) throw std::runtime_error{"Cannot replace Visia document"};
        } else std::filesystem::rename(temporary, path);
        canvas.path = path;
        canvas.dirty = false;
    }
}
