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

#include <algorithm>
#include <memory>
#include <numeric>
#include <utility>

#include <immer/config.hpp>
#include <immer/detail/rbts/position.hpp>
#include <immer/detail/rbts/visitor.hpp>

namespace immer {
namespace detail {
namespace rbts {

template <typename T>
struct array_for_visitor
{
    using this_t = array_for_visitor;

    template <typename PosT>
    friend T* visit_inner(this_t, PosT&& pos, size_t idx)
    { return pos.descend(this_t{}, idx); }

    template <typename PosT>
    friend T* visit_leaf(this_t, PosT&& pos, size_t)
    { return pos.node()->leaf(); }
};

template <typename T>
struct region_for_visitor
{
    using this_t = region_for_visitor;
    using result_t = std::tuple<T*, size_t, size_t>;

    template <typename PosT>
    friend result_t visit_inner(this_t, PosT&& pos, size_t idx)
    { return pos.towards(this_t{}, idx); }

    template <typename PosT>
    friend result_t visit_leaf(this_t, PosT&& pos, size_t idx)
    { return { pos.node()->leaf(), pos.index(idx), pos.count() }; }
};

template <typename T>
struct get_visitor
{
    using this_t = get_visitor;

    template <typename PosT>
    friend const T& visit_inner(this_t, PosT&& pos, size_t idx)
    { return pos.descend(this_t{}, idx); }

    template <typename PosT>
    friend const T& visit_leaf(this_t, PosT&& pos, size_t idx)
    { return pos.node()->leaf() [pos.index(idx)]; }
};

struct for_each_chunk_visitor
{
    using this_t = for_each_chunk_visitor;

    template <typename Pos, typename Fn>
    friend void visit_inner(this_t, Pos&& pos, Fn&& fn)
    { pos.each(this_t{}, std::forward<Fn>(fn)); }

    template <typename Pos, typename Fn>
    friend void visit_leaf(this_t, Pos&& pos, Fn&& fn)
    {
        auto data = pos.node()->leaf();
        std::forward<Fn>(fn)(data, data + pos.count());
    }
};

template <typename NodeT>
struct update_visitor
{
    using node_t = NodeT;
    using this_t = update_visitor;

    template <typename Pos, typename Fn>
    friend node_t* visit_relaxed(this_t, Pos&& pos, size_t idx, Fn&& fn)
    {
        auto offset  = pos.index(idx);
        auto count   = pos.count();
        auto node    = node_t::make_inner_sr_n(count, pos.relaxed());
        try {
            auto child = pos.towards_oh(this_t{}, idx, offset, fn);
            node_t::do_copy_inner_sr(node, pos.node(), count);
            node->inner()[offset]->dec_unsafe();
            node->inner()[offset] = child;
            return node;
        } catch (...) {
            node_t::delete_inner_r(node);
            throw;
        }
    }

    template <typename Pos, typename Fn>
    friend node_t* visit_regular(this_t, Pos&& pos, size_t idx, Fn&& fn)
    {
        auto offset  = pos.index(idx);
        auto count   = pos.count();
        auto node    = node_t::make_inner_n(count);
        try {
            auto child = pos.towards_oh_ch(this_t{}, idx, offset, count, fn);
            node_t::do_copy_inner(node, pos.node(), count);
            node->inner()[offset]->dec_unsafe();
            node->inner()[offset] = child;
            return node;
        } catch (...) {
            node_t::delete_inner(node);
            throw;
        }
    }

    template <typename Pos, typename Fn>
    friend node_t* visit_leaf(this_t, Pos&& pos, size_t idx, Fn&& fn)
    {
        auto offset  = pos.index(idx);
        auto node    = node_t::copy_leaf(pos.node(), pos.count());
        try {
            node->leaf()[offset] = std::forward<Fn>(fn) (
                std::move(node->leaf()[offset]));
            return node;
        } catch (...) {
            node_t::delete_leaf(node, pos.count());
            throw;
        }
    };
};

struct dec_visitor
{
    using this_t = dec_visitor;

    template <typename Pos>
    friend void visit_relaxed(this_t, Pos&& p)
    {
        using node_t = node_type<Pos>;
        auto node = p.node();
        if (node->dec()) {
            p.each(this_t{});
            node_t::delete_inner_r(node);
        }
    }

    template <typename Pos>
    friend void visit_regular(this_t, Pos&& p)
    {
        using node_t = node_type<Pos>;
        auto node = p.node();
        if (node->dec()) {
            p.each(this_t{});
            node_t::delete_inner(node);
        }
    }

    template <typename Pos>
    friend void visit_leaf(this_t, Pos&& p)
    {
        using node_t = node_type<Pos>;
        auto node = p.node();
        if (node->dec()) {
            auto count = p.count();
            node_t::delete_leaf(node, count);
        }
    }
};

template <typename NodeT>
void dec_leaf(NodeT* node, count_t n)
{
    make_leaf_sub_pos(node, n).visit(dec_visitor{});
}

template <typename NodeT>
void dec_inner(NodeT* node, shift_t shift, size_t size)
{
    visit_maybe_relaxed_sub(node, shift, size, dec_visitor());
}

template <typename NodeT>
void dec_relaxed(NodeT* node, shift_t shift)
{
    make_relaxed_pos(node, shift, node->relaxed()).visit(dec_visitor());
}

template <typename NodeT>
void dec_regular(NodeT* node, shift_t shift, size_t size)
{
    make_regular_pos(node, shift, size).visit(dec_visitor());
}

template <typename NodeT>
void dec_empty_regular(NodeT* node)
{
    make_empty_regular_pos(node).visit(dec_visitor());
}

template <typename NodeT>
struct get_mut_visitor
{
    using node_t  = NodeT;
    using this_t  = get_mut_visitor;
    using value_t = typename NodeT::value_t;
    using edit_t  = typename NodeT::edit_t;

    template <typename Pos>
    friend value_t& visit_relaxed(this_t, Pos&& pos, size_t idx,
                                  edit_t e, node_t** location)
    {
        auto offset  = pos.index(idx);
        auto count   = pos.count();
        auto node    = pos.node();
        if (node->can_mutate(e)) {
            return pos.towards_oh(this_t{}, idx, offset,
                                  e, &node->inner()[offset]);
        } else {
            auto new_node = node_t::copy_inner_sr_e(e, node, count);
            try {
                auto& res = pos.towards_oh(this_t{}, idx, offset,
                                           e, &new_node->inner()[offset]);
                pos.visit(dec_visitor{});
                *location = new_node;
                return res;
            } catch (...) {
                dec_relaxed(new_node, pos.shift());
                throw;
            }
        }
    }

    template <typename Pos>
    friend value_t& visit_regular(this_t, Pos&& pos, size_t idx,
                                  edit_t e, node_t** location)
    {
        assert(pos.node() == *location);
        auto offset  = pos.index(idx);
        auto count   = pos.count();
        auto node    = pos.node();
        if (node->can_mutate(e)) {
            return pos.towards_oh_ch(this_t{}, idx, offset, count,
                                     e, &node->inner()[offset]);
        } else {
            auto new_node = node_t::copy_inner_e(e, node, count);
            try {
                auto& res = pos.towards_oh_ch(this_t{}, idx, offset, count,
                                              e, &new_node->inner()[offset]);
                pos.visit(dec_visitor{});
                *location = new_node;
                return res;
            } catch (...) {
                dec_regular(new_node, pos.shift(), pos.size());
                throw;
            }
        }
    }

    template <typename Pos>
    friend value_t& visit_leaf(this_t, Pos&& pos, size_t idx,
                               edit_t e, node_t** location)
    {
        assert(pos.node() == *location);
        auto node = pos.node();
        if (node->can_mutate(e)) {
            return node->leaf() [pos.index(idx)];
        } else {
            auto new_node = node_t::copy_leaf_e(e, pos.node(), pos.count());
            pos.visit(dec_visitor{});
            *location = new_node;
            return new_node->leaf() [pos.index(idx)];
        }
    };
};

template <typename NodeT, bool Mutating = true>
struct push_tail_mut_visitor
{
    static constexpr auto B  = NodeT::bits;
    static constexpr auto BL = NodeT::bits_leaf;

    using this_t = push_tail_mut_visitor;
    using this_no_mut_t = push_tail_mut_visitor<NodeT, false>;
    using node_t = NodeT;
    using edit_t = typename NodeT::edit_t;

