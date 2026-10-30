// rxbufself.cpp —— RxBuffer（§117 真环形）语义自测 + 吞吐记录
// 用法：rxbufself.exe            → 逐条 print "rxbuf <i> ok/MISMATCH …"，退出码 0/1
//       rxbufself.exe --bench    → 4MB/64KB 块灌入 1MB 档，打印 ms 与 MB/s（出的是记录，不是门限）
// 为什么单独一个 harness：产品里 RxBuffer 的字节出口只有「保存数据」的 bin 导出（走文件对话框，
// 探针驱不动），所以字节级语义在这里做白盒断言；产品级回归（cap 染色/占用）仍由 verify_r77 覆盖。
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <vector>
#include "../src/core/rxbuf.h"

using namespace sncom;

static int gPass = 0, gTotal = 0;

static void chk(int idx, bool ok, const wchar_t* note) {
    ++gTotal;
    gPass += ok;
    wprintf(L"rxbuf %d %s %s\n", idx, ok ? L"ok" : L"MISMATCH", note);
}

static uint64_t NowMs() { return GetTickCount64(); }

// §117 前的实现（vector 尾插 + 头部 erase 裁剪），就地内联为对照：语义相同但每次裁剪搬整段
struct OldBuf {
    std::vector<uint8_t> v;
    size_t cap;
    explicit OldBuf(size_t c) : cap(c < 1024 ? 1024 : c) {}
    void append(const uint8_t* d, size_t n) {
        v.insert(v.end(), d, d + n);
        if (v.size() > cap) v.erase(v.begin(), v.begin() + (v.size() - cap));
    }
};

