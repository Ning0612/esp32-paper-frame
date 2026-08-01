#include "pf_image/pfr1.hpp"

static_assert(pf_image::kPfr1HeaderSize == 32U);
static_assert(pf_image::kPfr1MaxFileBytes == 182528U);
static_assert(pf_image::Pfr1Flags::kCompressed == 0x0008U);
