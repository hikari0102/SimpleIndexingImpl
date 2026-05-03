#pragma once

#include <cstddef>
#include <functional>

template <typename Key, typename Value>
class dbindex {
public:
    using Visitor = std::function<void(const Key&, const Value&)>;
    virtual ~dbindex() = default;
    virtual bool insert(const Key& key, const Value& value) = 0;
    virtual const Value* find(const Key& key) const = 0;
    virtual bool contains(const Key& key) const = 0;
    virtual bool erase(const Key& key) = 0;
    virtual void range(const Key& low, const Key& high, const Visitor& visit) const = 0;
    virtual std::size_t size()  const = 0;
    virtual bool empty() const = 0;
    virtual bool check() const = 0;
    virtual int height() const = 0;
    virtual double fill_factor() const = 0;
};