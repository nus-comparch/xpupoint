#ifndef __TUPLE_HASH
#define __TUPLE_HASH
#include <functional>
//#include <tuple>
#define tuple pair
#include <utility>
namespace std {
namespace {

// Code from boost
// Reciprocal of the golden ratio helps spread entropy
//     and handles duplicates.
// See Mike Seymour in magic-numbers-in-boosthash-combine:
//     http://stackoverflow.com/questions/4948780

template<class T>
inline void
hash_combine(std::size_t& seed, T const& v)
{
  seed ^= hash<T>()(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

// Recursive template code derived from Matthieu M.
template<class Tuple> // std::tuple_size<Tuple>::value - 1>
struct HashValueImpl
{
  static void apply(size_t& seed, Tuple const& tuple)
  {
    hash_combine(seed, tuple.first);
    hash_combine(seed, tuple.second);
  }
};
}

template<typename... TT>
struct hash<std::tuple<TT...>>
{
  size_t operator()(std::tuple<TT...> const& tt) const
  {
    size_t seed = 0;
    HashValueImpl<std::tuple<TT...>>::apply(seed, tt);
    return seed;
  }
};
}
#endif /* __TUPLE_HASH */
