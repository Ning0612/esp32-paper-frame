// Native PlatformIO tests do not link ESP-IDF component translation units.
// Include the production transaction implementation so this contract is tested
// without maintaining a host-only storage fork.
#include "../../components/pf_storage/image_store.cpp"
#include "../../components/pf_storage/catalog.cpp"
