// Mandelbrot escape-time benchmark.
// Iterates a fixed complex-plane grid, repeating until time budget elapses.
// Data-dependent early exit stresses both FP units and the branch predictor.

#include "MandelbrotBenchmark.h"
#include "TimeBox.h"

static int MandelbrotPixel(double cx, double cy, int maxIter) {
    double x = 0.0, y = 0.0;
    for (int i = 0; i < maxIter; ++i) {
        double x2 = x * x, y2 = y * y;
        if (x2 + y2 > 4.0) return i;
        double xy = x * y;
        x = x2 - y2 + cx;
        y = 2.0 * xy + cy;
    }
    return maxIter;
}

void MandelbrotBenchmark::RunCore(UINT32 workerIndex, UINT32 totalWorkers) {
    // Each worker computes a horizontal stripe of the grid
    int rowStart = static_cast<int>(workerIndex) * GRID_H / static_cast<int>(totalWorkers);
    int rowEnd   = static_cast<int>(workerIndex + 1) * GRID_H / static_cast<int>(totalWorkers);

    constexpr double X_MIN = -2.5, X_MAX = 1.0;
    constexpr double Y_MIN = -1.25, Y_MAX = 1.25;
    const double dx = (X_MAX - X_MIN) / GRID_W;
    const double dy = (Y_MAX - Y_MIN) / GRID_H;

    int   rS  = rowStart, rE = rowEnd;
    double xMn = X_MIN,   yMn = Y_MIN;
    double dxv = dx,       dyv = dy;

    UINT64 totalPixels = TimeBox::RunWithProgress(mBudgetUs, CHUNK_PIXELS, [=](UINT64 n) mutable {
        volatile UINT64 iters = 0;
        UINT64 pix = 0;
        for (int row = rS; row < rE && pix < n; ++row) {
            double cy = yMn + row * dyv;
            for (int col = 0; col < GRID_W && pix < n; ++col, ++pix) {
                double cx = xMn + col * dxv;
                iters += static_cast<UINT64>(MandelbrotPixel(cx, cy, MAX_ITER));
            }
        }
        (void)iters;
    }, [this](UINT64 e, UINT64) { TryReportProgress(e); });

    __atomic_fetch_add(const_cast<UINT64*>(&mTotalIter), totalPixels * MAX_ITER, __ATOMIC_RELAXED);
}