    template <typename Pos>
    friend node_t* visit_relaxed(this_t, Pos&& pos, edit_t e, node_t* tail, count_t ts)
    {
        auto node        = pos.node();
        auto level       = pos.shift();
        auto idx         = pos.count() - 1;
        auto children    = pos.size(idx);
        auto new_idx     = children == size_t{1} << level || level == BL
            ? idx + 1 : idx;
        auto new_child   = (node_t*){};
        auto mutate      = Mutating && node->can_mutate(e);

        if (new_idx >= branches<B>)
            return nullptr;
        else if (idx == new_idx) {
            new_child = mutate
                ? pos.last_oh_csh(this_t{}, idx, children, e, tail, ts)
                : pos.last_oh_csh(this_no_mut_t{}, idx, children, e, tail, ts);
            if (!new_child) {
                if (++new_idx < branches<B>)
                    new_child = node_t::make_path_e(e, level - B, tail);
                else
                    return nullptr;
            }
        } else
            new_child = node_t::make_path_e(e, level - B, tail);

        if (mutate) {
            auto count = new_idx + 1;
            auto relaxed = node->ensure_mutable_relaxed_n(e, new_idx);
            node->inner()[new_idx]  = new_child;
            relaxed->sizes[new_idx] = pos.size() + ts;
            relaxed->count = count;
            return node;
        } else {
            try {
                auto count    = new_idx + 1;
                auto new_node = node_t::copy_inner_r_e(e, pos.node(), new_idx);
                auto relaxed  = new_node->relaxed();
                new_node->inner()[new_idx] = new_child;
                relaxed->sizes[new_idx] = pos.size() + ts;
                relaxed->count = count;
                if (Mutating) pos.visit(dec_visitor{});
                return new_node;
            } catch (...) {
                auto shift = pos.shift();
                auto size  = new_idx == idx ? children + ts : ts;
                if (shift > BL) {
                    tail->inc();
                    dec_inner(new_child, shift - B, size);
                }
                throw;
            }
        }
    }

    template <typename Pos, typename... Args>
    friend node_t* visit_regular(this_t, Pos&& pos, edit_t e, node_t* tail, Args&&...)
    {
        assert((pos.size() & mask<BL>) == 0);
        auto node        = pos.node();
        auto idx         = pos.index(pos.size() - 1);
        auto new_idx     = pos.index(pos.size() + branches<BL> - 1);
        auto mutate      = Mutating && node->can_mutate(e);
        if (mutate) {
            node->inner()[new_idx] =
                idx == new_idx  ? pos.last_oh(this_t{}, idx, e, tail)
                /* otherwise */ : node_t::make_path_e(e, pos.shift() - B, tail);
            return node;
        } else {
            auto new_parent  = node_t::make_inner_e(e);
            try {
                new_parent->inner()[new_idx] =
                    idx == new_idx  ? pos.last_oh(this_no_mut_t{}, idx, e, tail)
                    /* otherwise */ : node_t::make_path_e(e, pos.shift() - B, tail);
                node_t::do_copy_inner(new_parent, node, new_idx);
                if (Mutating) pos.visit(dec_visitor{});
                return new_parent;
            } catch (...) {
                node_t::delete_inner(new_parent);
                throw;
            }
        }
    }

    template <typename Pos, typename... Args>
    friend node_t* visit_leaf(this_t, Pos&& pos, edit_t e, node_t* tail, Args&&...)
    { IMMER_UNREACHABLE; };
};

template <typename NodeT>
struct push_tail_visitor
{
    static constexpr auto B  = NodeT::bits;
    static constexpr auto BL = NodeT::bits_leaf;

    using this_t = push_tail_visitor;
    using node_t = NodeT;

    template <typename Pos>
    friend node_t* visit_relaxed(this_t, Pos&& pos, node_t* tail, count_t ts)
    {
        auto level       = pos.shift();
        auto idx         = pos.count() - 1;
        auto children    = pos.size(idx);
        auto new_idx     = children == size_t{1} << level || level == BL
            ? idx + 1 : idx;
        auto new_child   = (node_t*){};
        if (new_idx >= branches<B>)
            return nullptr;
        else if (idx == new_idx) {
            new_child = pos.last_oh_csh(this_t{}, idx, children, tail, ts);
            if (!new_child) {
                if (++new_idx < branches<B>)
                    new_child = node_t::make_path(level - B, tail);
                else
                    return nullptr;
            }
        } else
            new_child = node_t::make_path(level - B, tail);
        try {
            auto count       = new_idx + 1;
            auto new_parent  = node_t::copy_inner_r_n(count, pos.node(), new_idx);
            auto new_relaxed = new_parent->relaxed();
            new_parent->inner()[new_idx] = new_child;
            new_relaxed->sizes[new_idx] = pos.size() + ts;
            new_relaxed->count = count;
            return new_parent;
        } catch (...) {
            auto shift = pos.shift();
            auto size  = new_idx == idx ? children + ts : ts;
            if (shift > BL) {
                tail->inc();
                dec_inner(new_child, shift - B, size);
            }
            throw;
        }
    }

    template <typename Pos, typename... Args>
    friend node_t* visit_regular(this_t, Pos&& pos, node_t* tail, Args&&...)
    {
        assert((pos.size() & mask<BL>) == 0);
        auto idx         = pos.index(pos.size() - 1);
        auto new_idx     = pos.index(pos.size() + branches<BL> - 1);
        auto count       = new_idx + 1;
        auto new_parent  = node_t::make_inner_n(count);
        try {
            new_parent->inner()[new_idx] =
                idx == new_idx  ? pos.last_oh(this_t{}, idx, tail)
                /* otherwise */ : node_t::make_path(pos.shift() - B, tail);
        } catch (...) {
            node_t::delete_inner(new_parent);
            throw;
        }
        return node_t::do_copy_inner(new_parent, pos.node(), new_idx);
    }

    template <typename Pos, typename... Args>
    friend node_t* visit_leaf(this_t, Pos&& pos, node_t* tail, Args&&...)
    { IMMER_UNREACHABLE; };
};

struct dec_right_visitor
{
    using this_t = dec_right_visitor;
    using dec_t  = dec_visitor;

    template <typename Pos>
    friend void visit_relaxed(this_t, Pos&& p, count_t idx)
    {
        using node_t = node_type<Pos>;
        auto node = p.node();
        if (node->dec()) {
            p.each_right(dec_t{}, idx);
            node_t::delete_inner_r(node);
        }
    }

    template <typename Pos>
    friend void visit_regular(this_t, Pos&& p, count_t idx)
    {
        using node_t = node_type<Pos>;
        auto node = p.node();
        if (node->dec()) {
            p.each_right(dec_t{}, idx);
            node_t::delete_inner(node);
        }
    }

    template <typename Pos>
    friend void visit_leaf(this_t, Pos&& p, count_t idx)
    { IMMER_UNREACHABLE; }
};

template <typename NodeT, bool Collapse=true, bool Mutating=true>
struct slice_right_mut_visitor
{
    using node_t = NodeT;
    using this_t = slice_right_mut_visitor;
    using edit_t = typename NodeT::edit_t;

    // returns a new shift, new root, the new tail size and the new tail
    using result_t = std::tuple<shift_t, NodeT*, count_t, NodeT*>;
    using no_collapse_t = slice_right_mut_visitor<NodeT, false, true>;
    using no_collapse_no_mut_t = slice_right_mut_visitor<NodeT, false, false>;
    using no_mut_t = slice_right_mut_visitor<NodeT, Collapse, false>;

    static constexpr auto B  = NodeT::bits;
    static constexpr auto BL = NodeT::bits_leaf;

