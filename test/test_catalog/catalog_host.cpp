// Native PlatformIO tests do not link ESP-IDF component translation units.
// Include the storage implementation so the pure catalog contract is tested
// against the same production code without introducing a host-only fork.
#include "../../components/pf_storage/catalog.cpp"
