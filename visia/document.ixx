export module visia.document;
import visia.canvas;
import std;

export namespace visia {
    Picture read_png(const std::filesystem::path& path);
    Canvas open_document(const std::filesystem::path& path);
    void save_document(Canvas& canvas, const std::filesystem::path& path);
}
