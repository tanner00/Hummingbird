static const uint LuminanceHistogramBinsCount = 256;

static const float LuminanceLogMinimum = -20.0f; // Floor(Log2(0.000001f))
static const float LuminanceLogRange = 47.0f; // Ceiling(Log2(100000000.0f) - LuminanceLogMinimum)