int wmain(int argc, wchar_t** argv) {
    if (argc >= 2 && wcscmp(argv[1], L"--bench") == 0) {
        const int rounds = 2048;                     // 2048 × 64KB = 128MB（满档后持续丢弃最旧）
        std::vector<uint8_t> chunk(64 * 1024);
        for (size_t i = 0; i < chunk.size(); ++i) chunk[i] = (uint8_t)(i * 31 + 7);
        LARGE_INTEGER f, a, b2;
        QueryPerformanceFrequency(&f);

        RxBuffer b(1024 * 1024);
        QueryPerformanceCounter(&a);
        for (int r = 0; r < rounds; ++r) {
            chunk[0] = (uint8_t)r;
            b.append(chunk.data(), chunk.size());
        }
        QueryPerformanceCounter(&b2);
        double usNew = (double)(b2.QuadPart - a.QuadPart) * 1e6 / (double)f.QuadPart;

        OldBuf o(1024 * 1024);
        QueryPerformanceCounter(&a);
        for (int r = 0; r < rounds; ++r) {
            chunk[0] = (uint8_t)r;
            o.append(chunk.data(), chunk.size());
        }
        QueryPerformanceCounter(&b2);
        double usOld = (double)(b2.QuadPart - a.QuadPart) * 1e6 / (double)f.QuadPart;

        double mbps = usNew > 0 ? (double)(rounds * 64) / 1024.0 * 1e6 / usNew : 0.0;
        wprintf(L"rxbuf bench append=%dKBx%d cap=%dKB new=%.1fms old=%.1fms MBps=%.1f size=%d dropped=%llu\n",
                (int)(chunk.size() / 1024), rounds, (int)(b.cap() / 1024),
                usNew / 1000.0, usOld / 1000.0, mbps,
                (int)b.size(), (unsigned long long)b.dropped);
        return 0;
    }

    // 1. 基本追加/偏移
    {
        RxBuffer b(1024);
        uint8_t d[10];
        for (int i = 0; i < 10; ++i) d[i] = (uint8_t)(0xA0 + i);
        b.append(d, 10);
        bool ok = b.size() == 10 && b.start_abs() == 0 && b.end_abs() == 10 && b.dropped == 0 &&
                  b.total_rx == 10;
        chk(1, ok, L"append/offsets");
    }
    // 2. read：整段/部分/基线前夹取/越尾夹取
    {
        RxBuffer b(1024);
        uint8_t d[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        b.append(d, 8);
        std::vector<uint8_t> r1 = b.read(0, 8), r2 = b.read(2, 3), r3 = b.read(5, 100);
        std::vector<uint8_t> r4 = b.read(100, 4);
        bool ok = r1.size() == 8 && r1[0] == 1 && r1[7] == 8 &&
                  r2.size() == 3 && r2[0] == 3 && r2[2] == 5 &&
                  r3.size() == 3 && r3[0] == 6 && r3[2] == 8 && r4.empty();
        chk(2, ok, L"read clamp");
    }
    // 3. tail：0 / 部分 / 超长
    {
        RxBuffer b(1024);
        uint8_t d[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        b.append(d, 8);
        std::vector<uint8_t> t0 = b.tail(0), t1 = b.tail(3), t2 = b.tail(100);
        bool ok = t0.empty() && t1.size() == 3 && t1[0] == 6 && t1[2] == 8 &&
                  t2.size() == 8 && t2[0] == 1;
        chk(3, ok, L"tail");
    }
    // 4. 溢出：cap=1024 灌 2000 → 只留最后 1024，绝对偏移前移，dropped 记账
    {
        RxBuffer b(1024);
        std::vector<uint8_t> d(2000);
        for (size_t i = 0; i < d.size(); ++i) d[i] = (uint8_t)(i & 0xFF);
        b.append(d.data(), d.size());
        std::vector<uint8_t> all = b.read(b.start_abs(), b.size());
        bool ok = b.size() == 1024 && b.start_abs() == 976 && b.dropped == 976 &&
                  b.total_rx == 2000 && all.size() == 1024 && all[0] == d[976] && all[1023] == d[1999];
        chk(4, ok, L"overflow trims oldest");
    }
    // 5. 环绕：在（4）的态上再追 100 字节，强制跨越存储末尾回卷，内容仍逐字节正确
    {
        RxBuffer b(1024);
        std::vector<uint8_t> d(2000);
        for (size_t i = 0; i < d.size(); ++i) d[i] = (uint8_t)(i & 0xFF);
        b.append(d.data(), d.size());
        uint8_t more[100];
        for (int i = 0; i < 100; ++i) more[i] = (uint8_t)(0x40 + i);
        b.append(more, 100);
        std::vector<uint8_t> all = b.read(b.start_abs(), b.size());
        bool ok = b.size() == 1024 && b.start_abs() == 1076;
        for (int i = 0; ok && i < 100; ++i) ok = (all[1024 - 100 + i] == more[i]);
        if (ok) ok = all[0] == d[1076] && all[923] == d[1999];
        chk(5, ok, L"wrap-around read");
    }
    // 6. 单块 ≥ cap：只留该块最后 cap 字节
    {
        RxBuffer b(1024);
        std::vector<uint8_t> d(5000);
        for (size_t i = 0; i < d.size(); ++i) d[i] = (uint8_t)(i * 7);
        b.append(d.data(), d.size());
        std::vector<uint8_t> all = b.read(b.start_abs(), b.size());
        bool ok = b.size() == 1024 && b.start_abs() == 3976 && b.dropped == 3976 &&
                  all[0] == d[3976] && all[1023] == d[4999] && b.total_rx == 5000;
        chk(6, ok, L"single append >= cap");
    }
    // 7. set_cap 收小档位：丢最旧到新 cap（注意 cap 有 1024 下限：2048 → 1024）
    {
        RxBuffer b(2048);
        std::vector<uint8_t> d(1500);
        for (size_t i = 0; i < d.size(); ++i) d[i] = (uint8_t)(i & 0xFF);
        b.append(d.data(), d.size());
        b.set_cap(512);                              // 被 clamp 到 1024
        std::vector<uint8_t> all = b.read(b.start_abs(), b.size());
        bool ok = b.cap() == 1024 && b.size() == 1024 && b.start_abs() == 476 &&
                  all[0] == d[476] && all[1023] == d[1499] && b.dropped == 476;
        chk(7, ok, L"set_cap shrink (floor 1024)");
    }
    // 8. clear：偏移归零、容量保留、dropped/total 保留（与旧实现同语义）
    {
        RxBuffer b(1024);
        std::vector<uint8_t> d(3000);
        for (size_t i = 0; i < d.size(); ++i) d[i] = (uint8_t)i;
        b.append(d.data(), d.size());
        b.clear();
        bool ok = b.size() == 0 && b.start_abs() == 0 && b.end_abs() == 0 &&
                  b.dropped == 1976 && b.total_rx == 3000 && b.tail(8).empty();
        chk(8, ok, L"clear");
    }
    // 9. 多轮 1 字节追加 + 全量读，验证跨环绕后 read 基线仍精确（小步长回归；cap 下限 1024）
    {
        const size_t CAPW = 1024;
        RxBuffer b(CAPW);
        std::vector<uint8_t> ref;                    // 参考模型：最近 ≤1024 字节
        uint8_t v = 0;
        bool ok = true;
        for (int step = 0; step < 3000 && ok; ++step) {
            v = (uint8_t)(v + 3);
            b.append(&v, 1);
            ref.push_back(v);
            if (ref.size() > CAPW) ref.erase(ref.begin(), ref.begin() + (ref.size() - CAPW));
            if (b.size() != ref.size()) { ok = false; break; }
            std::vector<uint8_t> all = b.read(b.start_abs(), b.size());
            if (all != ref) { ok = false; break; }
        }
        chk(9, ok, L"1-byte steps vs reference model");
    }

    wprintf(L"SUMMARY rxbufself %d/%d\n", gPass, gTotal);
    return gPass == gTotal ? 0 : 1;
}
