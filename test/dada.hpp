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

#include <cstddef>
#include <utility>
#include <array>

#include <immer/detail/rbts/bits.hpp>

namespace {

template <typename Iterator>
struct rotator
{
    using value_type = typename std::iterator_traits<Iterator>::value_type;

    Iterator first;
    Iterator last;
    Iterator curr;

    void init(Iterator f, Iterator l)
    { first = f; last = l; curr = f; }

    value_type next()
    {
        if (curr == last) curr = first;
        return *curr++;
    }
};

template <typename Range>
struct range_rotator : rotator<typename Range::iterator>
{
    using base_t = rotator<typename Range::iterator>;

    Range range;

    range_rotator(Range r)
        : range{std::move(r)}
    { base_t::init(range.begin(), range.end()); }
};

template <typename Range>
auto make_rotator(Range r) -> range_rotator<Range>
{
    return { r };
}

inline auto magic_rotator()
{
    return make_rotator(std::array<unsigned, 15>{{
            7, 11, 2,  3,  5,
            7, 11, 13, 17, 19,
            23, 5, 29, 31, 37
        }});
}

struct dada_error {};

struct dadaism;
static dadaism* g_dadaism = nullptr;

struct dadaism
{
    using rotator_t = decltype(magic_rotator());

    rotator_t magic     = magic_rotator();

    unsigned step       = magic.next();
    unsigned count      = 0;
    unsigned happenings = 0;
    unsigned last       = 0;
    bool     toggle     = false;

    struct scope
    {
        bool moved = false;
        dadaism* save_ = g_dadaism;
        scope(scope&& s) { save_ = s.save_; s.moved = true; }
        scope(dadaism* self)  { g_dadaism = self; }
        ~scope() { if (!moved) g_dadaism = save_; }
    };

    static scope disable() { return { nullptr }; }

    scope next()
    {
        toggle = last == happenings;
        last   = happenings;
        step   = toggle ? step : magic.next();
        return { this };
    }

    void dada()
    {
        if (toggle && ++count % step == 0) {
            ++happenings;
            throw dada_error{};
        }
    }
};

inline void dada()
{
    if (g_dadaism)
        g_dadaism->dada();
}

inline bool soft_dada()
{
    try {
        dada();
        return false;
    } catch (dada_error) {
        return true;
    }
}

template <typename Heap>
struct dadaist_heap : Heap
{
    template <typename... Tags>
    static auto allocate(std::size_t s, Tags... tags)
    {
        dada();
        return Heap::allocate(s, tags...);
    }
};

template <typename MP>
struct dadaist_memory_policy : MP
{
    struct heap
    {
        template <std::size_t... Sizes>
        struct apply
        {
            using base = typename MP::heap::template apply<Sizes...>::type;
            using type = dadaist_heap<base>;
        };
    };
};

struct tristan_tzara
{
    tristan_tzara() { dada(); }
    tristan_tzara(const tristan_tzara&) { dada(); }
    tristan_tzara(tristan_tzara&&) { dada(); }
    tristan_tzara& operator= (const tristan_tzara&) { dada(); return *this; }
    tristan_tzara& operator= (tristan_tzara&&) { dada(); return *this; }
};

template <typename T>
struct dadaist : tristan_tzara
{
    T value;
    dadaist(T v) : value{std::move(v)} {}
    operator T() const { return value; }
};

template <typename T>
struct dadaist_vector;

using bits_t = immer::detail::rbts::bits_t;

template <template <class, class, bits_t, bits_t> class V,
          typename T, typename MP, bits_t B, bits_t BL>
struct dadaist_vector<V<T, MP, B, BL>>
{
    using type = V<dadaist<T>, dadaist_memory_policy<MP>, B, BL>;
};

} // anonymous namespace