    template <typename PosT>
    friend result_t visit_relaxed(this_t, PosT&& pos, size_t last, edit_t e)
    {
        auto idx = pos.index(last);
        auto node = pos.node();
        auto mutate = Mutating && node->can_mutate(e);
        if (Collapse && idx == 0) {
            auto res = mutate
                ? pos.towards_oh(this_t{}, last, idx, e)
                : pos.towards_oh(no_mut_t{}, last, idx, e);
            if (Mutating) pos.visit(dec_right_visitor{}, count_t{1});
            return res;
        } else {
            using std::get;
            auto subs = mutate
                ? pos.towards_oh(no_collapse_t{}, last, idx, e)
                : pos.towards_oh(no_collapse_no_mut_t{}, last, idx, e);
            auto next = get<1>(subs);
            auto ts   = get<2>(subs);
            auto tail = get<3>(subs);
            try {
                if (next) {
                    if (mutate) {
                        auto nodr = node->ensure_mutable_relaxed_n(e, idx);
                        pos.each_right(dec_visitor{}, idx + 1);
                        node->inner()[idx] = next;
                        nodr->sizes[idx] = last + 1 - ts;
                        nodr->count = idx + 1;
                        return { pos.shift(), node, ts, tail };
                    } else {
                        auto newn = node_t::copy_inner_r_e(e, node, idx);
                        auto newr = newn->relaxed();
                        newn->inner()[idx] = next;
                        newr->sizes[idx] = last + 1 - ts;
                        newr->count = idx + 1;
                        if (Mutating) pos.visit(dec_visitor{});
                        return { pos.shift(), newn, ts, tail };
                    }
                } else if (idx == 0) {
                    if (Mutating) pos.visit(dec_right_visitor{}, count_t{1});
                    return { pos.shift(), nullptr, ts, tail };
                } else if (Collapse && idx == 1 && pos.shift() > BL) {
                    auto newn = pos.node()->inner()[0];
                    if (Mutating) pos.visit(dec_right_visitor{}, count_t{2});
                    return { pos.shift() - B, newn, ts, tail };
                } else {
                    if (mutate) {
                        pos.each_right(dec_visitor{}, idx + 1);
                        node->ensure_mutable_relaxed_n(e, idx)->count = idx;
                        return { pos.shift(), node, ts, tail };
                    } else {
                        auto newn = node_t::copy_inner_r_e(e, node, idx);
                        if (Mutating) pos.visit(dec_visitor{});
                        return { pos.shift(), newn, ts, tail };
                    }
                }
            } catch (...) {
                assert(!mutate);
                assert(!next || pos.shift() > BL);
                if (next)
                    dec_inner(next, pos.shift() - B,
                              last + 1 - ts - pos.size_before(idx));
                dec_leaf(tail, ts);
                throw;
            }
        }
    }

    template <typename PosT>
    friend result_t visit_regular(this_t, PosT&& pos, size_t last, edit_t e)
    {
        auto idx = pos.index(last);
        auto node = pos.node();
        auto mutate = Mutating && node->can_mutate(e);
        if (Collapse && idx == 0) {
            auto res = mutate
                ? pos.towards_oh(this_t{}, last, idx, e)
                : pos.towards_oh(no_mut_t{}, last, idx, e);
            if (Mutating) pos.visit(dec_right_visitor{}, count_t{1});
            return res;
        } else {
            using std::get;
            auto subs = mutate
                ? pos.towards_oh(no_collapse_t{}, last, idx, e)
                : pos.towards_oh(no_collapse_no_mut_t{}, last, idx, e);
            auto next = get<1>(subs);
            auto ts   = get<2>(subs);
            auto tail = get<3>(subs);
            try {
                if (next) {
                    if (mutate) {
                        node->inner()[idx] = next;
                        pos.each_right(dec_visitor{}, idx + 1);
                        return { pos.shift(), node, ts, tail };
                    } else {
                        auto newn  = node_t::copy_inner_e(e, node, idx);
                        newn->inner()[idx] = next;
                        if (Mutating) pos.visit(dec_visitor{});
                        return { pos.shift(), newn, ts, tail };
                    }
                } else if (idx == 0) {
                    if (Mutating) pos.visit(dec_right_visitor{}, count_t{1});
                    return { pos.shift(), nullptr, ts, tail };
                } else if (Collapse && idx == 1 && pos.shift() > BL) {
                    auto newn = pos.node()->inner()[0];
                    if (Mutating) pos.visit(dec_right_visitor{}, count_t{2});
                    return { pos.shift() - B, newn, ts, tail };
                } else {
                    if (mutate) {
                        pos.each_right(dec_visitor{}, idx + 1);
                        return { pos.shift(), node, ts, tail };
                    } else {
                        auto newn = node_t::copy_inner_e(e, node, idx);
                        if (Mutating) pos.visit(dec_visitor{});
                        return { pos.shift(), newn, ts, tail };
                    }
                }
            } catch (...) {
                assert(!mutate);
                assert(!next || pos.shift() > BL);
                assert(tail);
                if (next) dec_regular(next, pos.shift() - B, last + 1 - ts);
                dec_leaf(tail, ts);
                throw;
            }
        }
    }

    template <typename PosT>
    friend result_t visit_leaf(this_t, PosT&& pos, size_t last, edit_t e)
    {
        auto old_tail_size = pos.count();
        auto new_tail_size = pos.index(last) + 1;
        auto node          = pos.node();
        auto mutate        = Mutating && node->can_mutate(e);
        if (new_tail_size == old_tail_size) {
            if (!Mutating) node->inc();
            return { 0, nullptr, new_tail_size, node };
        } else if (mutate) {
            destroy_n(node->leaf() + new_tail_size,
                      old_tail_size - new_tail_size);
            return { 0, nullptr, new_tail_size, node };
        } else {
            auto new_tail = node_t::copy_leaf_e(e, node, new_tail_size);
            if (Mutating) pos.visit(dec_visitor{});
            return { 0, nullptr, new_tail_size, new_tail };
        }
    };
};

template <typename NodeT, bool Collapse=true>
struct slice_right_visitor
{
    using node_t = NodeT;
    using this_t = slice_right_visitor;

    // returns a new shift, new root, the new tail size and the new tail
    using result_t = std::tuple<shift_t, NodeT*, count_t, NodeT*>;
    using no_collapse_t = slice_right_visitor<NodeT, false>;

    static constexpr auto B  = NodeT::bits;
    static constexpr auto BL = NodeT::bits_leaf;

    template <typename PosT>
    friend result_t visit_relaxed(this_t, PosT&& pos, size_t last)
    {
        auto idx = pos.index(last);
        if (Collapse && idx == 0) {
            return pos.towards_oh(this_t{}, last, idx);
        } else {
            using std::get;
            auto subs = pos.towards_oh(no_collapse_t{}, last, idx);
            auto next = get<1>(subs);
            auto ts   = get<2>(subs);
            auto tail = get<3>(subs);
            try {
                if (next) {
                    auto count = idx + 1;
                    auto newn  = node_t::copy_inner_r_n(count, pos.node(), idx);
                    auto newr  = newn->relaxed();
                    newn->inner()[idx] = next;
                    newr->sizes[idx] = last + 1 - ts;
                    newr->count = count;
                    return { pos.shift(), newn, ts, tail };
                } else if (idx == 0) {
                    return { pos.shift(), nullptr, ts, tail };
                } else if (Collapse && idx == 1 && pos.shift() > BL) {
                    auto newn = pos.node()->inner()[0];
                    return { pos.shift() - B, newn->inc(), ts, tail };
                } else {
                    auto newn = node_t::copy_inner_r(pos.node(), idx);
                    return { pos.shift(), newn, ts, tail };
                }
            } catch (...) {
                assert(!next || pos.shift() > BL);
                if (next) dec_inner(next, pos.shift() - B,
                                    last + 1 - ts - pos.size_before(idx));
                if (tail) dec_leaf(tail, ts);
                throw;
            }
        }
    }

    template <typename PosT>
    friend result_t visit_regular(this_t, PosT&& pos, size_t last)
    {
        auto idx = pos.index(last);
        if (Collapse && idx == 0) {
            return pos.towards_oh(this_t{}, last, idx);
        } else {
            using std::get;
            auto subs = pos.towards_oh(no_collapse_t{}, last, idx);
            auto next = get<1>(subs);
            auto ts   = get<2>(subs);
            auto tail = get<3>(subs);
            try {
                if (next) {
                    auto newn  = node_t::copy_inner_n(idx + 1, pos.node(), idx);
                    newn->inner()[idx] = next;
                    return { pos.shift(), newn, ts, tail };
                } else if (idx == 0) {
                    return { pos.shift(), nullptr, ts, tail };
                } else if (Collapse && idx == 1 && pos.shift() > BL) {
                    auto newn = pos.node()->inner()[0];
                    return { pos.shift() - B, newn->inc(), ts, tail };
                } else {
                    auto newn = node_t::copy_inner_n(idx, pos.node(), idx);
                    return { pos.shift(), newn, ts, tail };
                }
            } catch (...) {
                assert(!next || pos.shift() > BL);
                assert(tail);
                if (next) dec_regular(next, pos.shift() - B, last + 1 - ts);
                dec_leaf(tail, ts);
                throw;
            }
        }
    }

    template <typename PosT>
    friend result_t visit_leaf(this_t, PosT&& pos, size_t last)
    {
        auto old_tail_size = pos.count();
        auto new_tail_size = pos.index(last) + 1;
        auto new_tail      = new_tail_size == old_tail_size
            ? pos.node()->inc()
            : node_t::copy_leaf(pos.node(), new_tail_size);
        return { 0, nullptr, new_tail_size, new_tail };
    };
};

struct dec_left_visitor
{
    using this_t = dec_left_visitor;
    using dec_t  = dec_visitor;

