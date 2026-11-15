/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2025 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <memory>
#include <unordered_map>
#include <variant>
#include <vector>

namespace muse {
template<typename KeyType, typename ValType>
class SharedHashMap
{
public:
    // like std
    using key_type = KeyType;
    using mapped_type = ValType;
    using value_type = std::pair<const KeyType, ValType>;

    using PairType = value_type;
    using Data = std::unordered_map<KeyType, ValType>;
    using DataPtr = std::shared_ptr<Data>;
    typedef typename Data::iterator iterator;
    typedef typename Data::const_iterator const_iterator;

    SharedHashMap()
    {
        m_dataPtr = std::make_shared<Data>();
    }

    SharedHashMap(const size_t reserveSize)
    {
        m_dataPtr = std::make_shared<Data>(reserveSize);
    }

    SharedHashMap(std::initializer_list<PairType> initList)
    {
        m_dataPtr = std::make_shared<Data>(initList);
    }

    SharedHashMap& operator=(std::initializer_list<PairType> initList)
    {
        m_dataPtr = std::make_shared<Data>(initList);
        return *this;
    }

    SharedHashMap(const SharedHashMap&) = default;
    SharedHashMap(SharedHashMap&&) = default;
    SharedHashMap& operator=(const SharedHashMap&) = default;
    SharedHashMap& operator=(SharedHashMap&&) = default;

    const ValType& at(const KeyType& key) const
    {
        return m_dataPtr->at(key);
    }

    ValType& at(const KeyType& key)
    {
        ensureDetach();
        return m_dataPtr->at(key);
    }

    ValType& operator[](const KeyType& key)
    {
        ensureDetach();
        return m_dataPtr->operator [](key);
    }

    ValType& operator[](KeyType&& key)
    {
        ensureDetach();
        return m_dataPtr->operator [](std::forward<KeyType>(key));
    }

    iterator begin() noexcept
    {
        ensureDetach();
        return m_dataPtr->begin();
    }

    iterator end() noexcept
    {
        ensureDetach();
        return m_dataPtr->end();
    }

    const_iterator begin() const noexcept
    {
        return m_dataPtr->cbegin();
    }

    const_iterator end() const noexcept
    {
        return m_dataPtr->cend();
    }

    const_iterator cbegin() const noexcept
    {
        return m_dataPtr->cbegin();
    }

    const_iterator cend() const noexcept
    {
        return m_dataPtr->cend();
    }

    const_iterator find(const KeyType& key) const noexcept
    {
        return m_dataPtr->find(key);
    }

    iterator find(const KeyType& key) noexcept
    {
        ensureDetach();
        return m_dataPtr->find(key);
    }

    bool contains(const KeyType& key) const noexcept
    {
        return find(key) != end();
    }

    bool empty() const noexcept
    {
        return m_dataPtr->empty();
    }

    size_t size() const noexcept
    {
        return m_dataPtr->size();
    }

    void insert(const PairType& pair)
    {
        ensureDetach();
        m_dataPtr->insert(pair);
    }

    void insert(PairType&& pair)
    {
        ensureDetach();
        m_dataPtr->insert(std::forward<PairType>(pair));
    }

    void insert(const_iterator first, const_iterator last)
    {
        ensureDetach();
        m_dataPtr->insert(first, last);
    }

    iterator insert(const_iterator hint, const PairType& pair)
    {
        return withDetachedHint(hint, [this, &pair](const_iterator h) { return m_dataPtr->insert(h, pair); });
    }

    iterator insert(const_iterator hint, PairType&& pair)
    {
        return withDetachedHint(hint, [this, &pair](const_iterator h) {
            return m_dataPtr->insert(h, std::forward<PairType>(pair));
        });
    }

    void merge(SharedHashMap& source)
    {
        ensureDetach();
        source.ensureDetach();
        m_dataPtr->merge(*source.m_dataPtr);
    }

    void insert_or_assign(const KeyType& key, ValType&& val)
    {
        ensureDetach();
        m_dataPtr->insert_or_assign(key, std::forward<ValType>(val));
    }

