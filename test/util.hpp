//
// immer - immutable data structures for C++
// Copyright (C) 2016, 2017 Juan Pedro Bolivar Puente
//
// This file is part of immer.
//
// immer is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// immer is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with immer.  If not, see <http://www.gnu.org/licenses/>.
//

#pragma once

#include "dada.hpp"

#include <boost/range/irange.hpp>
#include <boost/range/join.hpp>
#include <cstddef>

namespace {

struct identity_t
{
    template <typename T>
    decltype(auto) operator() (T&& x)
    {
        return std::forward<decltype(x)>(x);
    }
};

} // anonymous namespace

#if IMMER_SLOW_TESTS
#define CHECK_VECTOR_EQUALS_RANGE_X(v1_, first_, last_, xf_)         \
    [] (auto&& v1, auto&& first, auto&& last, auto&& xf) {           \
        auto size = std::distance(first, last);                      \
        CHECK(static_cast<std::ptrdiff_t>(lsize) == size);           \
        if (static_cast<std::ptrdiff_t>(v1.size()) != size) return;  \
        for (auto j = 0u; j < size; ++j)                             \
            CHECK(xf(v1[j]) == xf(*first++));                        \
    } (v1_, first_, last_, xf_)                                      \
    // CHECK_EQUALS
#else
#define CHECK_VECTOR_EQUALS_RANGE_X(v1_, first_, last_, ...)            \
    [] (auto&& v1, auto&& first, auto&& last, auto&& xf) {              \
        auto size = std::distance(first, last);                         \
        CHECK(static_cast<std::ptrdiff_t>(v1.size()) == size);          \
        if (static_cast<std::ptrdiff_t>(v1.size()) != size) return;     \
        if (size > 0) {                                                 \
            CHECK(xf(v1[0]) == xf(*(first + (0))));                     \
            CHECK(xf(v1[size - 1]) == xf(*(first + (size - 1))));       \
            CHECK(xf(v1[size / 2]) == xf(*(first + (size / 2))));       \
            CHECK(xf(v1[size / 3]) == xf(*(first + (size / 3))));       \
            CHECK(xf(v1[size / 4]) == xf(*(first + (size / 4))));       \
            CHECK(xf(v1[size - 1 - size / 2]) == xf(*(first + (size - 1 - size / 2)))); \
            CHECK(xf(v1[size - 1 - size / 3]) == xf(*(first + (size - 1 - size / 3)))); \
            CHECK(xf(v1[size - 1 - size / 4]) == xf(*(first + (size - 1 - size / 4)))); \
        }                                                               \
        if (size > 1) {                                                 \
            CHECK(xf(v1[1]) == xf(*(first + (1))));                     \
            CHECK(xf(v1[size - 2]) == xf(*(first + (size - 2))));       \
        }                                                               \
        if (size > 2) {                                                 \
            CHECK(xf(v1[2]) == xf(*(first + (2))));                     \
            CHECK(xf(v1[size - 3]) == xf(*(first + (size - 3))));       \
        }                                                               \
    } (v1_, first_, last_, __VA_ARGS__)                                 \
    // CHECK_EQUALS
#endif // IMMER_SLOW_TESTS

#define CHECK_VECTOR_EQUALS_X(v1_, v2_, ...)                            \
    [] (auto&& v1, auto&& v2, auto&& ...xs) {                           \
        CHECK_VECTOR_EQUALS_RANGE_X(v1, v2.begin(), v2.end(), xs...);   \
    } (v1_, v2_, __VA_ARGS__)

#define CHECK_VECTOR_EQUALS_RANGE(v1, b, e)                     \
    CHECK_VECTOR_EQUALS_RANGE_X((v1), (b), (e), identity_t{})

#define CHECK_VECTOR_EQUALS(v1, v2)                             \
    CHECK_VECTOR_EQUALS_X((v1), (v2), identity_t{})

namespace {

template <typename Integer>
auto test_irange(Integer from, Integer to)
{
#if IMMER_SLOW_TESTS
    return boost::irange(from, to);
#else
    assert(to - from > Integer{2});
    return boost::join(
        boost::irange(from, from + Integer{2}),
        boost::join(
            boost::irange(from + Integer{2},
                          to - Integer{2},
                          (to - from) / Integer{5}),
            boost::irange(to - Integer{2}, to)));
#endif
}

struct push_back_fn
{
    template <typename T, typename U>
    auto operator() (T&& v, U&& x)
    {
        return std::forward<T>(v)
            .push_back(std::forward<U>(x));
    }
};

struct push_front_fn
{
    template <typename T, typename U>
    auto operator() (T&& v, U&& x)
    {
        return std::forward<T>(v)
            .push_front(std::forward<U>(x));
    }
};

template <typename VP, typename VT>
struct transient_tester
{
    VP vp;
    VT vt;
    dadaism d = {};
    bool transient = false;

    transient_tester(VP vp)
        : vp{vp}
        , vt{vp.transient()}
    {}

    bool step()
    {
        auto s = d.next();
        if (soft_dada()) {
            transient = !transient;
            if (transient)
                vt = vp.transient();
            else
                vp = vt.persistent();
            return true;
        } else
            return false;
    }
};

template <typename VP>
transient_tester<VP, typename VP::transient_type>
as_transient_tester(VP p)
{
    return { std::move(p) };
}

} // anonymous namespace
