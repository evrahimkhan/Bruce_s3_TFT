// Evrahim-only firmware: single-board Arduino variant dispatcher.
// (Upstream Bruce supports ~60 boards via a long #elif chain here; this fork
// builds evrahim-s3 only, so everything else was stripped.)
#if defined(EVRAHIM_S3)
#include "../evrahim-s3/pins_arduino.h"
#else
#error "This firmware supports evrahim-s3 only"
#endif