    void insert_or_assign(const KeyType& key, const ValType& val)
    {
        ensureDetach();
        m_dataPtr->insert_or_assign(key, val);
    }

    template<typename ... Args>
    void emplace(Args&& ... args)
    {
        ensureDetach();
        m_dataPtr->emplace(std::forward<Args>(args)...);
    }

    template<typename ... Args>
    iterator emplace_hint(const_iterator hint, Args&& ... args)
    {
        return withDetachedHint(hint, [this, &args ...](const_iterator h) {
            return m_dataPtr->emplace_hint(h, std::forward<Args>(args)...);
        });
    }

    template<typename ... Args>
    std::pair<iterator, bool> try_emplace(const KeyType& key, Args&& ... args)
    {
        ensureDetach();
        return m_dataPtr->try_emplace(key, std::forward<Args>(args)...);
    }

    template<typename ... Args>
    iterator try_emplace(const_iterator hint, const KeyType& key, Args&& ... args)
    {
        return withDetachedHint(hint, [this, &key, &args ...](const_iterator h) {
            return m_dataPtr->try_emplace(h, key, std::forward<Args>(args)...);
        });
    }

    template<typename ... Args>
    iterator try_emplace(const_iterator hint, KeyType&& key, Args&& ... args)
    {
        return withDetachedHint(hint, [this, &key, &args ...](const_iterator h) {
            return m_dataPtr->try_emplace(h, std::forward<KeyType>(key), std::forward<Args>(args)...);
        });
    }

    void clear() noexcept
    {
        ensureDetach();
        m_dataPtr->clear();
    }

    void erase(const KeyType& key)
    {
        ensureDetach();
        m_dataPtr->erase(key);
    }

    iterator erase(const_iterator pos)
    {
        return withDetachedHint(pos, [this](const_iterator p) { return m_dataPtr->erase(p); });
    }

    void erase(const_iterator first, const_iterator last)
    {
        if (m_dataPtr.use_count() == 1) {
            m_dataPtr->erase(first, last);
            return;
        }

        // The detached copy need not iterate in the same order as the shared data
        // (it doesn't with libc++), so a range can't be found again by its end points.
        // Remember which keys are in the range, then erase those from the copy.
        std::vector<KeyType> keys;
        for (const_iterator it = first; it != last; ++it) {
            keys.push_back(it->first);
        }

        ensureDetach();

        for (const KeyType& key : keys) {
            m_dataPtr->erase(key);
        }
    }

    bool operator ==(const SharedHashMap& another) const noexcept
    {
        if (m_dataPtr == another.m_dataPtr) {
            return true;
        }

        return *m_dataPtr == *another.m_dataPtr;
    }

    bool operator !=(const SharedHashMap& another) const noexcept
    {
        return !this->operator ==(another);
    }

private:
    void ensureDetach()
    {
        if (!m_dataPtr) {
            return;
        }

        if (m_dataPtr.use_count() == 1) {
            return;
        }

        m_dataPtr = std::make_shared<Data>(*m_dataPtr);
    }

    // Detach-safe descriptor of a position
    struct AtEnd {};
    using Anchor = std::variant<AtEnd, KeyType>;

    Anchor describeAnchor(const_iterator it) const
    {
        if (it == m_dataPtr->cend()) {
            return AtEnd {};
        }
        return it->first;
    }

    const_iterator resolveAnchor(const Anchor& a) const
    {
        if (std::holds_alternative<AtEnd>(a)) {
            return m_dataPtr->cend();
        }
        return m_dataPtr->find(std::get<KeyType>(a));
    }

    const_iterator detachAndReanchor(const_iterator hint)
    {
        const Anchor a = describeAnchor(hint);
        ensureDetach();
        return resolveAnchor(a);
    }

    template<typename Op>
    auto withDetachedHint(const_iterator hint, Op&& op)
    {
        if (m_dataPtr.use_count() == 1) {
            return op(hint);
        }

        const_iterator dhint = detachAndReanchor(hint);
        return op(dhint);
    }

    DataPtr m_dataPtr = nullptr;
};
}