    template <typename Pos>
    friend void visit_relaxed(this_t, Pos&& p, count_t idx)
    {
        using node_t = node_type<Pos>;
        auto node = p.node();
        if (node->dec()) {
            p.each_left(dec_t{}, idx);
            node_t::delete_inner_r(node);
        }
    }

    template <typename Pos>
    friend void visit_regular(this_t, Pos&& p, count_t idx)
    {
        using node_t = node_type<Pos>;
        auto node = p.node();
        if (node->dec()) {
            p.each_left(dec_t{}, idx);
            node_t::delete_inner(node);
        }
    }

    template <typename Pos>
    friend void visit_leaf(this_t, Pos&& p, count_t idx)
    { IMMER_UNREACHABLE; }
};

template <typename NodeT, bool Collapse=true, bool Mutating=true>
struct slice_left_mut_visitor
{
    using node_t = NodeT;
    using this_t = slice_left_mut_visitor;
    using edit_t = typename NodeT::edit_t;
    using value_t = typename NodeT::value_t;
    using relaxed_t = typename NodeT::relaxed_t;
    // returns a new shift and new root
    using result_t = std::tuple<shift_t, NodeT*>;

    using no_collapse_t = slice_left_mut_visitor<NodeT, false, true>;
    using no_collapse_no_mut_t = slice_left_mut_visitor<NodeT, false, false>;
    using no_mut_t = slice_left_mut_visitor<NodeT, Collapse, false>;

    static constexpr auto B  = NodeT::bits;
    static constexpr auto BL = NodeT::bits_leaf;

    template <typename PosT>
    friend result_t visit_relaxed(this_t, PosT&& pos, size_t first, edit_t e)
    {
        auto idx    = pos.subindex(first);
        auto count  = pos.count();
        auto node   = pos.node();
        auto mutate = Mutating && node->can_mutate(e);
        auto left_size  = pos.size_before(idx);
        auto child_size = pos.size_sbh(idx, left_size);
        auto dropped_size = first;
        auto child_dropped_size = dropped_size - left_size;
        if (Collapse && pos.shift() > BL && idx == pos.count() - 1) {
            auto r = mutate
                ? pos.towards_sub_oh(this_t{}, first, idx, e)
                : pos.towards_sub_oh(no_mut_t{}, first, idx, e);
            if (Mutating) pos.visit(dec_left_visitor{}, idx);
            return r;
        } else {
            using std::get;
            auto newn = mutate
                ? (node->ensure_mutable_relaxed(e), node)
                : node_t::make_inner_r_e(e);
            auto newr = newn->relaxed();
            auto newcount = count - idx;
            auto new_child_size = child_size - child_dropped_size;
            try {
                auto subs  = mutate
                    ? pos.towards_sub_oh(no_collapse_t{}, first, idx, e)
                    : pos.towards_sub_oh(no_collapse_no_mut_t{}, first, idx, e);
                if (mutate) pos.each_left(dec_visitor{}, idx);
                pos.copy_sizes(idx + 1, newcount - 1,
                               new_child_size, newr->sizes + 1);
                std::uninitialized_copy(node->inner() + idx + 1,
                                        node->inner() + count,
                                        newn->inner() + 1);
                newn->inner()[0] = get<1>(subs);
                newr->sizes[0] = new_child_size;
                newr->count = newcount;
                if (!mutate) {
                    node_t::inc_nodes(newn->inner() + 1, newcount - 1);
                    if (Mutating) pos.visit(dec_visitor{});
                }
                return { pos.shift(), newn };
            } catch (...) {
                if (!mutate) node_t::delete_inner_r(newn);
                throw;
            }
        }
    }

    template <typename PosT>
    friend result_t visit_regular(this_t, PosT&& pos, size_t first, edit_t e)
    {
        auto idx    = pos.subindex(first);
        auto count  = pos.count();
        auto node   = pos.node();
        auto mutate = Mutating
            // this is more restrictive than actually needed because
            // it causes the algorithm to also avoid mutating the leaf
            // in place
            && !node_t::embed_relaxed
            && node->can_mutate(e);
        auto left_size  = pos.size_before(idx);
        auto child_size = pos.size_sbh(idx, left_size);
        auto dropped_size = first;
        auto child_dropped_size = dropped_size - left_size;
        if (Collapse && pos.shift() > BL && idx == pos.count() - 1) {
            auto r = mutate
                ? pos.towards_sub_oh(this_t{}, first, idx, e)
                : pos.towards_sub_oh(no_mut_t{}, first, idx, e);
            if (Mutating) pos.visit(dec_left_visitor{}, idx);
            return r;
        } else {
            using std::get;
            // if possible, we convert the node to a relaxed one
            // simply by allocating a `relaxed_t` size table for
            // it... maybe some of this magic should be moved as a
            // `node<...>` static method...
            auto newcount = count - idx;
            auto newn = mutate
                ? (node->impl.data.inner.relaxed = new (
                       check_alloc(node_t::heap::allocate(
                                       node_t::max_sizeof_relaxed,
                                       norefs_tag{}))) relaxed_t,
                   node)
                : node_t::make_inner_r_e(e);
            auto newr = newn->relaxed();
            try {
                auto subs = mutate
                    ? pos.towards_sub_oh(no_collapse_t{}, first, idx, e)
                    : pos.towards_sub_oh(no_collapse_no_mut_t{}, first, idx, e);
                if (mutate) pos.each_left(dec_visitor{}, idx);
                newr->sizes[0] = child_size - child_dropped_size;
                pos.copy_sizes(idx + 1, newcount - 1,
                               newr->sizes[0], newr->sizes + 1);
                newr->count = newcount;
                newn->inner()[0] = get<1>(subs);
                std::uninitialized_copy(node->inner() + idx + 1,
                                        node->inner() + count,
                                        newn->inner() + 1);
                if (!mutate) {
                    node_t::inc_nodes(newn->inner() + 1, newcount - 1);
                    if (Mutating) pos.visit(dec_visitor{});
                }
                return { pos.shift(), newn };
            } catch (...) {
                if (!mutate) node_t::delete_inner_r(newn);
                else {
                    // restore the regular node that we were
                    // attempting to relax...
                    node_t::heap::deallocate(node->impl.data.inner.relaxed);
                    node->impl.data.inner.relaxed = nullptr;
                }
                throw;
            }
        }
    }

    template <typename PosT>
    friend result_t visit_leaf(this_t, PosT&& pos, size_t first, edit_t e)
    {
        auto node   = pos.node();
        auto idx    = pos.index(first);
        auto count  = pos.count();
        auto mutate = Mutating
            && std::is_nothrow_move_constructible<value_t>::value
            && node->can_mutate(e);
        if (mutate) {
            auto data = node->leaf();
            auto newcount = count - idx;
            std::move(data + idx, data + count, data);
            destroy_n(data + newcount, idx);
            return { 0, node };
        } else {
            auto newn = node_t::copy_leaf_e(e, node, idx, count);
            if (Mutating) pos.visit(dec_visitor{});
            return { 0, newn };
        }
    };
};

template <typename NodeT, bool Collapse=true>
struct slice_left_visitor
{
    using node_t = NodeT;
    using this_t = slice_left_visitor;

    // returns a new shift and new root
    using result_t = std::tuple<shift_t, NodeT*>;
    using no_collapse_t = slice_left_visitor<NodeT, false>;

    static constexpr auto B  = NodeT::bits;
    static constexpr auto BL = NodeT::bits_leaf;

    template <typename PosT>
    friend result_t visit_inner(this_t, PosT&& pos, size_t first)
    {
        auto idx    = pos.subindex(first);
        auto count  = pos.count();
        auto left_size  = pos.size_before(idx);
        auto child_size = pos.size_sbh(idx, left_size);
        auto dropped_size = first;
        auto child_dropped_size = dropped_size - left_size;
        if (Collapse && pos.shift() > BL && idx == pos.count() - 1) {
            return pos.towards_sub_oh(this_t{}, first, idx);
        } else {
            using std::get;
            auto n     = pos.node();
            auto newn  = node_t::make_inner_r_n(count - idx);
            try {
                auto subs  = pos.towards_sub_oh(no_collapse_t{}, first, idx);
                auto newr  = newn->relaxed();
                newr->count = count - idx;
                newr->sizes[0] = child_size - child_dropped_size;
                pos.copy_sizes(idx + 1, newr->count - 1,
                               newr->sizes[0], newr->sizes + 1);
                assert(newr->sizes[newr->count - 1] == pos.size() - dropped_size);
                newn->inner()[0] = get<1>(subs);
                std::uninitialized_copy(n->inner() + idx + 1,
                                        n->inner() + count,
                                        newn->inner() + 1);
                node_t::inc_nodes(newn->inner() + 1, newr->count - 1);
                return { pos.shift(), newn };
            } catch (...) {
                node_t::delete_inner_r(newn);
                throw;
            }
        }
    }

