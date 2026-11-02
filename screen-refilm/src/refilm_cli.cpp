// screen-refilm CLI: apply the effect to a video or image using ffmpeg for I/O.
//
//   screen-refilm in.mp4 out.mp4 [--preset NAME] [--set id=value[,value2]]...
//                [--start SECONDS] [--duration SECONDS] [--threads N]
//   screen-refilm --list

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "refilm_params.h"
#include "refilm_render.h"

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#define POPEN_R "rb"
#define POPEN_W "wb"
#else
#define POPEN_R "r"
#define POPEN_W "w"
#endif

namespace {

std::string quote(const std::string& s) {
#ifdef _WIN32
    return "\"" + s + "\"";
#else
    std::string o = "'";
    for (char c : s) o += (c == '\'') ? std::string("'\\''") : std::string(1, c);
    return o + "'";
#endif
}

bool endsWith(const std::string& s, const char* suf) {
    size_t n = std::strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}

void listParams() {
    size_t n = 0;
    const refilm::ParamDesc* t = refilm::paramTable(&n);
    std::printf("Parameters (--set id=value):\n");
    for (size_t i = 0; i < n; ++i) {
        const auto& d = t[i];
        if (d.type == refilm::kTypeGroup) { std::printf("\n  [%s]\n", d.label); continue; }
        std::printf("  %-20s %-22s default %g", d.id, d.label, d.def);
        if (d.type == refilm::kTypeDouble2D) std::printf(",%g", d.def2);
        if (d.type == refilm::kTypeChoice) {
            std::printf("  (");
            for (int k = 0; k < d.numOptions; ++k) std::printf("%s%d=%s", k ? ", " : "", k, d.options[k]);
            std::printf(")");
        }
        std::printf("\n");
    }
    int pc = 0;
    const refilm::Preset* p = refilm::presetTable(&pc);
    std::printf("\nPresets (--preset):\n");
    for (int i = 1; i < pc; ++i) std::printf("  \"%s\"\n", p[i].name);
}

int usage() {
    std::fprintf(stderr,
                 "usage: screen-refilm INPUT OUTPUT [--preset NAME] [--set id=value]... [--start S] "
                 "[--duration S] [--threads N]\n       screen-refilm --list\n");
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "--list") == 0) { listParams(); return 0; }
    if (argc < 3) return usage();
    std::string in = argv[1], out = argv[2];
    refilm::Params P;
    double start = 0, duration = -1;
    int threads = 0;
    std::vector<std::string> sets;
    std::string presetName;
    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", a.c_str()); std::exit(2); }
            return argv[++i];
        };
        if (a == "--preset") presetName = next();
        else if (a == "--set") sets.push_back(next());
        else if (a == "--start") start = std::atof(next().c_str());
        else if (a == "--duration") duration = std::atof(next().c_str());
        else if (a == "--threads") threads = std::atoi(next().c_str());
        else return usage();
    }
    if (!presetName.empty()) {
        int pc = 0;
        const refilm::Preset* pr = refilm::presetTable(&pc);
        int idx = -1;
        for (int k = 0; k < pc; ++k)
            if (presetName == pr[k].name) idx = k;
        if (idx < 0) { std::fprintf(stderr, "unknown preset '%s' (see --list)\n", presetName.c_str()); return 2; }
        refilm::applyPreset(P, idx);
    }
    for (const std::string& s : sets) {
        size_t eq = s.find('=');
        const refilm::ParamDesc* d = eq == std::string::npos ? nullptr : refilm::findParam(s.substr(0, eq).c_str());
        if (!d || !d->field) { std::fprintf(stderr, "bad --set '%s' (see --list)\n", s.c_str()); return 2; }
        std::string v = s.substr(eq + 1);
        P.*(d->field) = std::atof(v.c_str());
        size_t comma = v.find(',');
        if (d->field2 && comma != std::string::npos) P.*(d->field2) = std::atof(v.c_str() + comma + 1);
    }

    // Probe size and frame rate.
    std::string probe = "ffprobe -v error -select_streams v:0 -show_entries stream=width,height,r_frame_rate "
                        "-of csv=p=0 " + quote(in);
    FILE* pp = popen(probe.c_str(), "r");
    if (!pp) { std::perror("ffprobe"); return 1; }
    int W = 0, H = 0, fn = 0, fd = 1;
    int got = std::fscanf(pp, "%d,%d,%d/%d", &W, &H, &fn, &fd);
    pclose(pp);
    if (got < 2 || W <= 0 || H <= 0) { std::fprintf(stderr, "could not read %s\n", in.c_str()); return 1; }
    double fps = (got == 4 && fn > 0 && fd > 0) ? double(fn) / fd : 25.0;
    char fpsStr[64];
    std::snprintf(fpsStr, sizeof fpsStr, "%d/%d", fn > 0 ? fn : 25, fn > 0 && fd > 0 ? fd : 1);

    const bool still = endsWith(out, ".png") || endsWith(out, ".jpg") || endsWith(out, ".tif") || endsWith(out, ".exr");
    std::string trim;
    if (start > 0) trim += " -ss " + std::to_string(start);
    std::string dec = "ffmpeg -nostdin -v error" + trim + " -i " + quote(in);
    if (duration > 0) dec += " -t " + std::to_string(duration);
    if (still) dec += " -frames:v 1";
    dec += " -f rawvideo -pix_fmt rgb48le -";
    std::string enc = "ffmpeg -v error -y -f rawvideo -pix_fmt rgb48le -s " + std::to_string(W) + "x" + std::to_string(H) +
                      " -r " + fpsStr + " -i -";
    if (still) enc += " -frames:v 1 " + quote(out);
    else {
        enc += trim + " -i " + quote(in);
        if (duration > 0) enc += " -t " + std::to_string(duration);
        enc += " -map 0:v -map 1:a? -c:v libx264 -crf 16 -preset medium -pix_fmt yuv420p -c:a copy -shortest " + quote(out);
    }

    FILE* fin = popen(dec.c_str(), POPEN_R);
    FILE* fout = popen(enc.c_str(), POPEN_W);
    if (!fin || !fout) { std::perror("ffmpeg"); return 1; }

    const size_t n = size_t(W) * H;
    std::vector<uint16_t> raw(n * 3);
    std::vector<float> srcBuf(n * 4), dstBuf(n * 4);
    int frame = 0;
    while (std::fread(raw.data(), sizeof(uint16_t), raw.size(), fin) == raw.size()) {
        // ffmpeg rows are top-down; the renderer wants row 0 at the bottom.
        for (int y = 0; y < H; ++y) {
            const uint16_t* r = raw.data() + size_t(H - 1 - y) * W * 3;
            float* s = srcBuf.data() + size_t(y) * W * 4;
            for (int x = 0; x < W; ++x) {
                s[x * 4 + 0] = r[x * 3 + 0] / 65535.f;
                s[x * 4 + 1] = r[x * 3 + 1] / 65535.f;
                s[x * 4 + 2] = r[x * 3 + 2] / 65535.f;
                s[x * 4 + 3] = 1.f;
            }
        }
        refilm::ConstImage src{srcBuf.data(), W, H, std::ptrdiff_t(W) * 4};
        refilm::Image dst{dstBuf.data(), W, H, std::ptrdiff_t(W) * 4};
        refilm::render(src, dst, start + frame / fps, P, threads);
        for (int y = 0; y < H; ++y) {
            uint16_t* r = raw.data() + size_t(H - 1 - y) * W * 3;
            const float* d = dstBuf.data() + size_t(y) * W * 4;
            for (int x = 0; x < W; ++x)
                for (int c = 0; c < 3; ++c) {
                    float v = d[x * 4 + c];
                    v = v < 0 ? 0 : (v > 1 ? 1 : v);
                    r[x * 3 + c] = uint16_t(v * 65535.f + 0.5f);
                }
        }
        if (std::fwrite(raw.data(), sizeof(uint16_t), raw.size(), fout) != raw.size()) break;
        ++frame;
        std::fprintf(stderr, "\rframe %d", frame);
    }
    std::fprintf(stderr, "\n");
    pclose(fin);
    int rc = pclose(fout);
    return frame > 0 && rc == 0 ? 0 : 1;
}
