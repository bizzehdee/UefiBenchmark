// AES-NI encryption throughput.
// Chains 10 aesenc + 1 aesenclast rounds on a single block.
// Reports MB/s of data encrypted.

#include "AesBenchmark.h"
#include "CpuFeatures.h"
#include "TimeBox.h"
#include <wmmintrin.h>   // _mm_aesenc_si128 etc. (needs -maes)

// ── Key expansion helper ──────────────────────────────────────

__attribute__((target("aes")))
static __m128i AesKeygenAssist(__m128i key, __m128i assist) {
    assist = _mm_shuffle_epi32(assist, 0xFF);
    key    = _mm_xor_si128(key, _mm_slli_si128(key, 4));
    key    = _mm_xor_si128(key, _mm_slli_si128(key, 4));
    key    = _mm_xor_si128(key, _mm_slli_si128(key, 4));
    return _mm_xor_si128(key, assist);
}

void AesBenchmark::Setup() {
    const auto& feat = CpuFeatures::Get();
    mHasAes = feat.HasAESNI;
    if (!mHasAes) return;

    // Expand a fixed AES-128 key using AES-NI key schedule
    // Key: 000102030405060708090a0b0c0d0e0f
    __m128i rk[11];
    rk[0] = _mm_set_epi8(0x0f,0x0e,0x0d,0x0c,
                          0x0b,0x0a,0x09,0x08,
                          0x07,0x06,0x05,0x04,
                          0x03,0x02,0x01,0x00);

#define AES_EXPAND(i, rcon) \
    rk[(i)] = AesKeygenAssist(rk[(i)-1], \
        _mm_aeskeygenassist_si128(rk[(i)-1], (rcon)))

    AES_EXPAND(1,  0x01); AES_EXPAND(2,  0x02); AES_EXPAND(3,  0x04);
    AES_EXPAND(4,  0x08); AES_EXPAND(5,  0x10); AES_EXPAND(6,  0x20);
    AES_EXPAND(7,  0x40); AES_EXPAND(8,  0x80); AES_EXPAND(9,  0x1b);
    AES_EXPAND(10, 0x36);
#undef AES_EXPAND

    for (int i = 0; i < 11; ++i)
        _mm_storeu_si128(reinterpret_cast<__m128i*>(mRoundKeys + i * 16), rk[i]);
}

// ── AES kernel ────────────────────────────────────────────────

__attribute__((target("aes")))
static void RunAesKernel(UINT64 n, const UINT8* rkeys) {
    __m128i rk[11];
    for (int i = 0; i < 11; ++i)
        rk[i] = _mm_loadu_si128(reinterpret_cast<const __m128i*>(rkeys + i * 16));

    __m128i block = _mm_set_epi32(0xDEADBEEF, 0xCAFEBABE, 0x12345678, 0xABCDEF01);

    for (UINT64 i = 0; i < n; ++i) {
        block = _mm_xor_si128(block, rk[0]);
        block = _mm_aesenc_si128(block, rk[1]);
        block = _mm_aesenc_si128(block, rk[2]);
        block = _mm_aesenc_si128(block, rk[3]);
        block = _mm_aesenc_si128(block, rk[4]);
        block = _mm_aesenc_si128(block, rk[5]);
        block = _mm_aesenc_si128(block, rk[6]);
        block = _mm_aesenc_si128(block, rk[7]);
        block = _mm_aesenc_si128(block, rk[8]);
        block = _mm_aesenc_si128(block, rk[9]);
        block = _mm_aesenclast_si128(block, rk[10]);
    }

    volatile __m128i sink = block;
    (void)sink;
}

// ── RunCore ───────────────────────────────────────────────────

void AesBenchmark::RunCore(UINT32 /*workerIndex*/, UINT32 /*totalWorkers*/) {
    if (!mHasAes) return;

    const UINT8* rkeys = mRoundKeys;

    UINT64 localIter = TimeBox::RunWithProgress(mBudgetUs, CHUNK_SIZE,
        [rkeys](UINT64 n) { RunAesKernel(n, rkeys); },
        [this](UINT64 e, UINT64) { TryReportProgress(e); });

    __atomic_fetch_add(const_cast<UINT64*>(&mTotalIter), localIter, __ATOMIC_RELAXED);
}