    template <typename PosT>
    friend result_t visit_leaf(this_t, PosT&& pos, size_t first)
    {
        auto n = node_t::copy_leaf(pos.node(), pos.index(first), pos.count());
        return { 0, n };
    };
};

template <typename Node>
struct concat_center_pos
{
    static constexpr auto B  = Node::bits;
    static constexpr auto BL = Node::bits_leaf;

    static constexpr count_t max_children = 3;

    using node_t = Node;
    using edit_t = typename Node::edit_t;

    shift_t shift_ = 0u;
    count_t count_ = 0u;
    node_t* nodes_[max_children];
    size_t  sizes_[max_children];

    auto shift() const { return shift_; }

    concat_center_pos(shift_t s,
                      Node* n0, size_t s0)
        : shift_{s}, count_{1}, nodes_{n0}, sizes_{s0} {}

    concat_center_pos(shift_t s,
                      Node* n0, size_t s0,
                      Node* n1, size_t s1)
        : shift_{s}, count_{2}, nodes_{n0, n1}, sizes_{s0, s1}  {}

    concat_center_pos(shift_t s,
                      Node* n0, size_t s0,
                      Node* n1, size_t s1,
                      Node* n2, size_t s2)
        : shift_{s}, count_{3}, nodes_{n0, n1, n2}, sizes_{s0, s1, s2} {}

    template <typename Visitor, typename... Args>
    void each_sub(Visitor v, Args&& ...args)
    {
        if (shift_ == BL) {
            for (auto i = count_t{0}; i < count_; ++i)
                make_leaf_sub_pos(nodes_[i], sizes_[i]).visit(v, args...);
        } else {
            for (auto i = count_t{0}; i < count_; ++i)
                make_relaxed_pos(nodes_[i], shift_ - B, nodes_[i]->relaxed()).visit(v, args...);
        }
    }

    relaxed_pos<Node> realize() &&
    {
        if (count_ > 1) {
            try {
                auto result = node_t::make_inner_r_n(count_);
                auto r      = result->relaxed();
                r->count    = count_;
                std::copy(nodes_, nodes_ + count_, result->inner());
                std::copy(sizes_, sizes_ + count_, r->sizes);
                return { result, shift_, r };
            } catch (...) {
                each_sub(dec_visitor{});
                throw;
            }
        } else {
            assert(shift_ >= B + BL);
            return { nodes_[0], shift_ - B, nodes_[0]->relaxed() };
        }
    }

    relaxed_pos<Node> realize_e(edit_t e)
    {
        if (count_ > 1) {
            auto result = node_t::make_inner_r_e(e);
            auto r      = result->relaxed();
            r->count    = count_;
            std::copy(nodes_, nodes_ + count_, result->inner());
            std::copy(sizes_, sizes_ + count_, r->sizes);
            return { result, shift_, r };
        } else {
            assert(shift_ >= B + BL);
            return { nodes_[0], shift_ - B, nodes_[0]->relaxed() };
        }
    }
};

template <typename Node>
struct concat_merger
{
    using node_t = Node;
    static constexpr auto B  = Node::bits;
    static constexpr auto BL = Node::bits_leaf;

    using result_t = concat_center_pos<Node>;

    count_t* curr_;
    count_t  n_;
    result_t result_;

    concat_merger(shift_t shift, count_t* counts, count_t n)
        : curr_{counts}
        , n_{n}
        , result_{shift + B, node_t::make_inner_r_n(std::min(n_, branches<B>)), 0}
    {}

    node_t*  to_        = {};
    count_t  to_offset_ = {};
    size_t   to_size_   = {};

    void add_child(node_t* p, size_t size)
    {
        ++curr_;
        auto parent  = result_.nodes_[result_.count_ - 1];
        auto relaxed = parent->relaxed();
        if (relaxed->count == branches<B>) {
            assert(result_.count_ < result_t::max_children);
            n_ -= branches<B>;
            parent  = node_t::make_inner_r_n(std::min(n_, branches<B>));
            relaxed = parent->relaxed();
            result_.nodes_[result_.count_] = parent;
            result_.sizes_[result_.count_] = result_.sizes_[result_.count_ - 1];
            ++result_.count_;
        }
        auto idx = relaxed->count++;
        result_.sizes_[result_.count_ - 1] += size;
        relaxed->sizes[idx] = size + (idx ? relaxed->sizes[idx - 1] : 0);
        parent->inner() [idx] = p;
    };

    template <typename Pos>
    void merge_leaf(Pos&& p)
    {
        auto from       = p.node();
        auto from_size  = p.size();
        auto from_count = p.count();
        assert(from_size);
        if (!to_ && *curr_ == from_count) {
            add_child(from, from_size);
            from->inc();
        } else {
            auto from_offset = count_t{};
            auto from_data   = from->leaf();
            do {
                if (!to_) {
                    to_ = node_t::make_leaf_n(*curr_);
                    to_offset_ = 0;
                }
                auto data = to_->leaf();
                auto to_copy = std::min(from_count - from_offset,
                                        *curr_ - to_offset_);
                std::uninitialized_copy(from_data + from_offset,
                                        from_data + from_offset + to_copy,
                                        data + to_offset_);
                to_offset_  += to_copy;
                from_offset += to_copy;
                if (*curr_ == to_offset_) {
                    add_child(to_, to_offset_);
                    to_ = nullptr;
                }
            } while (from_offset != from_count);
        }
    }

    template <typename Pos>
    void merge_inner(Pos&& p)
    {
        auto from       = p.node();
        auto from_size  = p.size();
        auto from_count = p.count();
        assert(from_size);
        if (!to_ && *curr_ == from_count) {
            add_child(from, from_size);
            from->inc();
        } else {
            auto from_offset = count_t{};
            auto from_data  = from->inner();
            do {
                if (!to_) {
                    to_ = node_t::make_inner_r_n(*curr_);
                    to_offset_ = 0;
                    to_size_   = 0;
                }
                auto data    = to_->inner();
                auto to_copy = std::min(from_count - from_offset,
                                        *curr_ - to_offset_);
                std::uninitialized_copy(from_data + from_offset,
                                        from_data + from_offset + to_copy,
                                        data + to_offset_);
                node_t::inc_nodes(from_data + from_offset, to_copy);
                auto sizes   = to_->relaxed()->sizes;
                p.copy_sizes(from_offset, to_copy,
                             to_size_, sizes + to_offset_);
                to_offset_  += to_copy;
                from_offset += to_copy;
                to_size_     = sizes[to_offset_ - 1];
                if (*curr_ == to_offset_) {
                    to_->relaxed()->count = to_offset_;
                    add_child(to_, to_size_);
                    to_ = nullptr;
                }
            } while (from_offset != from_count);
        }
    }

    concat_center_pos<Node> finish() const
    {
        assert(!to_);
        return result_;
    }

    void abort()
    {
        auto shift = result_.shift_ - B;
        if (to_) {
            if (shift == BL)
                node_t::delete_leaf(to_, to_offset_);
            else {
                to_->relaxed()->count = to_offset_;
                dec_relaxed(to_, shift - B);
            }
        }
        result_.each_sub(dec_visitor());
    }
};

struct concat_merger_visitor
{
    using this_t = concat_merger_visitor;

    template <typename Pos, typename Merger>
    friend void visit_inner(this_t, Pos&& p, Merger& merger)
    { merger.merge_inner(p); }

    template <typename Pos, typename Merger>
    friend void visit_leaf(this_t, Pos&& p, Merger& merger)
    { merger.merge_leaf(p); }
};

struct concat_rebalance_plan_fill_visitor
{
    using this_t = concat_rebalance_plan_fill_visitor;

    template <typename Pos, typename Plan>
    friend void visit_node(this_t, Pos&& p, Plan& plan)
    {
        auto count = p.count();
        assert(plan.n < Plan::max_children);
        plan.counts[plan.n++] = count;
        plan.total += count;
    }
};

template <bits_t B, bits_t BL>
struct concat_rebalance_plan
{
    static constexpr auto max_children = 2 * branches<B> + 1;

    count_t counts [max_children];
    count_t n = 0u;
    count_t total = 0u;

    template <typename LPos, typename CPos, typename RPos>
    void fill(LPos&& lpos, CPos&& cpos, RPos&& rpos)
    {
        assert(n == 0u);
        assert(total == 0u);
        using visitor_t = concat_rebalance_plan_fill_visitor;
        lpos.each_left_sub(visitor_t{}, *this);
        cpos.each_sub(visitor_t{}, *this);
        rpos.each_right_sub(visitor_t{}, *this);
    }

