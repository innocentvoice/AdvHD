#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>
#include <filesystem>
#include <opencv2/opencv.hpp>

namespace fs = std::filesystem;

struct pngentry_t
{
    uint32_t x, y, w, h, offset, size;
};

std::vector<pngentry_t> parse_pna(std::vector<uint8_t>& data)
{
    std::vector<pngentry_t> pngentries;
    uint32_t n = *reinterpret_cast<uint32_t*>(&data[0x10]);
    uint32_t pngoffset = 0x14 + n * 0x28;
    for (uint32_t i = 0; i < n; ++i)
    {
        uint32_t cur = 0x14 + i * 0x28 + 0x8;
        uint32_t x, y, w, h;
        std::memcpy(&x, &data[cur], sizeof(uint32_t));
        std::memcpy(&y, &data[cur + sizeof(uint32_t)], sizeof(uint32_t));
        std::memcpy(&w, &data[cur + sizeof(uint32_t) * 2], sizeof(uint32_t));
        std::memcpy(&h, &data[cur + sizeof(uint32_t) * 3], sizeof(uint32_t));
        cur = 0x14 + i * 0x28 + 0x24;
        uint32_t size;
        std::memcpy(&size, &data[cur], sizeof(uint32_t));
        pngentries.push_back(pngentry_t{ x, y, w, h, pngoffset, size });
        pngoffset += size;
    }
    return pngentries;
}

std::vector<pngentry_t> export_pna(const std::string& inpath, const std::string& outdir = "./out")
{
    std::ifstream fp(inpath, std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(fp)), (std::istreambuf_iterator<char>()));
    fp.close();

    std::vector<pngentry_t> entries = parse_pna(data);

    if (outdir != "")
    {
        std::string inname = fs::path(inpath).filename().stem().string();
        for (size_t i = 0; i < entries.size(); ++i)
        {
           if (entries[i].size == 0) continue;
            std::string outpath = (fs::path(outdir) / (inname + "_" + std::to_string(i) + ".png")).string();
            std::ofstream fp(outpath, std::ios::binary);
            fp.write(reinterpret_cast<const char*>(&data[entries[i].offset]), entries[i].size);
            fp.close();
            std::cout << "read " << entries[i].x << " " << entries[i].y << " " << entries[i].w << " " << entries[i].h << " " << entries[i].offset << " " << entries[i].size << std::endl;
        }
    }

    return entries;
}

std::vector<uint8_t> import_pna(const std::string& indir, const std::string& orgpath, const std::string& outpath = "./out.pna")
{
    std::ifstream fp(orgpath, std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(fp)), (std::istreambuf_iterator<char>()));
    fp.close();
    std::vector<pngentry_t> entries = parse_pna(data);

    // prepare paths
    std::stringstream databuf;
    databuf.write(reinterpret_cast<const char*>(&data[0]), 0x14 + 0x28 * entries.size());
    std::vector<std::string> inpaths;
    for (const auto& entry : fs::directory_iterator(indir))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".png")
        {
            inpaths.push_back(entry.path().string());
        }
    }
    std::vector<std::string> innames;
    std::transform(inpaths.begin(), inpaths.end(), std::back_inserter(innames), [](const std::string& path) {
        return fs::path(path).filename().string();
        });
    std::string orgname = fs::path(orgpath).filename().stem().string();

    // write content
    for (size_t i = 0; i < entries.size(); ++i)
    {
        std::string inname = orgname + "_" + std::to_string(i) + ".png";
        if (std::find(innames.begin(), innames.end(), inname) != innames.end())
        {
            std::string inpath = (fs::path(indir) / inname).string();
            std::cout << "update " << inname
                << ", File offset 0x" << std::hex << entries[i].offset << "->0x" << databuf.tellg() << std::dec
                << ", Filze 0x" << std::hex << entries[i].size << "->0x" << std::hex << fs::file_size(inpath) << std::dec << std::endl;
            cv::Mat img = cv::imread(inpath, cv::IMREAD_UNCHANGED);
            if (img.channels() > 3)
            {
                cv::Mat amask;
                cv::extractChannel(img, amask, 3);
                amask = amask > 0;
                std::vector<cv::Mat> channels;
                cv::split(img, channels);
                channels[0] = channels[0].mul(amask);
                channels[1] = channels[1].mul(amask);
                channels[2] = channels[2].mul(amask);
                cv::merge(channels, img);
            }
            std::vector<unsigned char, std::allocator<unsigned char>> img_data;
            cv::imencode(".png", img, img_data);
            if (img_data.size() > std::numeric_limits<std::streamsize>::max())
            {
                throw std::length_error("Image data size too large");
            }
            std::cout << "Input image size: " << img.total() * img.elemSize() << std::endl;
            std::cout << "Encoded PNG size: " << img_data.size() << std::endl;
            databuf.write(reinterpret_cast<const char*>(img_data.data()), img_data.size());
            entries[i] = pngentry_t{ entries[i].x, entries[i].y, entries[i].w, entries[i].h, static_cast<uint32_t>(databuf.tellg()),static_cast<uint32_t>(img_data.size()) };
        }
        else
        {
            databuf.write(reinterpret_cast<const char*>(&data[entries[i].offset]), entries[i].size);
        }
    }


    //write header
    for (size_t i = 0; i < entries.size(); ++i)
    {
        uint32_t offset = 0x14 + i * 0x28 + 0x24;
        databuf.seekp(offset, std::ios::beg);
        databuf.write(reinterpret_cast<const char*>(&entries[i].size), sizeof(uint32_t));
    }

    if (outpath != "")
    {
        std::ofstream fp(outpath, std::ios::binary);
        fp.write(databuf.str().c_str(), databuf.str().size());
        fp.close();
    }

    return std::vector<uint8_t>(databuf.str().begin(), databuf.str().end());
}

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        std::cout << "How to use" << std::endl;
        std::cout << "For extracting a .PNA file/folder: main e [file/path] [outpath]" << std::endl;
        std::cout << "For repacking a .PNA file/folder: main i [inpath] [originalpath/file] [outpath]" << std::endl;
        return 0;
    }
    if (std::string(argv[1]).compare("e") == 0)
    {
        std::string outpath = (argc > 3) ? std::string(argv[3]) : "./out";
        export_pna(argv[2], outpath);
    }
    else if (std::string(argv[1]).compare("i") == 0)
    {
        std::string outpath = (argc > 4) ? std::string(argv[4]) : "out.pna";
        import_pna(argv[2], argv[3], outpath);
    }
    else
    {
        throw std::invalid_argument("unknown format " + std::string(argv[1]));
    }
    return 0;
}