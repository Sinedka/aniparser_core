#include <api.hpp>
#include <cstdint>

class Core {
public:
  Core();

private:
  void getAnime(uint64_t id);
  void getFeed();
  void getSearch(yani::SearchInput);
};