    void shuffle(shift_t shift)
    {
        // gcc seems to not really understand this code... :(
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
        constexpr count_t rrb_extras    = 2;
        constexpr count_t rrb_invariant = 1;
        const auto bits     = shift == BL ? BL : B;
        const auto branches = count_t{1} << bits;
        const auto optimal  = ((total - 1) >> bits) + 1;
        count_t i = 0;
        while (n >= optimal + rrb_extras) {
            // skip ok nodes
            while (counts[i] > branches - rrb_invariant) i++;
            // short node, redistribute
            auto remaining = counts[i];
            do {
                auto count = std::min(remaining + counts[i+1], branches);
                counts[i] = count;
                remaining +=  counts[i + 1] - count;
                ++i;
            } while (remaining > 0);
            // remove node
            std::move(counts + i + 1, counts + n, counts + i);
            --n;
            --i;
        }
#pragma GCC diagnostic pop
    }

    template <typename LPos, typename CPos, typename RPos>
    concat_center_pos<node_type<CPos>>
    merge(LPos&& lpos, CPos&& cpos, RPos&& rpos)
    {
        using node_t    = node_type<CPos>;
        using merger_t  = concat_merger<node_t>;
        using visitor_t = concat_merger_visitor;
        auto merger = merger_t{cpos.shift(), counts, n};
        try {
            lpos.each_left_sub(visitor_t{}, merger);
            cpos.each_sub(visitor_t{}, merger);
            rpos.each_right_sub(visitor_t{}, merger);
            cpos.each_sub(dec_visitor{});
            return merger.finish();
        } catch (...) {
            merger.abort();
            throw;
        }
    }
};

template <typename Node, typename LPos, typename CPos, typename RPos>
concat_center_pos<Node>
concat_rebalance(LPos&& lpos, CPos&& cpos, RPos&& rpos)
{
    auto plan = concat_rebalance_plan<Node::bits, Node::bits_leaf>{};
    plan.fill(lpos, cpos, rpos);
    plan.shuffle(cpos.shift());
    try {
        return plan.merge(lpos, cpos, rpos);
    } catch (...) {
        cpos.each_sub(dec_visitor{});
        throw;
    }
}

template <typename Node, typename LPos, typename TPos, typename RPos>
concat_center_pos<Node>
concat_leafs(LPos&& lpos, TPos&& tpos, RPos&& rpos)
{
    static_assert(Node::bits >= 2, "");
    assert(lpos.shift() == tpos.shift());
    assert(lpos.shift() == rpos.shift());
    assert(lpos.shift() == 0);
    if (tpos.count() > 0)
        return {
            Node::bits_leaf,
            lpos.node()->inc(), lpos.count(),
            tpos.node()->inc(), tpos.count(),
            rpos.node()->inc(), rpos.count(),
        };
    else
        return {
            Node::bits_leaf,
            lpos.node()->inc(), lpos.count(),
            rpos.node()->inc(), rpos.count(),
        };
}

template <typename Node>
struct concat_left_visitor;
template <typename Node>
struct concat_right_visitor;
template <typename Node>
struct concat_both_visitor;

template <typename Node, typename LPos, typename TPos, typename RPos>
concat_center_pos<Node>
concat_inners(LPos&& lpos, TPos&& tpos, RPos&& rpos)
{
    auto lshift = lpos.shift();
    auto rshift = rpos.shift();
    if (lshift > rshift) {
        auto cpos = lpos.last_sub(concat_left_visitor<Node>{}, tpos, rpos);
        return concat_rebalance<Node>(lpos, cpos, null_sub_pos{});
    } else if (lshift < rshift) {
        auto cpos = rpos.first_sub(concat_right_visitor<Node>{}, lpos, tpos);
        return concat_rebalance<Node>(null_sub_pos{}, cpos, rpos);
    } else {
        assert(lshift == rshift);
        assert(Node::bits_leaf == 0u || lshift > 0);
        auto cpos = lpos.last_sub(concat_both_visitor<Node>{}, tpos, rpos);
        return concat_rebalance<Node>(lpos, cpos, rpos);
    }
}

template <typename Node>
struct concat_left_visitor
{
    using this_t = concat_left_visitor;

    template <typename LPos, typename TPos, typename RPos>
    friend concat_center_pos<Node>
    visit_inner(this_t, LPos&& lpos, TPos&& tpos, RPos&& rpos)
    { return concat_inners<Node>(lpos, tpos, rpos); }

    template <typename LPos, typename TPos, typename RPos>
    friend concat_center_pos<Node>
    visit_leaf(this_t, LPos&& lpos, TPos&& tpos, RPos&& rpos)
    { IMMER_UNREACHABLE; }
};

template <typename Node>
struct concat_right_visitor
{
    using this_t = concat_right_visitor;

    template <typename RPos, typename LPos, typename TPos>
    friend concat_center_pos<Node>
    visit_inner(this_t, RPos&& rpos, LPos&& lpos, TPos&& tpos)
    { return concat_inners<Node>(lpos, tpos, rpos); }

    template <typename RPos, typename LPos, typename TPos>
    friend concat_center_pos<Node>
    visit_leaf(this_t, RPos&& rpos, LPos&& lpos, TPos&& tpos)
    { return concat_leafs<Node>(lpos, tpos, rpos); }
};

template <typename Node>
struct concat_both_visitor
{
    using this_t = concat_both_visitor;

    template <typename LPos, typename TPos, typename RPos>
    friend concat_center_pos<Node>
    visit_inner(this_t, LPos&& lpos, TPos&& tpos, RPos&& rpos)
    { return rpos.first_sub(concat_right_visitor<Node>{}, lpos, tpos); }

    template <typename LPos, typename TPos, typename RPos>
    friend concat_center_pos<Node>
    visit_leaf(this_t, LPos&& lpos, TPos&& tpos, RPos&& rpos)
    { return rpos.first_sub_leaf(concat_right_visitor<Node>{}, lpos, tpos); }
};

template <typename Node>
struct concat_trees_right_visitor
{
    using this_t = concat_trees_right_visitor;

    template <typename RPos, typename LPos, typename TPos>
    friend concat_center_pos<Node>
    visit_node(this_t, RPos&& rpos, LPos&& lpos, TPos&& tpos)
    { return concat_inners<Node>(lpos, tpos, rpos); }
};

template <typename Node>
struct concat_trees_left_visitor
{
    using this_t = concat_trees_left_visitor;

    template <typename LPos, typename TPos, typename... Args>
    friend concat_center_pos<Node>
    visit_node(this_t, LPos&& lpos, TPos&& tpos, Args&& ...args)
    { return visit_maybe_relaxed_sub(
            args...,
            concat_trees_right_visitor<Node>{},
            lpos, tpos); }
};

template <typename Node>
relaxed_pos<Node>
concat_trees(Node* lroot, shift_t lshift, size_t lsize,
             Node* ltail, count_t ltcount,
             Node* rroot, shift_t rshift, size_t rsize)
{
    return visit_maybe_relaxed_sub(
        lroot, lshift, lsize,
        concat_trees_left_visitor<Node>{},
        make_leaf_pos(ltail, ltcount),
        rroot, rshift, rsize)
        .realize();
}

template <typename Node>
relaxed_pos<Node>
concat_trees(Node* ltail, count_t ltcount,
             Node* rroot, shift_t rshift, size_t rsize)
{
    return make_singleton_regular_sub_pos(ltail, ltcount).visit(
        concat_trees_left_visitor<Node>{},
        empty_leaf_pos<Node>{},
        rroot, rshift, rsize)
        .realize();
}

template <typename Node>
using concat_center_mut_pos = concat_center_pos<Node>;

template <typename Node>
struct concat_merger_mut
{
    using node_t = Node;
    using edit_t = typename Node::edit_t;

    static constexpr auto B  = Node::bits;
    static constexpr auto BL = Node::bits_leaf;

    using result_t = concat_center_pos<Node>;

    edit_t ec_  = {};

    count_t* curr_;
    count_t  n_;
    result_t result_;
    count_t  count_ = 0;
    node_t*  candidate_ = nullptr;
    edit_t   candidate_e_;

    concat_merger_mut(edit_t ec, shift_t shift,
                      count_t* counts, count_t n,
                      edit_t e1, node_t* c1,
                      edit_t e2, node_t* c2)
        : ec_{ec}
        , curr_{counts}
        , n_{n}
        , result_{shift + B, nullptr, 0}
    {
        if (c1) {
            c1->ensure_mutable_relaxed_e(e1, ec);
            result_.nodes_[0] = c1->inc();
            candidate_ = c2;
            candidate_e_ = e2;
        } else if (c2) {
            c2->ensure_mutable_relaxed_e(e2, ec);
            result_.nodes_[0] = c2->inc();
        } else {
            result_.nodes_[0] = node_t::make_inner_r_e(ec);
        }
    }

    node_t*  to_        = {};
    count_t  to_offset_ = {};
    size_t   to_size_   = {};
    size_t   to_cleanup_ = {};

    void add_child(node_t* p, size_t size)
    {
        ++curr_;
        auto parent  = result_.nodes_[result_.count_ - 1];
        auto relaxed = parent->relaxed();
        if (count_ == branches<B>) {
            parent->relaxed()->count = count_;
            assert(result_.count_ < result_t::max_children);
            n_ -= branches<B>;
            if (candidate_) {
                parent = candidate_->inc();
                parent->ensure_mutable_relaxed_e(candidate_e_, ec_);
                candidate_ = nullptr;
            } else
                parent  = node_t::make_inner_r_e(ec_);
            count_ = 0;
            relaxed = parent->relaxed();
            result_.nodes_[result_.count_] = parent;
            result_.sizes_[result_.count_] = result_.sizes_[result_.count_ - 1];
            ++result_.count_;
        }
        auto idx = count_++;
        result_.sizes_[result_.count_ - 1] += size;
        relaxed->sizes[idx] = size + (idx ? relaxed->sizes[idx - 1] : 0);
        parent->inner() [idx] = p;
    };

    template <typename Pos>
    void merge_leaf(Pos&& p, edit_t e, bool mutating)
    {
        auto from       = p.node();
        auto from_size  = p.size();
        auto from_count = p.count();
        assert(from_size);
        if (!to_ && *curr_ == from_count) {
            add_child(from, from_size);
            if (!mutating) from->inc();
        } else {
            auto from_offset  = count_t{};
            auto from_data    = from->leaf();
            auto from_mutate  = mutating && from->can_mutate(e);
            auto from_adopted = false;
            do {
                if (!to_) {
                    if (from_mutate) {
                        assert(!from_adopted);
                        from_adopted = true;
                        node_t::ownee(from) = ec_;
                        to_ = from;
                        to_cleanup_ = from_count;
                        assert(from_count);
                    } else {
                        to_ = node_t::make_leaf_e(ec_);
                        to_cleanup_ = 0;
                    }
                    to_offset_ = 0;
                }
                auto data = to_->leaf();
                auto to_copy = std::min(from_count - from_offset,
                                        *curr_ - to_offset_);
                if (from == to_) {
                    if (from_offset != to_offset_)
                        std::move(from_data + from_offset,
                                  from_data + from_offset + to_copy,
                                  data + to_offset_);
                    to_cleanup_ -= to_copy;
                } else {
                    auto cleanup = std::min(to_copy, to_cleanup_);
                    destroy_n(data + to_offset_, cleanup);
                    to_cleanup_ -= cleanup;
                    if (!from_mutate)
                        std::uninitialized_copy(from_data + from_offset,
                                                from_data + from_offset + to_copy,
                                                data + to_offset_);
                    else
                        uninitialized_move(from_data + from_offset,
                                           from_data + from_offset + to_copy,
                                           data + to_offset_);
                }
                to_offset_  += to_copy;
                from_offset += to_copy;
                if (*curr_ == to_offset_) {
                    destroy_n(data + to_offset_, to_cleanup_);
                    add_child(to_, to_offset_);
                    to_ = nullptr;
                }
            } while (from_offset != from_count);
            if (mutating && !from_adopted && from->dec())
                node_t::delete_leaf(from, from_count);
        }
    }

    template <typename Pos>
    void merge_inner(Pos&& p, edit_t e, bool mutating)
    {
        auto from       = p.node();
        auto from_size  = p.size();
        auto from_count = p.count();
        assert(from_size);
        if (!to_ && *curr_ == from_count) {
            add_child(from, from_size);
            if (!mutating) from->inc();
        } else {
            auto from_offset  = count_t{};
            auto from_data    = from->inner();
            auto from_adopted = false;
            do {
                if (!to_) {
                    auto from_mutate = mutating
                        && from->can_relax()
                        && from->can_mutate(e);
                    if (from_mutate) {
                        assert(!from_adopted);
                        from_adopted = from_mutate;
                        node_t::ownee(from) = ec_;
                        from->ensure_mutable_relaxed_e(e, ec_);
                        to_ = from->inc();
                    } else {
                        to_ = node_t::make_inner_r_e(ec_);
                    }
                    to_offset_ = 0;
                    to_size_   = 0;
                }
                auto data    = to_->inner();
                auto to_copy = std::min(from_count - from_offset,
                                        *curr_ - to_offset_);
                auto sizes   = to_->relaxed()->sizes;
                if (from != to_ || from_offset != to_offset_) {
                    std::copy(from_data + from_offset,
                              from_data + from_offset + to_copy,
                              data + to_offset_);
                    if (!mutating)
                        node_t::inc_nodes(from_data + from_offset, to_copy);
                    p.copy_sizes(from_offset, to_copy,
                                 to_size_, sizes + to_offset_);
                }
                to_offset_  += to_copy;
                from_offset += to_copy;
                to_size_     = sizes[to_offset_ - 1];
                if (*curr_ == to_offset_) {
                    to_->relaxed()->count = to_offset_;
                    add_child(to_, to_size_);
                    to_ = nullptr;
                }
            } while (from_offset != from_count);
            if (mutating && !from_adopted && from->dec())
                node_t::delete_inner_any(from);
        }
    }

    concat_center_pos<Node> finish() const
    {
        assert(!to_);
        result_.nodes_[result_.count_ - 1]->relaxed()->count = count_;
        return result_;
    }

    void abort()
    {
        // We may have mutated stuff the tree in place, leaving
        // everything in a corrupted state...  It should be possible
        // to define cleanup properly, but that is a task for some
        // other day... ;)
        std::terminate();
    }
};

struct concat_merger_mut_visitor
{
    using this_t = concat_merger_mut_visitor;

    template <typename Pos, typename Merger>
    friend void visit_inner(this_t, Pos&& p,
                            Merger& merger, edit_type<Pos> e, bool mut)
    { merger.merge_inner(p, e, mut); }

    template <typename Pos, typename Merger>
    friend void visit_leaf(this_t, Pos&& p,
                           Merger& merger, edit_type<Pos> e, bool mut)
    { merger.merge_leaf(p, e, mut); }
};

template <bits_t B, bits_t BL>
struct concat_rebalance_plan_mut : concat_rebalance_plan<B, BL>
{
    using this_t = concat_rebalance_plan_mut;

    template <typename LPos, typename CPos, typename RPos>
    concat_center_mut_pos<node_type<CPos>>
    merge(edit_type<CPos> ec,
          edit_type<CPos> el, bool lmut, LPos&& lpos, CPos&& cpos,
          edit_type<CPos> er, bool rmut, RPos&& rpos)
    {
        using node_t    = node_type<CPos>;
        using merger_t  = concat_merger_mut<node_t>;
        using visitor_t = concat_merger_mut_visitor;
        auto lnode = ((node_t*)lpos.node());
        auto rnode = ((node_t*)rpos.node());
        auto lmut2 = lmut && lnode && lnode->can_relax() && lnode->can_mutate(el);
        auto rmut2 = rmut && rnode && rnode->can_relax() && rnode->can_mutate(er);
        auto merger = merger_t{
            ec, cpos.shift(), this->counts, this->n,
            el, lmut2 ? lnode : nullptr,
            er, rmut2 ? rnode : nullptr
        };
        try {
            lpos.each_left_sub(visitor_t{}, merger, el, lmut2);
            cpos.each_sub(visitor_t{}, merger, ec, true);
            rpos.each_right_sub(visitor_t{}, merger, er, rmut2);
            if (lmut && lnode && lnode->dec())
                node_t::delete_inner_any(lnode);
            if (rmut && rnode && rnode->dec())
                node_t::delete_inner_any(rnode);
            return merger.finish();
        } catch (...) {
            merger.abort();
            throw;
        }
    }
};

template <typename Node, typename LPos, typename CPos, typename RPos>
concat_center_pos<Node>
concat_rebalance_mut(edit_type<Node> ec,
                     edit_type<Node> el, bool lmut, LPos&& lpos, CPos&& cpos,
                     edit_type<Node> er, bool rmut, RPos&& rpos)
{
    auto plan = concat_rebalance_plan_mut<Node::bits, Node::bits_leaf>{};
    plan.fill(lpos, cpos, rpos);
    plan.shuffle(cpos.shift());
    return plan.merge(ec, el, lmut, lpos, cpos, er, rmut, rpos);
}

template <typename Node, typename LPos, typename TPos, typename RPos>
concat_center_mut_pos<Node>
concat_leafs_mut(edit_type<Node> ec,
                 edit_type<Node> el, bool lmut, LPos&& lpos, TPos&& tpos,
                 edit_type<Node> er, bool rmut, RPos&& rpos)
{
    static_assert(Node::bits >= 2, "");
    assert(lpos.shift() == tpos.shift());
    assert(lpos.shift() == rpos.shift());
    assert(lpos.shift() == 0);
    if (!lmut) lpos.node()->inc();
    if (!lmut && tpos.count()) tpos.node()->inc();
    if (!rmut) rpos.node()->inc();
    if (tpos.count() > 0)
        return {
            Node::bits_leaf,
            lpos.node(), lpos.count(),
            tpos.node(), tpos.count(),
            rpos.node(), rpos.count(),
        };
    else
        return {
            Node::bits_leaf,
            lpos.node(), lpos.count(),
            rpos.node(), rpos.count(),
        };
}

template <typename Node>
struct concat_left_mut_visitor;
template <typename Node>
struct concat_right_mut_visitor;
template <typename Node>
struct concat_both_mut_visitor;

template <typename Node, typename LPos, typename TPos, typename RPos>
concat_center_mut_pos<Node>
concat_inners_mut(edit_type<Node> ec,
                  edit_type<Node> el, bool lmut, LPos&& lpos, TPos&& tpos,
                  edit_type<Node> er, bool rmut, RPos&& rpos)
{
    auto lshift = lpos.shift();
    auto rshift = rpos.shift();
    // lpos.node() can be null it is a singleton_regular_sub_pos<...>,
    // this is, when the tree is just a tail...
    if (lshift > rshift) {
        auto lmut2 = lmut && (!lpos.node() || lpos.node()->can_mutate(el));
        auto cpos = lpos.last_sub(concat_left_mut_visitor<Node>{},
                                  ec, el, lmut2, tpos, er, rmut, rpos);
        return concat_rebalance_mut<Node>(ec,
                                          el, lmut, lpos, cpos,
                                          er, rmut, null_sub_pos{});
    } else if (lshift < rshift) {
        auto rmut2 = rmut && rpos.node()->can_mutate(er);
        auto cpos = rpos.first_sub(concat_right_mut_visitor<Node>{},
                                   ec, el, lmut, lpos, tpos, er, rmut2);
        return concat_rebalance_mut<Node>(ec,
                                          el, lmut, null_sub_pos{}, cpos,
                                          er, rmut, rpos);
    } else {
        assert(lshift == rshift);
        assert(Node::bits_leaf == 0u || lshift > 0);
        auto lmut2 = lmut && (!lpos.node() || lpos.node()->can_mutate(el));
        auto rmut2 = rmut && rpos.node()->can_mutate(er);
        auto cpos = lpos.last_sub(concat_both_mut_visitor<Node>{},
                                  ec, el, lmut2, tpos, er, rmut2, rpos);
        return concat_rebalance_mut<Node>(ec,
                                          el, lmut, lpos, cpos,
                                          er, rmut, rpos);
    }
}

template <typename Node>
struct concat_left_mut_visitor
{
    using this_t = concat_left_mut_visitor;
    using edit_t = typename Node::edit_t;

    template <typename LPos, typename TPos, typename RPos>
    friend concat_center_mut_pos<Node>
    visit_inner(this_t, LPos&& lpos, edit_t ec,
                edit_t el, bool lmut, TPos&& tpos,
                edit_t er, bool rmut, RPos&& rpos)
    { return concat_inners_mut<Node>(
            ec, el, lmut, lpos, tpos, er, rmut, rpos); }

    template <typename LPos, typename TPos, typename RPos>
    friend concat_center_mut_pos<Node>
    visit_leaf(this_t, LPos&& lpos, edit_t ec,
               edit_t el, bool lmut, TPos&& tpos,
               edit_t er, bool rmut, RPos&& rpos)
    { IMMER_UNREACHABLE; }
};

template <typename Node>
struct concat_right_mut_visitor
{
    using this_t = concat_right_mut_visitor;
    using edit_t = typename Node::edit_t;

    template <typename RPos, typename LPos, typename TPos>
    friend concat_center_mut_pos<Node>
    visit_inner(this_t, RPos&& rpos, edit_t ec,
                edit_t el, bool lmut, LPos&& lpos, TPos&& tpos,
                edit_t er, bool rmut)
    { return concat_inners_mut<Node>(
            ec, el, lmut, lpos, tpos, er, rmut, rpos); }

    template <typename RPos, typename LPos, typename TPos>
    friend concat_center_mut_pos<Node>
    visit_leaf(this_t, RPos&& rpos, edit_t ec,
               edit_t el, bool lmut, LPos&& lpos, TPos&& tpos,
               edit_t er, bool rmut)
    { return concat_leafs_mut<Node>(
            ec, el, lmut, lpos, tpos, er, rmut, rpos); }
};

template <typename Node>
struct concat_both_mut_visitor
{
    using this_t = concat_both_mut_visitor;
    using edit_t = typename Node::edit_t;

    template <typename LPos, typename TPos, typename RPos>
    friend concat_center_mut_pos<Node>
    visit_inner(this_t, LPos&& lpos, edit_t ec,
                edit_t el, bool lmut, TPos&& tpos,
                edit_t er, bool rmut, RPos&& rpos)
    { return rpos.first_sub(concat_right_mut_visitor<Node>{},
                            ec, el, lmut, lpos, tpos, er, rmut); }

    template <typename LPos, typename TPos, typename RPos>
    friend concat_center_mut_pos<Node>
    visit_leaf(this_t, LPos&& lpos, edit_t ec,
               edit_t el, bool lmut, TPos&& tpos,
               edit_t er, bool rmut, RPos&& rpos)
    { return rpos.first_sub_leaf(concat_right_mut_visitor<Node>{},
                                 ec, el, lmut, lpos, tpos, er, rmut); }
};

template <typename Node>
struct concat_trees_right_mut_visitor
{
    using this_t = concat_trees_right_mut_visitor;
    using edit_t = typename Node::edit_t;

    template <typename RPos, typename LPos, typename TPos>
    friend concat_center_mut_pos<Node>
    visit_node(this_t, RPos&& rpos, edit_t ec,
               edit_t el, bool lmut, LPos&& lpos, TPos&& tpos,
               edit_t er, bool rmut)
    { return concat_inners_mut<Node>(
            ec, el, lmut, lpos, tpos, er, rmut, rpos); }
};

template <typename Node>
struct concat_trees_left_mut_visitor
{
    using this_t = concat_trees_left_mut_visitor;
    using edit_t = typename Node::edit_t;

    template <typename LPos, typename TPos, typename... Args>
    friend concat_center_mut_pos<Node>
    visit_node(this_t, LPos&& lpos, edit_t ec,
               edit_t el, bool lmut, TPos&& tpos,
               edit_t er, bool rmut, Args&& ...args)
    { return visit_maybe_relaxed_sub(
            args...,
            concat_trees_right_mut_visitor<Node>{},
            ec, el, lmut, lpos, tpos, er, rmut); }
};

template <typename Node>
relaxed_pos<Node>
concat_trees_mut(edit_type<Node> ec,
                 edit_type<Node> el, bool lmut,
                 Node* lroot, shift_t lshift, size_t lsize,
                 Node* ltail, count_t ltcount,
                 edit_type<Node> er, bool rmut,
                 Node* rroot, shift_t rshift, size_t rsize)
{
    return visit_maybe_relaxed_sub(
        lroot, lshift, lsize,
        concat_trees_left_mut_visitor<Node>{},
        ec,
        el, lmut, make_leaf_pos(ltail, ltcount),
        er, rmut, rroot, rshift, rsize)
        .realize_e(ec);
}

template <typename Node>
relaxed_pos<Node>
concat_trees_mut(edit_type<Node> ec,
                 edit_type<Node> el, bool lmut,
                 Node* ltail, count_t ltcount,
                 edit_type<Node> er, bool rmut,
                 Node* rroot, shift_t rshift, size_t rsize)
{
    return make_singleton_regular_sub_pos(ltail, ltcount).visit(
        concat_trees_left_mut_visitor<Node>{},
        ec,
        el, lmut, empty_leaf_pos<Node>{},
        er, rmut, rroot, rshift, rsize)
        .realize_e(ec);
}

} // namespace rbts
} // namespace detail
} // namespace immer
